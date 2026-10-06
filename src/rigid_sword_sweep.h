#pragma once

#include "sword_sweep.h"

namespace w3vr::combat {

struct Quaternion { double x{}, y{}, z{}, w{1}; };
struct RigidPose { Vec3 position{}; Quaternion orientation{}; };
struct RigidBladeMotion {
    RigidPose previous{}, current{};
    Segment local_blade{}; // fixed endpoints relative to the tracked grip, metres
    double radius_m{};
};

namespace rigid_detail {
inline Quaternion multiply(Quaternion a, Quaternion b) {
    return {a.w*b.x+a.x*b.w+a.y*b.z-a.z*b.y,
            a.w*b.y-a.x*b.z+a.y*b.w+a.z*b.x,
            a.w*b.z+a.x*b.y-a.y*b.x+a.z*b.w,
            a.w*b.w-a.x*b.x-a.y*b.y-a.z*b.z};
}
inline bool normalize(Quaternion q, Quaternion& out) {
    const double n = q.x*q.x+q.y*q.y+q.z*q.z+q.w*q.w;
    if (!std::isfinite(n) || n < 0.95 || n > 1.05) return false;
    const double s = 1/std::sqrt(n);
    out = {q.x*s,q.y*s,q.z*s,q.w*s};
    return true;
}
inline Vec3 rotate(Quaternion q, Vec3 v) {
    const Vec3 xyz{q.x,q.y,q.z};
    const Vec3 t = detail::cross(xyz,v)*2;
    return v + t*q.w + detail::cross(xyz,t);
}
struct Path {
    RigidPose first{};
    Vec3 translation{}, axis{};
    double angle{}; // shortest rotation, radians; no extra turns inferred
    Quaternion orientation_at(double t) const {
        const double s = std::sin(angle*t/2);
        return multiply({axis.x*s,axis.y*s,axis.z*s,std::cos(angle*t/2)},
                        first.orientation);
    }
    Segment at(Segment local, double t) const {
        const Vec3 p = first.position + translation*t;
        const auto q = orientation_at(t);
        return {p+rotate(q,local.a),p+rotate(q,local.b)};
    }
    Vec3 point_delta(Vec3 local, double t) const {
        // Derivative with respect to frame fraction, not seconds.
        return translation + detail::cross(axis*angle,rotate(orientation_at(t),local));
    }
};
inline bool path(const RigidBladeMotion& blade, Path& out) {
    if (!detail::finite(blade.previous.position) || !detail::finite(blade.current.position) ||
        !detail::finite(blade.local_blade.a) || !detail::finite(blade.local_blade.b) ||
        !std::isfinite(blade.radius_m) || blade.radius_m < 0 || blade.radius_m > 1e4)
        return false;
    Quaternion first, last;
    if (!normalize(blade.previous.orientation,first) ||
        !normalize(blade.current.orientation,last)) return false;
    if (first.x*last.x+first.y*last.y+first.z*last.z+first.w*last.w < 0)
        last = {-last.x,-last.y,-last.z,-last.w};
    const auto delta = multiply(last,{-first.x,-first.y,-first.z,first.w});
    const double sine = detail::length({delta.x,delta.y,delta.z});
    out.first = {blade.previous.position,first};
    out.translation = blade.current.position-blade.previous.position;
    out.angle = 2*std::atan2(sine,std::clamp(delta.w,0.0,1.0));
    out.axis = sine > 0 ? Vec3{delta.x,delta.y,delta.z}*(1/sine) : Vec3{};
    return true;
}
} // namespace rigid_detail

// Evaluates the actual rigid rotation arc at every advancement step. Unlike
// endpoint interpolation, this never shortens the sword during rotation.
// Assumes linear grip translation and the shortest quaternion arc between
// samples; tracking cannot reveal unobserved turns or changes of direction.
// Targets still use linear endpoint motion. Contact is geometry, not damage.
inline SweepResult sweep_rigid_blade(const RigidBladeMotion& blade,
    const CapsuleMotion& target, double frame_seconds,
    unsigned max_iterations = 128, double tolerance_m = 1e-6) {
    SweepResult result;
    rigid_detail::Path path;
    if (!rigid_detail::path(blade,path) || !detail::valid(target) ||
        !std::isfinite(frame_seconds) || frame_seconds <= 0 || frame_seconds > 0.1 ||
        !std::isfinite(tolerance_m) || tolerance_m <= 0 || tolerance_m > 0.01 ||
        max_iterations == 0 || max_iterations > 4096) return result;
    const Vec3 ta = target.current.a-target.previous.a;
    const Vec3 tb = target.current.b-target.previous.b;
    const double grip_radius = std::max(detail::length(blade.local_blade.a),
                                       detail::length(blade.local_blade.b));
    // Bound relative point motion directly: common translation must cancel.
    // Target velocity is a convex combination of its endpoint displacements,
    // whose maximum relative speed bounds every point on that segment.
    const double bound = path.angle*grip_radius +
        std::max(detail::length(path.translation-ta),detail::length(path.translation-tb));
    double t = 0;
    for (unsigned i=0; i<max_iterations; ++i) {
        result.iterations = i+1;
        const auto points = detail::closest(path.at(blade.local_blade,t),detail::at(target,t));
        const double gap = detail::length(points.first-points.second) - blade.radius_m-target.radius_m;
        result.fraction = t;
        result.blade_point = points.first;
        result.target_point = points.second;
        if (gap <= tolerance_m) {
            const Vec3 local = blade.local_blade.a +
                (blade.local_blade.b-blade.local_blade.a)*points.first_fraction;
            const Vec3 target_delta = ta*(1-points.second_fraction)+tb*points.second_fraction;
            result.relative_speed_mps = detail::length(path.point_delta(local,t)-target_delta)/frame_seconds;
            result.status = SweepStatus::contact;
            return result;
        }
        if (bound == 0 || gap > bound*(1-t)+tolerance_m || t == 1) {
            result.status = SweepStatus::no_contact;
            return result;
        }
        const double next = std::min(1.0,t+0.9*gap/bound);
        if (next <= t) break;
        t = next;
    }
    result.status = SweepStatus::inconclusive;
    return result;
}

} // namespace w3vr::combat
