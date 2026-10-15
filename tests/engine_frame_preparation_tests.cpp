#include "engine_frame_preparation.h"

#include <cstdlib>
#include <iostream>
#include <string>
#include <thread>
#include <atomic>
#include <limits>
#include <stdexcept>
#include <vector>

namespace prep = w3vr::frame_preparation;
namespace submit = w3vr::frame_submission;

std::atomic<int> checks{};
thread_local const prep::Profile* fixture_profile=&prep::remastered_500c;

void require(bool condition, const char* message) {
    ++checks;
    if (!condition) { std::cerr << message << '\n'; std::exit(1); }
}

struct Frame {
    int references{1};
    bool prepared{};
    bool effects_applied{};
    float effects_time{};
};

struct Fixture {
    const prep::Profile* profile{fixture_profile};
    Frame primary{}, duplicate{};
    int engine{}, effects_argument{}, command{}, sentinel{};
    Frame* queued{};
    float effects_time{};
    int tick_count{}, caller_releases{}, invalidations{}, allocations{};
    std::string events;
    bool allocation_failure{}, immediate_completion{},publication_confirmed{true};
    prep::PendingPair* reenter{};
};
thread_local Fixture* current{};

void __fastcall prepare(void* engine, void* raw_frame) {
    auto& f = *current;
    auto* frame = static_cast<Frame*>(raw_frame);
    require(engine == &f.engine && frame->references > 0 && !frame->prepared,
        "Prepared a foreign, dead or already prepared frame");
    frame->prepared = true;
    f.events += frame == &f.primary ? 'L' : 'R';
    if (frame == &f.duplicate && f.reenter != nullptr)
        require(f.reenter->at_native_command(&f.primary, {}, true) ==
            prep::PendingPair::Completion::ignored,
            "Reentrant native callback replayed an already detached duplicate");
}

void tick_once(float delta) {
    ++current->tick_count;
    current->effects_time += delta;
    current->events += 'T';
}

void __fastcall apply(void* first_argument, void* raw_frame) {
    auto& f = *current;
    auto* frame = static_cast<Frame*>(raw_frame);
    require(first_argument == &f.effects_argument && frame->prepared &&
        frame->references > 0 && !frame->effects_applied,
        "Effects lost the observed argument, preceded preparation or repeated");
    frame->effects_applied = true;
    frame->effects_time = f.effects_time;
    f.events += frame == &f.primary ? 'l' : 'r';
}

void __fastcall release(void* raw_frame) {
    auto* frame = static_cast<Frame*>(raw_frame);
    require(frame == &current->duplicate && frame->references > 0,
        "Released the game's primary ownership or a dead duplicate");
    --frame->references;
    ++current->caller_releases;
    current->events += 'O';
}

void invalidate(void* owner, void* primary, void* duplicate) {
    auto* f = static_cast<Fixture*>(owner);
    require(primary == &f->primary && duplicate == &f->duplicate,
        "Invalidation removed a different pair");
    ++f->invalidations;
    f->events += 'X';
}

void* __fastcall allocate() {
    ++current->allocations;
    current->events += 'A';
    return current->allocation_failure ? &current->sentinel : &current->command;
}

void* __fastcall construct(void* command, void* raw_frame, void* auxiliary) {
    auto& f = *current;
    auto* frame = static_cast<Frame*>(raw_frame);
    require(command == &f.command && frame == &f.duplicate && auxiliary == nullptr &&
        frame->prepared && frame->references == 1,
        "Native command preceded duplicate preparation or changed ownership");
    ++frame->references;
    f.queued = frame;
    f.events += 'C';
    return command;
}

void complete() {
    require(current->queued != nullptr && current->queued->references > 0,
        "Completed a dead or nonexistent command reference");
    --current->queued->references;
    current->queued = nullptr;
    current->events += 'F';
}

bool __fastcall dispatch(void* command,void* frame) {
    require(command == &current->command && current->queued != nullptr,
        "Dispatched an unconstructed command");
    current->events += 'D';
    require(frame==current->queued,"Publication adapter changed its frame");
    if (current->immediate_completion) complete();
    return current->publication_confirmed;
}

submit::NativeCommandRoute route() {
    return {allocate, construct, dispatch, &current->sentinel, true,current->profile->submission};
}

void arm(prep::PendingPair& pair, Fixture& f) {
    current = &f;
    require(pair.arm(fixture_profile,&f.primary, &f.duplicate, release, invalidate, &f), "Could not arm pair");
}

void natural_prepare(prep::PendingPair& pair, Fixture& f, bool effects) {
    prepare(&f.engine, &f.primary);
    pair.observe_engine(fixture_profile,&f.primary, &f.engine, prepare, true);
    if (effects) {
        tick_once(0.016f);
        pair.observe_effects_tick(fixture_profile);
        apply(&f.effects_argument, &f.primary);
        pair.observe_effects(fixture_profile,&f.primary, &f.effects_argument, apply);
    }
}

void test_natural_clock_and_command_lifetimes() {
    for (bool effects : {false, true}) for (bool immediate : {false, true}) {
        Fixture f;
        f.immediate_completion = immediate;
        {
            prep::PendingPair pair;
            arm(pair, f);
            natural_prepare(pair, f, effects);
            require(f.allocations == 0 && !f.duplicate.prepared && f.caller_releases == 0,
                "Duplicate work escaped before the natural command boundary");
            require(pair.at_native_command(&f.primary, route(), true) ==
                prep::PendingPair::Completion::dispatched, "Prepared command rejected");
            const std::string expected = (effects ? "LTlRrACD" : "LRACD") +
                std::string(immediate ? "FO" : "O");
            require(f.events == expected && f.tick_count == (effects ? 1 : 0) &&
                f.primary.effects_time == f.duplicate.effects_time &&
                f.primary.effects_applied == f.duplicate.effects_applied &&
                f.primary.references == 1 && f.caller_releases == 1 && f.invalidations == 0,
                "Ordering, one shared clock, same effects state or ownership changed");
            require(!pair.available(), "A completed producer allowed a second pair");
            pair.cancel();
            require(f.caller_releases == 1, "Cancel released a submitted duplicate twice");
            if (!immediate) complete();
        }
        require(f.duplicate.references == 0 && f.primary.references == 1,
            "Command completion or producer exit leaked/consumed frame references");
    }
}

void test_cancellation_and_missing_stages() {
    for (int reason = 0; reason < 9; ++reason) {
        Fixture f;
        {
            prep::PendingPair pair;
            arm(pair, f);
            if (reason != 0 && reason != 1) natural_prepare(pair, f, false);
            if (reason == 2) pair.observe_effects_tick(fixture_profile); // Tick without apply.
            if (reason == 3) pair.observe_effects(fixture_profile,&f.primary, &f.effects_argument, apply);
            if (reason == 4) pair.observe_engine(fixture_profile,&f.primary, &f.engine, prepare, true);
            if (reason == 5) {
                pair.observe_effects_tick(fixture_profile); pair.observe_effects_tick(fixture_profile);
            }
            if (reason == 6) {
                pair.observe_effects_tick(fixture_profile);
                pair.observe_effects(fixture_profile,&f.primary, &f.effects_argument, apply);
                pair.observe_effects(fixture_profile,&f.primary, &f.effects_argument, apply);
            }
            if (reason == 7) pair.observe_engine(fixture_profile,&f.primary, &f.engine, prepare, false);
            if (reason != 0) require(pair.at_native_command(&f.primary, route(), reason != 8) ==
                prep::PendingPair::Completion::rejected, "Incomplete/changed producer accepted");
            // Reason 0 exits before its native command: owned right must die.
        }
        require(f.allocations == 0 && !f.duplicate.prepared && f.caller_releases == 1 &&
            f.invalidations == 1 && f.duplicate.references == 0 && f.primary.references == 1,
            "Cancelled producer allocated, leaked, double-released or kept pair identities");
    }
}

void test_failed_dispatch_and_foreign_calls() {
    Fixture f;
    prep::PendingPair pair;
    arm(pair, f);
    int foreign{};
    pair.observe_engine(fixture_profile,&foreign, &f.engine, prepare, true);
    pair.observe_effects(fixture_profile,&foreign, &f.effects_argument, apply);
    require(pair.at_native_command(&foreign, route(), true) ==
        prep::PendingPair::Completion::ignored && f.events.empty(),
        "Unrelated native frame drove this pair");
    natural_prepare(pair, f, false);
    f.allocation_failure = true;
    require(pair.at_native_command(&f.primary, route(), true) ==
        prep::PendingPair::Completion::submission_failed && f.events == "LRAOX" &&
        f.caller_releases == 1 && f.invalidations == 1 && f.duplicate.references == 0,
        "Native sentinel leaked prepared resources or retained pair identities");
}

void test_unconfirmed_pair_publication() {
    Fixture f;prep::PendingPair pair;arm(pair,f);natural_prepare(pair,f,false);f.publication_confirmed=false;
    require(pair.at_native_command(&f.primary,route(),true)==prep::PendingPair::Completion::submission_failed &&
        f.invalidations==1 && f.caller_releases==1 && f.duplicate.references==1 && f.events=="LRACDOX",
        "Unconfirmed duplicate became a dispatched pair or lost its command reference");
    pair.cancel();require(f.invalidations==1 && f.caller_releases==1,"Cancellation retried uncertain cleanup");
    complete();require(f.duplicate.references==0 && f.primary.references==1,"Unconfirmed reference crossed to primary");
}

void test_missing_route_before_duplicate_work() {
    for (int missing = 0; missing < 5; ++missing) {
        Fixture f;
        prep::PendingPair pair;
        arm(pair, f);
        natural_prepare(pair, f, false);
        auto incomplete = route();
        if (missing == 0) incomplete.renderer_ready = false;
        if (missing == 1) incomplete.allocate = nullptr;
        if (missing == 2) incomplete.construct = nullptr;
        if (missing == 3) incomplete.dispatch = nullptr;
        if (missing == 4) incomplete.unavailable_command = nullptr;
        require(pair.at_native_command(&f.primary, incomplete, true) ==
            prep::PendingPair::Completion::rejected && f.events == "LXO" &&
            !f.duplicate.prepared && f.duplicate.references == 0 && f.caller_releases == 1,
            "Unavailable route performed duplicate preparation or leaked ownership");
    }
}

void test_reentry_and_nested_ownership() {
    Fixture outer;
    prep::PendingPair parent;
    arm(parent, outer);
    natural_prepare(parent, outer, true);
    {
        Fixture inner;
        {
            prep::PendingPair child;
            arm(child, inner);
        }
        require(inner.caller_releases == 1 && inner.duplicate.references == 0 &&
            outer.caller_releases == 0 && parent.accepts(&outer.primary),
            "Nested producer cancelled or stole the parent's frame");
    }
    current = &outer;
    outer.reenter = &parent;
    require(parent.at_native_command(&outer.primary, route(), true) ==
        prep::PendingPair::Completion::dispatched && outer.tick_count == 1 &&
        outer.caller_releases == 1, "Reentrant native preparation repeated submission");
    complete();
}

void test_concurrent_independent_producers() {
    auto run = [profile=fixture_profile] {
        fixture_profile=profile;
        Fixture f;
        prep::PendingPair pair;
        arm(pair, f);
        natural_prepare(pair, f, true);
        require(pair.at_native_command(&f.primary, route(), true) ==
            prep::PendingPair::Completion::dispatched && f.tick_count == 1,
            "Concurrent producers shared preparation/clock state");
        complete();
    };
    std::thread first{run}, second{run};
    first.join(); second.join();
}

void test_arming_contract() {
    Fixture f;
    current = &f;
    prep::PendingPair pair;
    require(!pair.arm(fixture_profile,nullptr, &f.duplicate, release, invalidate, &f) &&
        !pair.arm(fixture_profile,&f.primary, &f.primary, release, invalidate, &f) &&
        !pair.arm(fixture_profile,&f.primary, &f.duplicate, nullptr, invalidate, &f) &&
        !pair.arm(fixture_profile,&f.primary, &f.duplicate, release, nullptr, &f),
        "Invalid frame ownership accepted");
    require(f.caller_releases == 0 && pair.available(),
        "Rejected arm consumed ownership without accepting it");
    arm(pair, f);
    require(!pair.arm(fixture_profile,&f.primary, &f.duplicate, release, invalidate, &f),
        "One producer accepted duplicate ownership twice");
}

const prep::Profile* other_profile() {
    return fixture_profile==&prep::remastered_500c ? &prep::remastered_1048522 : &prep::remastered_500c;
}
const w3vr::engine_camera::TemporalContract* contract_for(const prep::Profile* profile) {
    return profile==&prep::remastered_500c ? &w3vr::engine_camera::remastered_500c : &w3vr::engine_camera::remastered_1048522;
}

void test_profile_rejection_preserves_ownership() {
    const auto copied=*fixture_profile;
    for(const auto* invalid : {static_cast<const prep::Profile*>(nullptr),&copied}) {
        Fixture f;current=&f;prep::PendingPair pair;
        require(!pair.arm(invalid,&f.primary,&f.duplicate,release,invalidate,&f),
            "Unknown/copied preparation profile acquired ownership");
        require(pair.available() && !pair.pending() && f.events.empty() && f.duplicate.references==1,
            "Rejected profile consumed an unaccepted frame");
    }
    for(int stage=0;stage<4;++stage)for(int wrong=0;wrong<3;++wrong) {
        Fixture f;prep::PendingPair pair;arm(pair,f);
        const auto* invalid=wrong==0?nullptr:wrong==1?&copied:other_profile();
        if(stage==0)pair.observe_engine(invalid,&f.primary,&f.engine,prepare,true);
        else {
            natural_prepare(pair,f,false);
            if(stage==1)pair.observe_effects_tick(invalid);
            if(stage==2){pair.observe_effects_tick(fixture_profile);pair.observe_effects(invalid,&f.primary,&f.effects_argument,apply);}
        }
        auto invalid_route=route();
        auto copied_submission=*fixture_profile->submission;
        if(stage==3)invalid_route.profile=wrong==0?nullptr:wrong==1?&copied_submission:other_profile()->submission;
        require(pair.at_native_command(&f.primary,invalid_route,true)==prep::PendingPair::Completion::rejected,
            "Mixed/copied observation or submission profile accepted");
        pair.cancel();
        require(!f.duplicate.prepared && f.allocations==0 && f.invalidations==1 &&
            f.caller_releases==1 && f.duplicate.references==0 && f.primary.references==1,
            "Rejected mixed profile performed extra Native work or mishandled ownership");
    }
}

void test_selection_boundaries_and_readiness() {
    const auto* profile=fixture_profile;
    const auto* contract=contract_for(profile);
    auto copied_contract=*contract;
    auto copied_profile=*profile;
    auto copied_lifecycle=*profile->lifecycle;
    require(prep::selected(contract)==profile && submit::selected(contract)==profile->submission,
        "Exact contract selected mismatched subsystems");
    require(prep::selected(nullptr)==nullptr && prep::selected(&copied_contract)==nullptr &&
        prep::selected(&w3vr::engine_camera::legacy_404)==nullptr,
        "Unknown/copied/legacy contract adopted preparation profile");
    require(prep::ready_for(contract,profile,profile->lifecycle,true,true),"Matching bundle not ready");
    require(!prep::ready_for(contract,profile,profile->lifecycle,false,true) &&
        !prep::ready_for(contract,profile,profile->lifecycle,true,false) &&
        !prep::ready_for(contract,&copied_profile,profile->lifecycle,true,true) &&
        !prep::ready_for(contract,profile,&copied_lifecycle,true,true) &&
        !prep::ready_for(contract,profile,other_profile()->lifecycle,true,true) &&
        !prep::ready_for(contract,other_profile(),other_profile()->lifecycle,true,true) &&
        !prep::ready_for(&copied_contract,profile,profile->lifecycle,true,true) &&
        !prep::ready_for(nullptr,profile,profile->lifecycle,true,true) &&
        !prep::ready_for(contract,nullptr,profile->lifecycle,true,true),
        "Readiness allowed mixed, copied or incomplete installation");
    const std::array<std::uintptr_t,6> returns{profile->factory_return,profile->prepare_return,
        profile->tick_return,profile->apply_return,profile->construct_return,profile->publication_return};
    const auto* other=other_profile();
    const std::array<std::uintptr_t,6> other_returns{other->factory_return,other->prepare_return,
        other->tick_return,other->apply_return,other->construct_return,other->publication_return};
    for(std::size_t i=0;i<returns.size();++i) {
        const auto boundary=static_cast<prep::Boundary>(i);
        require(prep::allows(profile,boundary,returns[i]),"Natural boundary rejected");
        require(!prep::allows(profile,boundary,returns[i]+1) && !prep::allows(profile,boundary,other_returns[i]) &&
            !prep::allows(&copied_profile,boundary,returns[i]) && !prep::allows(nullptr,boundary,returns[i]),
            "Foreign/copied boundary accepted");
        for(std::size_t j=0;j<returns.size();++j)if(i!=j)
            require(!prep::allows(profile,boundary,returns[j]),"Wrong stage adopted a valid foreign boundary");
    }
    require(!prep::allows(profile,static_cast<prep::Boundary>(99),returns[0]),"Unknown stage accepted");
    for(const auto& hook:profile->hooks) {
        std::vector<std::uint8_t> code(hook.prefix.begin(),hook.prefix.end());
        require(prep::prefix_matches(hook.prefix,code),"Matching complete prefix rejected");
        require(!prep::prefix_matches(hook.prefix,std::span<const std::uint8_t>{code}.first(code.size()-1)),
            "Truncated prefix accepted");
        for(std::size_t i=0;i<code.size();++i) {
            code[i]^=1;require(!prep::prefix_matches(hook.prefix,code),"Changed prefix accepted");code[i]^=1;
        }
    }
    require(!prep::prefix_matches({},{}),"Empty prefix accepted");
}

void test_actual_empty_world_leaf() {
    const auto* profile=fixture_profile;
    constexpr std::uintptr_t module=0x140000000ULL;
    const std::array<std::uint8_t,3> ret_zero{0xC2,0,0};
    const auto callback=module+profile->empty_world;
    require(prep::empty_callback(profile,module,callback,ret_zero),"Exact empty leaf rejected");
    const auto copied=*profile;
    require(!prep::empty_callback(&copied,module,callback,ret_zero) &&
        !prep::empty_callback(nullptr,module,callback,ret_zero) &&
        !prep::empty_callback(other_profile(),module,callback,ret_zero) &&
        !prep::empty_callback(profile,module,callback+1,ret_zero) &&
        !prep::empty_callback(profile,module,callback,std::span<const std::uint8_t>{ret_zero}.first(2)) &&
        !prep::empty_callback(profile,std::numeric_limits<std::uintptr_t>::max(),callback,ret_zero),
        "Foreign, truncated or overflowing callback accepted");
    for(std::size_t i=0;i<ret_zero.size();++i) {
        auto changed=ret_zero;changed[i]^=1;
        require(!prep::empty_callback(profile,module,callback,changed),"Nonempty or changed callback accepted");
    }
}

void test_installation_partial_failure_and_contract_change() {
    const auto* profile=fixture_profile;
    const auto* contract=contract_for(profile);
    for(std::size_t i=0;i<prep::observer_count;++i)
        require(prep::observer_rva(profile,i)==(i<5?profile->hooks[i].rva:profile->submission->dispatch),"mixed observer target");
    require(prep::observer_rva(profile,6)==0 && prep::observer_rva(nullptr,0)==0,"unknown observer target");
    // Each real helper operation can fail or lose its captured contract.
    // Spies model API return values; they do not install executable hooks.
    for(int scenario=0;scenario<26;++scenario) {
        std::array<bool,6> created{},enabled{},disabled{},originals{};
        int create_count{},enable_count{},disable_count{},operation{};
        bool current=true;
        const bool success=prep::install_observers(contract,profile,
            [&](std::size_t i){
                require(i==static_cast<std::size_t>(create_count),"Nonsequential create");
                ++create_count;const int op=operation++;
                if(scenario==op && scenario<12)return false;
                created[i]=true;originals[i]=true;
                if(scenario==12+op)current=false;
                return true;
            },
            [&](std::size_t i){
                require(create_count==6 && i==static_cast<std::size_t>(enable_count),"Enabled before all creates");
                ++enable_count;const int op=operation++;
                if(scenario==op && scenario<12)return false;
                enabled[i]=true;
                if(scenario==12+op)current=false;
                return true;
            },
            [&](std::size_t i){require(enabled[i] && !disabled[i],"Disabled an unenabled/duplicate entry");disabled[i]=true;++disable_count;},
            [&]{return current;});
        require(success==(scenario>=24),"Partial or changed installation published success");
        for(std::size_t i=0;i<6;++i) {
            require(originals[i]==created[i],"Partial failure erased an original forwarder");
            require(disabled[i]==(enabled[i] && !success),"Incomplete rollback admission state");
        }
        require(success?(create_count==6 && enable_count==6 && disable_count==0):disable_count==std::count(enabled.begin(),enabled.end(),true),
            "Partial rollback lost an enabled observer");
    }
    const auto copied=*profile;
    int calls{};
    auto create=[&](std::size_t){++calls;return true;};
    auto disable=[&](std::size_t){++calls;};
    for(const auto* invalid:{static_cast<const prep::Profile*>(nullptr),&copied,other_profile()})
        require(!prep::install_observers(contract,invalid,create,create,disable,[]{return true;}),
            "Unknown/copied/mixed installation ran operations");
    require(!prep::install_observers(contract,profile,create,create,disable,[]{return false;}) && calls==0,
        "Already rejected installation touched hooks");
}

void test_rejected_scopes_mask_restore_and_unwind() {
    int parent{},child{};int* slot=&parent;
    {
        prep::ScopedProducer binding(slot,&child,true);
        require(binding.admitted() && slot==&child,"Admitted child did not bind");
        bool ready=false;
        {
            prep::ScopedProducer rejected(slot,&parent,ready);
            require(!rejected.admitted() && slot==nullptr,"Rejected entry exposed parent state");
            ready=true;
            require(ready && !rejected.admitted() && slot==nullptr,"Rejected entry adopted later readiness");
        }
        require(slot==&child,"Rejected return did not restore child");
        try {
            prep::ScopedProducer rejected(slot,&parent,false);
            require(slot==nullptr,"Rejected exceptional entry exposed parent");
            throw std::runtime_error("fixture");
        }catch(const std::runtime_error&){require(slot==&child,"C++ unwind did not restore child");}
        {
            prep::ScopedProducer missing(slot,static_cast<int*>(nullptr),true);
            require(!missing.admitted() && slot==nullptr,"Null entry adopted readiness");
        }
        require(slot==&child,"Null scope did not restore child");
    }
    require(slot==&parent,"Nested producer did not restore parent");
    try {
        prep::ScopedProducer binding(slot,&child,true);
        throw std::runtime_error("fixture");
    }catch(const std::runtime_error&){require(slot==&parent,"Admitted unwind lost parent");}
}

int main() {
    int profile_checks{};
    for(const auto* profile:{&prep::remastered_500c,&prep::remastered_1048522}) {
        fixture_profile=profile;
        test_natural_clock_and_command_lifetimes();
        test_cancellation_and_missing_stages();
        test_failed_dispatch_and_foreign_calls();
        test_missing_route_before_duplicate_work();
        test_unconfirmed_pair_publication();
        test_reentry_and_nested_ownership();
        test_concurrent_independent_producers();
        test_arming_contract();
        const auto before=checks.load();
        test_profile_rejection_preserves_ownership();
        test_selection_boundaries_and_readiness();
        test_actual_empty_world_leaf();
        test_installation_partial_failure_and_contract_change();
        test_rejected_scopes_mask_restore_and_unwind();
        profile_checks+=checks.load()-before;
    }
    std::cout << checks.load() << " CPU checks; " << profile_checks << " new profile/scope/installation checks. "
        "Both versions preserve one effects tick, ownership and immediate/deferred order. "
        "Spies did not run Native functions, MinHook, SEH, the game, GPU or Quest.\n";
}
