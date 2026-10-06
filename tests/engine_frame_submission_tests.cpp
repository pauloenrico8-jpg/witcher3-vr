#include "engine_frame_submission.h"

#include <cstdlib>
#include <iostream>
#include <string>

namespace submission = w3vr::frame_submission;

void require(bool condition, const char* message) {
    if (!condition) { std::cerr << message << '\n'; std::exit(1); }
}

struct Frame { int references{1}; bool destroyed{}; };
Frame* command_frame{};
int command_storage{};
int unavailable_command{};
enum class Allocation { normal, null, sentinel };
Allocation allocation{};
bool immediate_completion{};
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

void __fastcall dispatch_command(void* command) {
    events += 'D';
    require(command == &command_storage && command_frame != nullptr,
        "Dispatch did not use the constructed native allocation");
    require(!command_frame->destroyed && command_frame->references == 2,
        "Caller released its reference before dispatch");
    if (immediate_completion) complete_command();
}

void __fastcall release_owned_frame(void* raw_frame) {
    events += 'R';
    ++owned_releases;
    drop_reference(*static_cast<Frame*>(raw_frame));
}

submission::NativeCommandRoute route() {
    return {allocate_command, construct_command, dispatch_command,
        &unavailable_command, true};
}

void reset() {
    command_frame = nullptr;
    allocation = Allocation::normal;
    immediate_completion = false;
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

int main() {
    test_deferred_and_immediate_command_lifetimes();
    test_missing_native_dependencies();
    test_allocation_rejection();
    test_invalid_owned_frame_contract();
    std::cout << "Frame command ownership, native sentinel and immediate/deferred "
        "completion passed using simulated native functions only.\n";
}
