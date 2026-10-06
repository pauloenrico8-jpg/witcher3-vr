#pragma once

#include <algorithm>
#include <cmath>
#include <cstddef>

namespace w3vr::combat {

// Geometry only. The caller must supply continuous samples in the same
// coordinate system, in metres. No game memory, input emulation or damage calls.
struct Vec3 {
    double x{}, y{}, z{};
    Vec3 operator+(Vec3 b) const { return {x + b.x, y + b.y, z + b.z}; }
    Vec3 operator-(Vec3 b) const { return {x - b.x, y - b.y, z - b.z}; }
    Vec3 operator*(double s) const { return {x * s, y * s, z * s}; }
};
struct Segment { Vec3 a{}, b{}; };
struct CapsuleMotion {
    Segment previous{}, current{};
    double radius_m{};
};
enum class SweepStatus { invalid_input, no_contact, contact, inconclusive };
struct SweepResult {
    SweepStatus status{SweepStatus::invalid_input};
    double fraction{};
    Vec3 blade_point{}, target_point{}; // closest points on the centre lines
    double relative_speed_mps{};
    unsigned iterations{};
};

namespace detail {
inline double dot(Vec3 a, Vec3 b) { return a.x*b.x + a.y*b.y + a.z*b.z; }
inline Vec3 cross(Vec3 a, Vec3 b) {
    return {a.y*b.z - a.z*b.y, a.z*b.x - a.x*b.z, a.x*b.y - a.y*b.x};
}
inline double length(Vec3 v) { return std::sqrt(dot(v, v)); }
inline bool finite(Vec3 v) {
    // A generous numerical bound prevents overflow in segment products.
    return std::isfinite(v.x) && std::isfinite(v.y) && std::isfinite(v.z) &&
        std::abs(v.x) <= 1e6 && std::abs(v.y) <= 1e6 && std::abs(v.z) <= 1e6;
}
inline bool valid(const CapsuleMotion& m) {
    return finite(m.previous.a) && finite(m.previous.b) &&
        finite(m.current.a) && finite(m.current.b) &&
        std::isfinite(m.radius_m) && m.radius_m >= 0 && m.radius_m <= 1e4;
}
inline Segment at(const CapsuleMotion& m, double t) {
    return {m.previous.a + (m.current.a - m.previous.a)*t,
            m.previous.b + (m.current.b - m.previous.b)*t};
}
struct Closest { Vec3 first, second; double first_fraction, second_fraction; };
inline Closest closest(Segment first, Segment second) {
    const Vec3 u = first.b - first.a, v = second.b - second.a;
    const Vec3 w = first.a - second.a;
    const double a = dot(u,u), e = dot(v,v);
    auto projection = [](Vec3 offset, Vec3 axis, double squared_length) {
        return squared_length == 0 ? 0.0 : std::clamp(dot(offset,axis)/squared_length,0.0,1.0);
    };
    double t = projection(w,v,e);
    Closest best{first.a,second.a + v*t,0,t};
    double best_distance = dot(best.first-best.second,best.first-best.second);
    auto consider = [&](double s, double fraction) {
        const Vec3 p = first.a + u*s, q = second.a + v*fraction;
        const double distance = dot(p-q,p-q);
        if (distance < best_distance) {
            best = {p,q,s,fraction}; best_distance = distance;
        }
    };
    // A constrained minimum lies either at an edge of [0,1]^2 or at its
    // interior. Checking all edges also handles points and parallel segments.
    consider(1,projection(first.b-second.a,v,e));
    consider(projection(second.a-first.a,u,a),0);
    consider(projection(second.b-first.a,u,a),1);
    const Vec3 uv = cross(u,v);
    const double denominator = dot(uv,uv);
    if (denominator > 0) {
        // Cross products avoid cancellation in a*e-dot(u,v)^2 when nearly parallel.
        const double s = dot(cross(v,w),uv)/denominator;
        t = dot(cross(u,w),uv)/denominator;
        if (s >= 0 && s <= 1 && t >= 0 && t <= 1) consider(s,t);
    }
    return best;
}
} // namespace detail

// Conservative advancement for LINEAR endpoint motion between two samples.
// It can find contact even when both sampled end poses miss a thin target.
// Rigid rotation arcs require an upstream pose interpolator/subdivision; this
// function deliberately does not claim to reconstruct a quaternion trajectory.
// Exhausting the work budget returns inconclusive, never a fabricated hit or
// a definite miss. An overlap is a contact candidate, not permission for damage.
inline SweepResult sweep_capsules(const CapsuleMotion& blade,
    const CapsuleMotion& target, double frame_seconds,
    unsigned max_iterations = 96, double tolerance_m = 1e-6) {
    SweepResult result;
    if (!detail::valid(blade) || !detail::valid(target) ||
        !std::isfinite(frame_seconds) || frame_seconds <= 0 || frame_seconds > 0.1 ||
        !std::isfinite(tolerance_m) || tolerance_m <= 0 || tolerance_m > 0.01 ||
        max_iterations == 0 || max_iterations > 4096) return result;

    const Vec3 ba = blade.current.a - blade.previous.a;
    const Vec3 bb = blade.current.b - blade.previous.b;
    const Vec3 ta = target.current.a - target.previous.a;
    const Vec3 tb = target.current.b - target.previous.b;
    const double bound = std::max(detail::length(ba), detail::length(bb)) +
                         std::max(detail::length(ta), detail::length(tb));
    const double radius = blade.radius_m + target.radius_m;
    double t = 0;
    for (unsigned i = 0; i < max_iterations; ++i) {
        result.iterations = i + 1;
        const auto points = detail::closest(detail::at(blade,t), detail::at(target,t));
        const double gap = detail::length(points.first - points.second) - radius;
        result.fraction = t;
        result.blade_point = points.first;
        result.target_point = points.second;
        if (gap <= tolerance_m) {
            result.status = SweepStatus::contact;
            const Vec3 blade_delta = ba*(1-points.first_fraction) + bb*points.first_fraction;
            const Vec3 target_delta = ta*(1-points.second_fraction) + tb*points.second_fraction;
            result.relative_speed_mps = detail::length(blade_delta - target_delta)/frame_seconds;
            return result;
        }
        // Distance cannot close faster than this maximum endpoint displacement.
        if (bound == 0 || gap > bound*(1-t) + tolerance_m || t == 1) {
            result.status = SweepStatus::no_contact;
            return result;
        }
        const double next = std::min(1.0, t + 0.9*gap/bound);
        if (next <= t) break; // floating-point resolution reached
        t = next;
    }
    result.status = SweepStatus::inconclusive;
    return result;
}

} // namespace w3vr::combat
