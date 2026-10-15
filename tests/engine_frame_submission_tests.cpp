#include "engine_frame_submission.h"

#include <cstdlib>
#include <iostream>
#include <string>

namespace submission = w3vr::frame_submission;

int checks{};
const submission::Profile* fixture_profile=&submission::remastered_500c;
void require(bool condition, const char* message) {
    ++checks;
    if (!condition) { std::cerr << message << '\n'; std::exit(1); }
}

struct Frame { int references{1}; bool destroyed{}; };
Frame* command_frame{};
int command_storage{};
int unavailable_command{};
enum class Allocation { normal, null, sentinel };
Allocation allocation{};
bool immediate_completion{};
bool publication_confirmed{true};
int owned_releases{};
std::string events;

void drop_reference(Frame& frame) {
    require(!frame.destroyed && frame.references > 0, "Released a destroyed frame");
    if (--frame.references == 0) frame.destroyed = true;
}

void complete_command() {
    require(command_frame != nullptr, "Completed a command without a frame");
    events += 'F';
    drop_reference(*command_frame);
    command_frame = nullptr;
}

void* __fastcall allocate_command() {
    events += 'A';
    if (allocation == Allocation::null) return nullptr;
    if (allocation == Allocation::sentinel) return &unavailable_command;
    return &command_storage;
}

void* __fastcall construct_command(void* destination, void* raw_frame, void* auxiliary) {
    events += 'C';
    require(destination == &command_storage, "Constructor used a foreign allocation");
    require(auxiliary == nullptr, "Duplicate command used an unproved auxiliary object");
    command_frame = static_cast<Frame*>(raw_frame);
    require(!command_frame->destroyed && command_frame->references == 1,
        "Constructor received a dead or unexpectedly retained frame");
    ++command_frame->references;
    return destination;
}

bool __fastcall dispatch_command(void* command,void* frame) {
    events += 'D';
    require(command == &command_storage && command_frame != nullptr,
        "Dispatch did not use the constructed native allocation");
    require(!command_frame->destroyed && command_frame->references == 2,
        "Caller released its reference before dispatch");
    require(frame==command_frame,"Publication adapter changed frame identity");
    if (immediate_completion) complete_command();
    return publication_confirmed;
}

void __fastcall release_owned_frame(void* raw_frame) {
    events += 'R';
    ++owned_releases;
    drop_reference(*static_cast<Frame*>(raw_frame));
}

submission::NativeCommandRoute route() {
    return {allocate_command, construct_command, dispatch_command,
        &unavailable_command, true,fixture_profile};
}

void reset() {
    command_frame = nullptr;
    allocation = Allocation::normal;
    immediate_completion = false;
    publication_confirmed = true;
    owned_releases = 0;
    events.clear();
}

void test_deferred_and_immediate_command_lifetimes() {
    for (bool immediate : {false, true}) {
        reset();
        immediate_completion = immediate;
        Frame frame;
        require(submission::submit_owned_frame(route(), &frame, release_owned_frame) ==
            submission::Result::dispatched, "Native command path rejected");
        require(owned_releases == 1, "Owned factory reference not released exactly once");
        if (immediate) {
            require(events == "ACDFR" && frame.references == 0 && frame.destroyed,
                "Immediate completion caused a leak or premature destruction");
        } else {
            require(events == "ACDR" && frame.references == 1 && !frame.destroyed,
                "Deferred command lost its frame reference");
            complete_command();
            require(frame.references == 0 && frame.destroyed,
                "Deferred command did not release its independent reference");
        }
    }
}

void test_unconfirmed_publication() {
    for(bool immediate:{false,true}) {
        reset();publication_confirmed=false;immediate_completion=immediate;Frame frame;
        require(submission::submit_owned_frame(route(),&frame,release_owned_frame)==submission::Result::publication_unconfirmed,
            "Returned but unconfirmed publication was promoted to dispatched");
        require(owned_releases==1 && events==(immediate?"ACDFR":"ACDR"),
            "Unconfirmed publication retried work or freed command storage");
        if(!immediate){require(frame.references==1 && !frame.destroyed,"Uncertain command lost its own reference");complete_command();}
        require(frame.destroyed && owned_releases==1,"Factory/command references were mixed");
    }
}

void test_missing_native_dependencies() {
    for (int dependency = 0; dependency < 5; ++dependency) {
        reset();
        Frame frame;
        auto missing = route();
        if (dependency == 0) missing.renderer_ready = false;
        if (dependency == 1) missing.allocate = nullptr;
        if (dependency == 2) missing.construct = nullptr;
        if (dependency == 3) missing.dispatch = nullptr;
        if (dependency == 4) missing.unavailable_command = nullptr;
        require(submission::submit_owned_frame(missing, &frame, release_owned_frame) ==
            submission::Result::unavailable, "Incomplete native route accepted");
        require(events == "R" && owned_releases == 1 && frame.destroyed,
            "Unavailable route allocated a command or leaked the owned frame");
    }
}

void test_allocation_rejection() {
    for (auto failure : {Allocation::null, Allocation::sentinel}) {
        reset();
        allocation = failure;
        Frame frame;
        require(submission::submit_owned_frame(route(), &frame, release_owned_frame) ==
            submission::Result::allocation_failed, "Invalid command allocation accepted");
        require(events == "AR" && owned_releases == 1 && frame.destroyed,
            "Failed allocation constructed/dispatched a command or leaked a frame");
        require(command_frame == nullptr, "Sentinel acquired a command reference");
    }
}

void test_invalid_owned_frame_contract() {
    reset();
    Frame frame;
    require(submission::submit_owned_frame(route(), nullptr, release_owned_frame) ==
        submission::Result::invalid_frame, "Missing frame accepted");
    require(submission::submit_owned_frame(route(), &frame, nullptr) ==
        submission::Result::invalid_frame, "Missing release function accepted");
    require(events.empty() && owned_releases == 0 && frame.references == 1,
        "Invalid ownership contract caused native work");
}

void test_profile_selection_and_rejection() {
    const auto* contract=fixture_profile==&submission::remastered_500c ?
        &w3vr::engine_camera::remastered_500c : &w3vr::engine_camera::remastered_1048522;
    auto copied_contract=*contract;
    const auto copied_profile=*fixture_profile;
    require(submission::selected(contract)==fixture_profile && submission::known(fixture_profile),
        "Exact submission contract rejected");
    require(!submission::known(&copied_profile) && !submission::known(nullptr) &&
        submission::selected(&copied_contract)==nullptr && submission::selected(nullptr)==nullptr &&
        submission::selected(&w3vr::engine_camera::legacy_404)==nullptr,
        "Copied/unknown/legacy contract silently selected submission");
    for(const auto* invalid:{static_cast<const submission::Profile*>(nullptr),&copied_profile}) {
        reset();Frame frame;auto invalid_route=route();invalid_route.profile=invalid;
        require(submission::submit_owned_frame(invalid_route,&frame,release_owned_frame)==submission::Result::unavailable,
            "Unknown/copied route accepted");
        require(events=="R" && owned_releases==1 && frame.destroyed && command_frame==nullptr,
            "Rejected route allocated or mishandled its owned frame");
    }
}
int main() {
    int profile_checks{};
    for(const auto* profile:{&submission::remastered_500c,&submission::remastered_1048522}) {
        fixture_profile=profile;
        test_deferred_and_immediate_command_lifetimes();
        test_missing_native_dependencies();
        test_unconfirmed_publication();
        test_allocation_rejection();
        test_invalid_owned_frame_contract();
        const auto before=checks;
        test_profile_selection_and_rejection();
        profile_checks+=checks-before;
    }
    std::cout << checks << " CPU checks; " << profile_checks << " new profile checks. "
        "Both versions preserve native sentinel and immediate/deferred ownership. "
        "Simulated functions did not execute Native code, the game, GPU or Quest.\n";
}
