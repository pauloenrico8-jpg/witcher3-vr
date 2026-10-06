#include "motion_controllers.h"
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <vector>

namespace w3vr::motion {
namespace {
bool finite_vector(XrVector3f v) {
    return std::isfinite(v.x) && std::isfinite(v.y) && std::isfinite(v.z);
}
float analog(float value) {
    return std::isfinite(value) ? std::clamp(value, 0.0f, 1.0f) : 0.0f;
}
}

bool usable_pose(const XrSpaceLocation& location) {
    constexpr XrSpaceLocationFlags required =
        XR_SPACE_LOCATION_POSITION_VALID_BIT |
        XR_SPACE_LOCATION_ORIENTATION_VALID_BIT |
        XR_SPACE_LOCATION_POSITION_TRACKED_BIT |
        XR_SPACE_LOCATION_ORIENTATION_TRACKED_BIT;
    const auto q = location.pose.orientation;
    const float norm = q.x*q.x + q.y*q.y + q.z*q.z + q.w*q.w;
    return (location.locationFlags & required) == required &&
        finite_vector(location.pose.position) &&
        std::isfinite(norm) && std::abs(norm - 1.0f) < 0.05f;
}

template<class T>
bool Controllers::load(const char* name, T& destination) {
    PFN_xrVoidFunction proc{};
    const auto result = get_proc_(instance_, name, &proc);
    destination = reinterpret_cast<T>(proc);
    return XR_SUCCEEDED(result) && destination;
}

XrResult Controllers::create_action(const char* name, const char* label,
                                   XrActionType type, XrAction& action) {
    XrActionCreateInfo info{XR_TYPE_ACTION_CREATE_INFO};
    std::snprintf(info.actionName, sizeof(info.actionName), "%s", name);
    std::snprintf(info.localizedActionName, sizeof(info.localizedActionName), "%s", label);
    info.actionType = type;
    info.countSubactionPaths = 2;
    info.subactionPaths = hand_paths_.data();
    return create_action_(action_set_, &info, &action);
}

XrResult Controllers::initialize(XrInstance instance, XrSession session,
                                PFN_xrGetInstanceProcAddr get_proc) {
    std::lock_guard lock(mutex_);
    // Action sets can only be attached once in the lifetime of a session.
    if (attached_) return XR_ERROR_ACTIONSETS_ALREADY_ATTACHED;
    if (instance == XR_NULL_HANDLE || session == XR_NULL_HANDLE || !get_proc)
        return XR_ERROR_HANDLE_INVALID;
    instance_ = instance;
    session_ = session;
    get_proc_ = get_proc;
#define LOAD(member, name) if (!load(name, member)) return XR_ERROR_FUNCTION_UNSUPPORTED
    LOAD(string_to_path_, "xrStringToPath");
    LOAD(create_action_set_, "xrCreateActionSet");
    LOAD(destroy_action_set_, "xrDestroyActionSet");
    LOAD(create_action_, "xrCreateAction");
    LOAD(suggest_bindings_, "xrSuggestInteractionProfileBindings");
    LOAD(attach_sets_, "xrAttachSessionActionSets");
    LOAD(create_space_, "xrCreateActionSpace");
    LOAD(destroy_space_, "xrDestroySpace");
    LOAD(sync_, "xrSyncActions");
    LOAD(get_pose_, "xrGetActionStatePose");
    LOAD(get_float_, "xrGetActionStateFloat");
    LOAD(get_vector_, "xrGetActionStateVector2f");
    LOAD(get_bool_, "xrGetActionStateBoolean");
    LOAD(locate_, "xrLocateSpace");
    LOAD(apply_haptic_, "xrApplyHapticFeedback");
#undef LOAD
    auto checked = [this](XrResult result) {
        if (XR_FAILED(result)) clear_resources();
        return result;
    };
    XrResult result = string_to_path_(instance_, "/user/hand/left", &hand_paths_[0]);
    if (XR_FAILED(result)) return checked(result);
    result = string_to_path_(instance_, "/user/hand/right", &hand_paths_[1]);
    if (XR_FAILED(result)) return checked(result);
    XrActionSetCreateInfo set_info{XR_TYPE_ACTION_SET_CREATE_INFO};
    std::snprintf(set_info.actionSetName, sizeof(set_info.actionSetName), "w3vr_motion");
    std::snprintf(set_info.localizedActionSetName, sizeof(set_info.localizedActionSetName), "Witcher VR hands");
    result = create_action_set_(instance_, &set_info, &action_set_);
    if (XR_FAILED(result)) return checked(result);
    struct Definition { const char* name; const char* label; XrActionType type; XrAction* handle; };
    const Definition definitions[] = {
        {"grip_pose", "Hand position", XR_ACTION_TYPE_POSE_INPUT, &grip_},
        {"aim_pose", "Hand aiming", XR_ACTION_TYPE_POSE_INPUT, &aim_},
        {"trigger", "Index trigger", XR_ACTION_TYPE_FLOAT_INPUT, &trigger_},
        {"squeeze", "Grab", XR_ACTION_TYPE_FLOAT_INPUT, &squeeze_},
        {"stick", "Thumbstick", XR_ACTION_TYPE_VECTOR2F_INPUT, &stick_},
        {"primary", "Primary button", XR_ACTION_TYPE_BOOLEAN_INPUT, &primary_},
        {"secondary", "Secondary button", XR_ACTION_TYPE_BOOLEAN_INPUT, &secondary_},
        {"haptic", "Hand vibration", XR_ACTION_TYPE_VIBRATION_OUTPUT, &haptic_}
    };
    for (const auto& d : definitions) {
        result = create_action(d.name, d.label, d.type, *d.handle);
        if (XR_FAILED(result)) return checked(result);
    }
    // This core Touch profile is available through SteamVR, Meta Link and
    // VDXR without requesting vendor extensions. Actual binding is runtime-owned.
    XrPath profile{};
    result = string_to_path_(instance_, "/interaction_profiles/oculus/touch_controller", &profile);
    if (XR_FAILED(result)) return checked(result);
    std::vector<XrActionSuggestedBinding> bindings;
    for (unsigned hand = 0; hand < 2; ++hand) {
        const char* prefix = hand == 0 ? "/user/hand/left" : "/user/hand/right";
        const struct { XrAction action; const char* suffix; } inputs[] = {
            {grip_, "/input/grip/pose"}, {aim_, "/input/aim/pose"},
            {trigger_, "/input/trigger/value"}, {squeeze_, "/input/squeeze/value"},
            {stick_, "/input/thumbstick"}, {haptic_, "/output/haptic"},
            {primary_, hand == 0 ? "/input/x/click" : "/input/a/click"},
            {secondary_, hand == 0 ? "/input/y/click" : "/input/b/click"}
        };
        for (const auto& input : inputs) {
            char path_name[128];
            std::snprintf(path_name, sizeof(path_name), "%s%s", prefix, input.suffix);
            XrPath path{};
            result = string_to_path_(instance_, path_name, &path);
            if (XR_FAILED(result)) return checked(result);
            bindings.push_back({input.action, path});
        }
    }
    XrInteractionProfileSuggestedBinding suggestion{XR_TYPE_INTERACTION_PROFILE_SUGGESTED_BINDING};
    suggestion.interactionProfile = profile;
    suggestion.countSuggestedBindings = static_cast<uint32_t>(bindings.size());
    suggestion.suggestedBindings = bindings.data();
    result = suggest_bindings_(instance_, &suggestion);
    if (XR_FAILED(result)) return checked(result);
    for (unsigned hand = 0; hand < 2; ++hand) {
        XrActionSpaceCreateInfo info{XR_TYPE_ACTION_SPACE_CREATE_INFO};
        info.subactionPath = hand_paths_[hand];
        info.poseInActionSpace.orientation.w = 1.0f;
        info.action = grip_;
        result = create_space_(session_, &info, &grip_spaces_[hand]);
        if (XR_FAILED(result)) return checked(result);
        info.action = aim_;
        result = create_space_(session_, &info, &aim_spaces_[hand]);
        if (XR_FAILED(result)) return checked(result);
    }
    XrSessionActionSetsAttachInfo attach{XR_TYPE_SESSION_ACTION_SETS_ATTACH_INFO};
    attach.countActionSets = 1;
    attach.actionSets = &action_set_;
    result = attach_sets_(session_, &attach);
    if (XR_FAILED(result)) return checked(result);
    attached_ = true;
    frame_ = {};
    frame_.ready = true;
    return result;
}

void Controllers::update(XrSpace base_space, XrTime time, bool focused) {
    std::lock_guard lock(mutex_);
    frame_ = {};
    frame_.display_time = time;
    frame_.ready = attached_;
    frame_.focused = attached_ && focused;
    if (!attached_ || !focused || base_space == XR_NULL_HANDLE || time <= 0) return;
    XrActiveActionSet active{action_set_, XR_NULL_PATH};
    XrActionsSyncInfo sync_info{XR_TYPE_ACTIONS_SYNC_INFO};
    sync_info.countActiveActionSets = 1;
    sync_info.activeActionSets = &active;
    frame_.result = sync_(session_, &sync_info);
    // XR_SESSION_NOT_FOCUSED is a positive result, but is not usable input.
    if (frame_.result != XR_SUCCESS) { frame_.focused = false; return; }
    for (unsigned hand = 0; hand < 2; ++hand) {
        auto& output = frame_.hands[hand];
        XrActionStateGetInfo query{XR_TYPE_ACTION_STATE_GET_INFO};
        query.subactionPath = hand_paths_[hand];
        auto record = [this](XrResult result) {
            if (XR_FAILED(result) && frame_.result == XR_SUCCESS) frame_.result = result;
            return result == XR_SUCCESS;
        };
        auto pose = [&](XrAction action, XrSpace space, XrPosef& dst, bool& tracked, bool velocity) {
            query.action = action;
            XrActionStatePose state{XR_TYPE_ACTION_STATE_POSE};
            if (!record(get_pose_(session_, &query, &state)) || !state.isActive) return;
            XrSpaceVelocity speeds{XR_TYPE_SPACE_VELOCITY};
            XrSpaceLocation location{XR_TYPE_SPACE_LOCATION};
            location.next = velocity ? &speeds : nullptr;
            if (!record(locate_(space, base_space, time, &location)) || !usable_pose(location)) return;
            dst = location.pose;
            tracked = true;
            if (velocity && (speeds.velocityFlags & XR_SPACE_VELOCITY_LINEAR_VALID_BIT) && finite_vector(speeds.linearVelocity)) {
                output.linear_velocity = speeds.linearVelocity;
                output.linear_velocity_valid = true;
            }
            if (velocity && (speeds.velocityFlags & XR_SPACE_VELOCITY_ANGULAR_VALID_BIT) && finite_vector(speeds.angularVelocity)) {
                output.angular_velocity = speeds.angularVelocity;
                output.angular_velocity_valid = true;
            }
        };
        pose(grip_, grip_spaces_[hand], output.grip, output.grip_tracked, true);
        pose(aim_, aim_spaces_[hand], output.aim, output.aim_tracked, false);
        // A disconnected or inferred hand must not retain pressed controls.
        if (!output.grip_tracked) continue;
        auto scalar = [&](XrAction action) {
            query.action = action;
            XrActionStateFloat value{XR_TYPE_ACTION_STATE_FLOAT};
            return record(get_float_(session_, &query, &value)) && value.isActive ? analog(value.currentState) : 0.0f;
        };
        output.trigger = scalar(trigger_);
        output.squeeze = scalar(squeeze_);
        query.action = stick_;
        XrActionStateVector2f stick{XR_TYPE_ACTION_STATE_VECTOR2F};
        if (record(get_vector_(session_, &query, &stick)) && stick.isActive &&
            std::isfinite(stick.currentState.x) && std::isfinite(stick.currentState.y)) {
            output.stick = {std::clamp(stick.currentState.x, -1.0f, 1.0f),
                            std::clamp(stick.currentState.y, -1.0f, 1.0f)};
        }
        auto button = [&](XrAction action) {
            query.action = action;
            XrActionStateBoolean value{XR_TYPE_ACTION_STATE_BOOLEAN};
            return record(get_bool_(session_, &query, &value)) && value.isActive && value.currentState;
        };
        output.primary = button(primary_);
        output.secondary = button(secondary_);
    }
    // A failed action query invalidates the whole publication instead of
    // mixing samples from a partially failing runtime.
    if (frame_.result != XR_SUCCESS) frame_.hands = {};
}

void Controllers::invalidate() {
    std::lock_guard lock(mutex_);
    frame_ = {};
    frame_.ready = attached_;
}

Frame Controllers::snapshot() const {
    std::lock_guard lock(mutex_);
    return frame_;
}

XrResult Controllers::vibrate(unsigned hand, float amplitude, XrDuration duration) {
    std::lock_guard lock(mutex_);
    if (!attached_ || !frame_.focused || hand > 1 || !frame_.hands[hand].grip_tracked)
        return XR_SESSION_NOT_FOCUSED;
    if (!std::isfinite(amplitude) || duration <= 0 || duration > 100000000)
        return XR_ERROR_VALIDATION_FAILURE;
    XrHapticActionInfo info{XR_TYPE_HAPTIC_ACTION_INFO};
    info.action = haptic_;
    info.subactionPath = hand_paths_[hand];
    XrHapticVibration vibration{XR_TYPE_HAPTIC_VIBRATION};
    vibration.amplitude = analog(amplitude);
    vibration.duration = duration;
    vibration.frequency = XR_FREQUENCY_UNSPECIFIED;
    return apply_haptic_(session_, &info, reinterpret_cast<const XrHapticBaseHeader*>(&vibration));
}

void Controllers::clear_resources() {
    for (auto& space : grip_spaces_) {
        if (space && destroy_space_) destroy_space_(space);
        space = XR_NULL_HANDLE;
    }
    for (auto& space : aim_spaces_) {
        if (space && destroy_space_) destroy_space_(space);
        space = XR_NULL_HANDLE;
    }
    if (action_set_ && destroy_action_set_) destroy_action_set_(action_set_);
    action_set_ = XR_NULL_HANDLE;
    grip_ = aim_ = trigger_ = squeeze_ = stick_ = primary_ = secondary_ = haptic_ = XR_NULL_HANDLE;
    frame_ = {};
}

void Controllers::shutdown() {
    std::lock_guard lock(mutex_);
    clear_resources();
    attached_ = false;
    instance_ = XR_NULL_HANDLE;
    session_ = XR_NULL_HANDLE;
}

} // namespace w3vr::motion
