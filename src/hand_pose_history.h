#pragma once

#include "motion_controllers.h"
#include "rigid_sword_sweep.h"
#include <mutex>

namespace w3vr::motion {

struct HandPath {
    combat::RigidPose previous{}, current{};
    double seconds{};
    bool tracked{}, continuous{};
};
struct HandPaths {
    XrTime display_time{};
    uint64_t tracking_epoch{};
    XrSpace reference_space{XR_NULL_HANDLE};
    std::array<HandPath,2> hands{};
};

// Grip paths in OpenXR LOCAL metres, not REDengine world coordinates. A game
// adapter must map the whole path with one calibrated transform and reset on
// teleport, camera-anchor or weapon changes before querying enemy geometry.
class HandPoseHistory {
public:
    HandPaths update(const Frame& frame) {
        std::lock_guard lock(mutex_);
        if (frame.tracking_epoch != published_.tracking_epoch ||
            frame.reference_space != published_.reference_space)
            previous_valid_ = {};
        const bool usable = frame.ready && frame.focused && frame.result == XR_SUCCESS &&
                            frame.reference_space != XR_NULL_HANDLE && frame.display_time > 0;
        HandPaths next;
        next.display_time = frame.display_time;
        next.tracking_epoch = frame.tracking_epoch;
        next.reference_space = frame.reference_space;
        for (unsigned i=0; i<2; ++i) {
            const auto& hand = frame.hands[i];
            const auto& q = hand.grip.orientation;
            combat::RigidPose pose{{hand.grip.position.x,hand.grip.position.y,hand.grip.position.z},
                                  {q.x,q.y,q.z,q.w}};
            combat::Quaternion normalized;
            if (!usable || !hand.grip_tracked || !combat::detail::finite(pose.position) ||
                !combat::rigid_detail::normalize(pose.orientation,normalized)) {
                previous_valid_[i] = false;
                continue;
            }
            pose.orientation = normalized;
            auto& path = next.hands[i];
            path.tracked = true;
            path.current = pose;
            if (previous_valid_[i] && frame.display_time > previous_times_[i]) {
                // Divide separately to avoid signed overflow on invalid timestamps.
                const double dt = (static_cast<double>(frame.display_time) -
                                   static_cast<double>(previous_times_[i]))*1e-9;
                if (dt > 0 && dt <= 0.1) {
                    path.previous = previous_[i];
                    path.seconds = dt;
                    path.continuous = true;
                }
            }
            // A repeated sample never creates a new motion interval. A reversed
            // clock seeds a new interval, without sweeping across the reversal.
            previous_[i] = pose;
            previous_times_[i] = frame.display_time;
            previous_valid_[i] = true;
        }
        published_ = next;
        return published_;
    }
    void reset() {
        std::lock_guard lock(mutex_);
        previous_valid_ = {};
        published_ = {};
    }
    HandPaths snapshot() const {
        std::lock_guard lock(mutex_);
        return published_;
    }
private:
    mutable std::mutex mutex_;
    std::array<combat::RigidPose,2> previous_{};
    std::array<XrTime,2> previous_times_{};
    std::array<bool,2> previous_valid_{};
    HandPaths published_{};
};

} // namespace w3vr::motion
