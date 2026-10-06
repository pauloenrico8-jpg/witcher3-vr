#include "sword_sweep.h"

#include <cstdio>
#include <cstdlib>
#include <limits>

using namespace w3vr::combat;
namespace {
void require(bool ok, const char* name) {
    if (!ok) { std::fprintf(stderr, "FAIL: %s\n", name); std::exit(1); }
}
CapsuleMotion blade_at(double x) {
    Segment segment{{x,0,0},{x,1,0}};
    return {segment,segment,0.002};
}
CapsuleMotion sphere_at(Vec3 centre, double radius = 0.01) {
    Segment point{centre,centre};
    return {point,point,radius};
}
}
int main() {
    auto blade = blade_at(-0.15);
    blade.current = blade_at(0.15).current;
    const auto target = sphere_at({0,0.5,0});
    require(sweep_capsules(blade_at(-0.15), target, 0.016).status == SweepStatus::no_contact,
            "first sampled pose misses");
    require(sweep_capsules(blade_at(0.15), target, 0.016).status == SweepStatus::no_contact,
            "last sampled pose also misses");
    auto hit = sweep_capsules(blade,target,0.016);
    require(hit.status == SweepStatus::contact, "fast blade detects contact between samples");
    require(std::abs(hit.fraction - 0.46) < 1e-4, "analytical first contact fraction");
    require(std::abs(hit.blade_point.y - 0.5) < 1e-8, "contact along the blade centreline");
    require(std::abs(hit.relative_speed_mps - 18.75) < 1e-8, "relative contact speed in metres per second");

    const auto near_miss = sphere_at({0,0.5,0.0121});
    require(sweep_capsules(blade,near_miss,0.016).status == SweepStatus::no_contact,
            "near miss must not become a hit");
    auto retreat = blade_at(-0.15);
    retreat.current = blade_at(-0.3).current;
    require(sweep_capsules(retreat,target,0.016).status == SweepStatus::no_contact,
            "retreating blade misses");

    auto moving_target = sphere_at({-0.15,0.5,0});
    moving_target.current = sphere_at({0.15,0.5,0}).current;
    hit = sweep_capsules(blade_at(0),moving_target,0.016);
    require(hit.status == SweepStatus::contact && hit.relative_speed_mps > 18,
            "enemy moving into a stationary blade can produce a parry candidate");
    CapsuleMotion enemy_blade{{{-0.2,0.5,0},{0.2,0.5,0}},
                             {{-0.2,0.5,0},{0.2,0.5,0}},0.003};
    hit = sweep_capsules(blade_at(0),enemy_blade,0.016);
    require(hit.status == SweepStatus::contact && hit.fraction == 0 && hit.relative_speed_mps == 0,
            "existing overlap is only a stationary contact candidate");
    require(sweep_capsules(sphere_at({0,0,0}),sphere_at({1,0,0}),0.016).status == SweepStatus::no_contact,
            "degenerate segments behave as spheres");
    require(sweep_capsules(blade_at(0),sphere_at({0,2,0}),0.016).status == SweepStatus::no_contact,
            "finite endpoints prevent hitting beyond the sword tip");

    auto parallel = blade_at(0.5);
    parallel.radius_m = 0.001;
    require(sweep_capsules(blade_at(0),parallel,0.016).status == SweepStatus::no_contact,
            "parallel non-overlapping capsules");
    CapsuleMotion almost_parallel{{{0,0,0},{1e6,0,0}},{{0,0,0},{1e6,0,0}},0};
    CapsuleMotion crossing{{{0,0.002,0},{1e6,-0.002,0}},
                           {{0,0.002,0},{1e6,-0.002,0}},0};
    require(sweep_capsules(almost_parallel,crossing,0.016).status == SweepStatus::contact,
            "near-parallel interior contact survives numerical cancellation");
    auto co_moving = target;
    co_moving.previous = sphere_at({-0.1,0.5,0}).previous;
    co_moving.current = sphere_at({0.2,0.5,0}).current;
    require(sweep_capsules(blade,co_moving,0.016).status == SweepStatus::no_contact,
            "equal motion preserves a non-contact separation");

    auto invalid = blade;
    invalid.current.a.x = std::numeric_limits<double>::quiet_NaN();
    require(sweep_capsules(invalid,target,0.016).status == SweepStatus::invalid_input,
            "reject missing or invalid tracking coordinates");
    invalid = blade; invalid.radius_m = -1;
    require(sweep_capsules(invalid,target,0.016).status == SweepStatus::invalid_input,
            "reject negative radii");
    require(sweep_capsules(blade,target,0).status == SweepStatus::invalid_input,
            "reject a zero duration");
    require(sweep_capsules(blade,target,0.5).status == SweepStatus::invalid_input,
            "reject stale frame gaps");
    require(sweep_capsules(blade,target,0.016,1).status == SweepStatus::inconclusive,
            "exhausted work budget is not a false hit or a definite miss");
    require(sweep_capsules(blade,target,0.016,0).status == SweepStatus::invalid_input,
            "reject an empty work budget");

    auto reversed = blade;
    std::swap(reversed.previous.a,reversed.previous.b);
    std::swap(reversed.current.a,reversed.current.b);
    const auto reversed_hit = sweep_capsules(reversed,target,0.016);
    require(reversed_hit.status == SweepStatus::contact && std::abs(reversed_hit.fraction-0.46)<1e-4,
            "endpoint order does not change the physical sweep");
    const auto swapped_hit = sweep_capsules(target,blade,0.016);
    require(swapped_hit.status == SweepStatus::contact && std::abs(swapped_hit.fraction-0.46)<1e-4,
            "swapping the moving shapes preserves the time of contact");
    auto shifted = blade; auto shifted_target = target;
    const Vec3 shift{150000,500,-5000};
    shifted.previous.a = shifted.previous.a + shift;
    shifted.previous.b = shifted.previous.b + shift;
    shifted.current.a = shifted.current.a + shift;
    shifted.current.b = shifted.current.b + shift;
    shifted_target.previous.a = shifted_target.previous.a + shift;
    shifted_target.previous.b = shifted_target.previous.b + shift;
    shifted_target.current = shifted_target.previous;
    const auto shifted_hit = sweep_capsules(shifted,shifted_target,0.016);
    require(shifted_hit.status == SweepStatus::contact && std::abs(shifted_hit.fraction-0.46)<1e-4,
            "large world-coordinate translation preserves the sweep");

    // Known analytical times over translations; also catches sign/scale errors.
    for (int i=1; i<=25; ++i) {
        const double travel = 0.04 + i*0.01;
        auto sample = blade_at(-travel);
        sample.current = blade_at(travel).current;
        const auto result = sweep_capsules(sample,target,0.02);
        const double expected = (travel - 0.012)/(2*travel);
        require(result.status == SweepStatus::contact && std::abs(result.fraction - expected) < 1e-4,
                "analytical contact across different sweep distances");
    }
    std::puts("Sword sweep geometry passed; game collision and physical combat remain unimplemented.");
}
