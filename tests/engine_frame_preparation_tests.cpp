#include "engine_frame_preparation.h"

#include <cstdlib>
#include <iostream>
#include <string>
#include <thread>

namespace prep = w3vr::frame_preparation;
namespace submit = w3vr::frame_submission;

void require(bool condition, const char* message) {
    if (!condition) { std::cerr << message << '\n'; std::exit(1); }
}

struct Frame {
    int references{1};
    bool prepared{};
    bool effects_applied{};
    float effects_time{};
};

struct Fixture {
    Frame primary{}, duplicate{};
    int engine{}, effects_argument{}, command{}, sentinel{};
    Frame* queued{};
    float effects_time{};
    int tick_count{}, caller_releases{}, invalidations{}, allocations{};
    std::string events;
    bool allocation_failure{}, immediate_completion{};
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

void __fastcall dispatch(void* command) {
    require(command == &current->command && current->queued != nullptr,
        "Dispatched an unconstructed command");
    current->events += 'D';
    if (current->immediate_completion) complete();
}

submit::NativeCommandRoute route() {
    return {allocate, construct, dispatch, &current->sentinel, true};
}

void arm(prep::PendingPair& pair, Fixture& f) {
    current = &f;
    require(pair.arm(&f.primary, &f.duplicate, release, invalidate, &f), "Could not arm pair");
}

void natural_prepare(prep::PendingPair& pair, Fixture& f, bool effects) {
    prepare(&f.engine, &f.primary);
    pair.observe_engine(&f.primary, &f.engine, prepare, true);
    if (effects) {
        tick_once(0.016f);
        pair.observe_effects_tick();
        apply(&f.effects_argument, &f.primary);
        pair.observe_effects(&f.primary, &f.effects_argument, apply);
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
            if (reason == 2) pair.observe_effects_tick(); // Tick without apply.
            if (reason == 3) pair.observe_effects(&f.primary, &f.effects_argument, apply);
            if (reason == 4) pair.observe_engine(&f.primary, &f.engine, prepare, true);
            if (reason == 5) {
                pair.observe_effects_tick(); pair.observe_effects_tick();
            }
            if (reason == 6) {
                pair.observe_effects_tick();
                pair.observe_effects(&f.primary, &f.effects_argument, apply);
                pair.observe_effects(&f.primary, &f.effects_argument, apply);
            }
            if (reason == 7) pair.observe_engine(&f.primary, &f.engine, prepare, false);
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
    pair.observe_engine(&foreign, &f.engine, prepare, true);
    pair.observe_effects(&foreign, &f.effects_argument, apply);
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
    auto run = [] {
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
    require(!pair.arm(nullptr, &f.duplicate, release, invalidate, &f) &&
        !pair.arm(&f.primary, &f.primary, release, invalidate, &f) &&
        !pair.arm(&f.primary, &f.duplicate, nullptr, invalidate, &f) &&
        !pair.arm(&f.primary, &f.duplicate, release, nullptr, &f),
        "Invalid frame ownership accepted");
    require(f.caller_releases == 0 && pair.available(),
        "Rejected arm consumed ownership without accepting it");
    arm(pair, f);
    require(!pair.arm(&f.primary, &f.duplicate, release, invalidate, &f),
        "One producer accepted duplicate ownership twice");
}

int main() {
    test_natural_clock_and_command_lifetimes();
    test_cancellation_and_missing_stages();
    test_failed_dispatch_and_foreign_calls();
    test_missing_route_before_duplicate_work();
    test_reentry_and_nested_ownership();
    test_concurrent_independent_producers();
    test_arming_contract();
    std::cout << "Deferred native preparation, one shared effects tick, cancellation, "
        "nested/reentrant ownership and immediate/deferred commands passed in simulation.\n";
}
