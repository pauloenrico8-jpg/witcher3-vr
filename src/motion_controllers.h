#pragma once

#include <openxr/openxr.h>
#include <array>
#include <mutex>

namespace w3vr::motion {

struct HandState {
    XrPosef grip{{0, 0, 0, 1}, {0, 0, 0}};
    XrPosef aim{{0, 0, 0, 1}, {0, 0, 0}};
    XrVector3f linear_velocity{};
    XrVector3f angular_velocity{};
    XrVector2f stick{};
    float trigger{};
    float squeeze{};
    bool primary{};
    bool secondary{};
    bool grip_tracked{};
    bool aim_tracked{};
    bool linear_velocity_valid{};
    bool angular_velocity_valid{};
};

struct Frame {
    XrTime display_time{};
    std::array<HandState, 2> hands{}; // left, right; metres in LOCAL space
    bool focused{};
    bool ready{};
    XrResult result{XR_SUCCESS};
};

// Owned by the existing renderer's OpenXR session. No second XR session,
// keyboard emulation, gamepad emulation, or damage injection is involved.
class Controllers {
public:
    XrResult initialize(XrInstance instance, XrSession session,
                        PFN_xrGetInstanceProcAddr get_proc);
    void update(XrSpace base_space, XrTime display_time, bool focused);
    void invalidate();
    void shutdown(); // before destroying the owning XR session/instance
    Frame snapshot() const;
    XrResult vibrate(unsigned hand, float amplitude, XrDuration duration);

private:
    template<class T> bool load(const char* name, T& destination);
    XrResult create_action(const char* name, const char* label,
                           XrActionType type, XrAction& action);
    void clear_resources();

    XrInstance instance_{XR_NULL_HANDLE};
    XrSession session_{XR_NULL_HANDLE};
    XrActionSet action_set_{XR_NULL_HANDLE};
    XrAction grip_{}, aim_{}, trigger_{}, squeeze_{}, stick_{};
    XrAction primary_{}, secondary_{}, haptic_{};
    std::array<XrPath, 2> hand_paths_{};
    std::array<XrSpace, 2> grip_spaces_{}, aim_spaces_{};
    bool attached_{};
    mutable std::mutex mutex_;
    Frame frame_{};

    PFN_xrGetInstanceProcAddr get_proc_{};
    PFN_xrStringToPath string_to_path_{};
    PFN_xrCreateActionSet create_action_set_{};
    PFN_xrDestroyActionSet destroy_action_set_{};
    PFN_xrCreateAction create_action_{};
    PFN_xrSuggestInteractionProfileBindings suggest_bindings_{};
    PFN_xrAttachSessionActionSets attach_sets_{};
    PFN_xrCreateActionSpace create_space_{};
    PFN_xrDestroySpace destroy_space_{};
    PFN_xrSyncActions sync_{};
    PFN_xrGetActionStatePose get_pose_{};
    PFN_xrGetActionStateFloat get_float_{};
    PFN_xrGetActionStateVector2f get_vector_{};
    PFN_xrGetActionStateBoolean get_bool_{};
    PFN_xrLocateSpace locate_{};
    PFN_xrApplyHapticFeedback apply_haptic_{};
};

// Positions are deliberately not extrapolated when tracking drops out.
bool usable_pose(const XrSpaceLocation& location);

} // namespace w3vr::motion
