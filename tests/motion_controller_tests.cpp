#include "motion_controllers.h"
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <map>
#include <string>

namespace {
void require(bool condition, const char* description) {
    if (!condition) { std::fprintf(stderr, "FAIL: %s\n", description); std::exit(1); }
}
template<class T> T handle(unsigned value) { return reinterpret_cast<T>(static_cast<uintptr_t>(value)); }
std::map<std::string, XrAction> actions;
std::map<XrSpace, XrPath> spaces;
unsigned next_handle = 10, spaces_destroyed = 0, sets_destroyed = 0, sync_calls = 0;
bool active = true, tracked = true, missing_proc = false, invalid_velocity = false;
XrResult sync_result = XR_SUCCESS, locate_result = XR_SUCCESS, attach_result = XR_SUCCESS;

XrResult XRAPI_CALL string_to_path(XrInstance, const char* name, XrPath* out) {
    *out = std::strcmp(name, "/user/hand/left") == 0 ? 1 :
           std::strcmp(name, "/user/hand/right") == 0 ? 2 : next_handle++;
    return XR_SUCCESS;
}
XrResult XRAPI_CALL create_set(XrInstance, const XrActionSetCreateInfo*, XrActionSet* out) {
    *out = handle<XrActionSet>(next_handle++); return XR_SUCCESS;
}
XrResult XRAPI_CALL destroy_set(XrActionSet) { ++sets_destroyed; return XR_SUCCESS; }
XrResult XRAPI_CALL create_action(XrActionSet, const XrActionCreateInfo* info, XrAction* out) {
    require(info->countSubactionPaths == 2, "Every action has two hands");
    *out = handle<XrAction>(next_handle++); actions[info->actionName] = *out; return XR_SUCCESS;
}
XrResult XRAPI_CALL suggest(XrInstance, const XrInteractionProfileSuggestedBinding* info) {
    require(info->countSuggestedBindings == 16, "Touch binds both poses, axes, buttons and haptics");
    return XR_SUCCESS;
}
XrResult XRAPI_CALL attach(XrSession, const XrSessionActionSetsAttachInfo* info) {
    require(info->countActionSets == 1, "Only one action set attached"); return attach_result;
}
XrResult XRAPI_CALL create_space(XrSession, const XrActionSpaceCreateInfo* info, XrSpace* out) {
    require(info->poseInActionSpace.orientation.w == 1.0f, "Identity action-space orientation");
    *out = handle<XrSpace>(next_handle++); spaces[*out] = info->subactionPath; return XR_SUCCESS;
}
XrResult XRAPI_CALL destroy_space(XrSpace) { ++spaces_destroyed; return XR_SUCCESS; }
XrResult XRAPI_CALL sync(XrSession, const XrActionsSyncInfo*) { ++sync_calls; return sync_result; }
XrResult XRAPI_CALL get_pose(XrSession, const XrActionStateGetInfo*, XrActionStatePose* out) {
    out->isActive = active; return XR_SUCCESS;
}
XrResult XRAPI_CALL get_float(XrSession, const XrActionStateGetInfo*, XrActionStateFloat* out) {
    out->isActive = active; out->currentState = 0.75f; return XR_SUCCESS;
}
XrResult XRAPI_CALL get_vector(XrSession, const XrActionStateGetInfo*, XrActionStateVector2f* out) {
    out->isActive = active; out->currentState = {0.4f, -0.5f}; return XR_SUCCESS;
}
XrResult XRAPI_CALL get_bool(XrSession, const XrActionStateGetInfo*, XrActionStateBoolean* out) {
    out->isActive = active; out->currentState = true; return XR_SUCCESS;
}
XrResult XRAPI_CALL locate(XrSpace space, XrSpace, XrTime, XrSpaceLocation* out) {
    out->locationFlags = XR_SPACE_LOCATION_POSITION_VALID_BIT | XR_SPACE_LOCATION_ORIENTATION_VALID_BIT;
    if (tracked) out->locationFlags |= XR_SPACE_LOCATION_POSITION_TRACKED_BIT | XR_SPACE_LOCATION_ORIENTATION_TRACKED_BIT;
    out->pose = {{0, 0, 0, 1}, {spaces[space] == 1 ? -0.3f : 0.3f, 1.2f, -0.4f}};
    if (out->next) {
        auto* velocity = static_cast<XrSpaceVelocity*>(out->next);
        velocity->velocityFlags = XR_SPACE_VELOCITY_LINEAR_VALID_BIT | XR_SPACE_VELOCITY_ANGULAR_VALID_BIT;
        velocity->linearVelocity = {invalid_velocity ? NAN : 1.5f, 0, 0};
        velocity->angularVelocity = {0, 0.2f, 0};
    }
    return locate_result;
}
XrResult XRAPI_CALL haptic(XrSession, const XrHapticActionInfo*, const XrHapticBaseHeader*) { return XR_SUCCESS; }
XrResult XRAPI_CALL proc(XrInstance, const char* name, PFN_xrVoidFunction* out) {
    if (missing_proc) { *out = nullptr; return XR_ERROR_FUNCTION_UNSUPPORTED; }
#define PROC(api, function) if (std::strcmp(name, api) == 0) { *out = reinterpret_cast<PFN_xrVoidFunction>(function); return XR_SUCCESS; }
    PROC("xrStringToPath", string_to_path)
    PROC("xrCreateActionSet", create_set)
    PROC("xrDestroyActionSet", destroy_set)
    PROC("xrCreateAction", create_action)
    PROC("xrSuggestInteractionProfileBindings", suggest)
    PROC("xrAttachSessionActionSets", attach)
    PROC("xrCreateActionSpace", create_space)
    PROC("xrDestroySpace", destroy_space)
    PROC("xrSyncActions", sync)
    PROC("xrGetActionStatePose", get_pose)
    PROC("xrGetActionStateFloat", get_float)
    PROC("xrGetActionStateVector2f", get_vector)
    PROC("xrGetActionStateBoolean", get_bool)
    PROC("xrLocateSpace", locate)
    PROC("xrApplyHapticFeedback", haptic)
#undef PROC
    *out = nullptr; return XR_ERROR_FUNCTION_UNSUPPORTED;
}
}

int main() {
    using w3vr::motion::Controllers;
    Controllers controllers;
    require(controllers.initialize(handle<XrInstance>(1), handle<XrSession>(2), proc) == XR_SUCCESS, "Initialize actions");
    require(controllers.initialize(handle<XrInstance>(1), handle<XrSession>(2), proc) == XR_ERROR_ACTIONSETS_ALREADY_ATTACHED, "No double attach");
    controllers.update(handle<XrSpace>(3), 1000000000, true);
    auto frame = controllers.snapshot();
    require(frame.ready && frame.focused && frame.result == XR_SUCCESS, "Focused frame is ready");
    require(frame.hands[0].grip_tracked && frame.hands[1].aim_tracked, "Both hands tracked");
    require(frame.hands[0].grip.position.x < 0 && frame.hands[1].grip.position.x > 0, "Hands retain independent poses");
    require(frame.hands[1].trigger == 0.75f && frame.hands[1].primary, "Analog and buttons read");
    require(frame.hands[1].linear_velocity_valid, "Runtime velocity retained");
    require(controllers.vibrate(1, 0.4f, 20000000) == XR_SUCCESS, "Bounded vibration");
    require(controllers.vibrate(1, NAN, 20000000) == XR_ERROR_VALIDATION_FAILURE, "Invalid vibration rejected");
    require(controllers.vibrate(1, 0.4f, 200000000) == XR_ERROR_VALIDATION_FAILURE, "Long vibration rejected");
    controllers.update(handle<XrSpace>(3), 1010000000, false);
    frame = controllers.snapshot();
    require(!frame.focused && !frame.hands[1].grip_tracked && frame.hands[1].trigger == 0 && !frame.hands[1].primary, "Focus loss clears pose and held input");
    require(sync_calls == 1, "No sync while unfocused");
    sync_result = XR_SESSION_NOT_FOCUSED;
    controllers.update(handle<XrSpace>(3), 1020000000, true);
    require(!controllers.snapshot().focused && !controllers.snapshot().hands[0].grip_tracked, "Positive not-focused result cannot authorize input");
    sync_result = XR_SUCCESS;
    active = false;
    controllers.update(handle<XrSpace>(3), 1030000000, true);
    require(!controllers.snapshot().hands[1].grip_tracked && !controllers.snapshot().hands[1].primary, "Inactive action clears input");
    active = true; tracked = false;
    controllers.update(handle<XrSpace>(3), 1040000000, true);
    require(!controllers.snapshot().hands[1].grip_tracked && controllers.snapshot().hands[1].trigger == 0, "Inferred position does not count as tracked");
    tracked = true; invalid_velocity = true;
    controllers.update(handle<XrSpace>(3), 1050000000, true);
    require(!controllers.snapshot().hands[1].linear_velocity_valid, "NaN velocity rejected");
    invalid_velocity = false; locate_result = XR_ERROR_RUNTIME_FAILURE;
    controllers.update(handle<XrSpace>(3), 1060000000, true);
    require(controllers.snapshot().result == XR_ERROR_RUNTIME_FAILURE && !controllers.snapshot().hands[1].grip_tracked, "Runtime failure clears publication");
    locate_result = XR_SUCCESS;
    controllers.shutdown();
    require(spaces_destroyed == 4 && sets_destroyed == 1 && !controllers.snapshot().ready, "Spaces and action set cleaned up");
    controllers.shutdown();
    require(sets_destroyed == 1, "Repeated shutdown is harmless");
    Controllers failed;
    attach_result = XR_ERROR_RUNTIME_FAILURE;
    require(failed.initialize(handle<XrInstance>(1), handle<XrSession>(2), proc) == XR_ERROR_RUNTIME_FAILURE, "Failed attach reported");
    require(spaces_destroyed == 8 && sets_destroyed == 2 && !failed.snapshot().ready, "Failed initialization cleans up created handles");
    missing_proc = true;
    require(failed.initialize(handle<XrInstance>(1), handle<XrSession>(2), proc) == XR_ERROR_FUNCTION_UNSUPPORTED, "Missing XR function reported");
    std::puts("Motion controller lifecycle tests passed (simulated runtime; no headset validation). ");
}
