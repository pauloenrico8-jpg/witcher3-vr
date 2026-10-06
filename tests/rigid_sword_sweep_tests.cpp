#include "rigid_sword_sweep.h"
#include <cstdio>
#include <cstdlib>
#include <limits>
#include <numbers>

using namespace w3vr::combat;
namespace {
void require(bool ok, const char* name) {
    if (!ok) { std::fprintf(stderr,"FAIL: %s\n",name); std::exit(1); }
}
Quaternion yaw(double radians) { return {0,0,std::sin(radians/2),std::cos(radians/2)}; }
CapsuleMotion sphere(Vec3 point, double radius=0.01) { return {{point,point},{point,point},radius}; }
}
int main() {
    const double pi = std::numbers::pi;
    RigidBladeMotion tip{{{},yaw(0)},{ {},yaw(pi/2)},{{1,0,0},{1,0,0}},0.002};
    const auto target = sphere({std::sqrt(0.5),std::sqrt(0.5),0});
    CapsuleMotion chord{{{1,0,0},{1,0,0}},{{0,1,0},{0,1,0}},tip.radius_m};
    require(sweep_capsules(chord,target,0.016).status == SweepStatus::no_contact,
            "straight tip chord misses an object on the true arc");
    auto hit = sweep_rigid_blade(tip,target,0.016);
    const double expected = (pi/4 - 2*std::asin(0.012/2))/(pi/2);
    require(hit.status == SweepStatus::contact && std::abs(hit.fraction-expected)<1e-5,
            "rotation detects analytical first contact between sampled poses");
    require(std::abs(hit.relative_speed_mps-(pi/2)/0.016)<1e-8,
            "contact velocity includes angular speed at the sword tip");
    require(std::abs(detail::length(hit.blade_point)-1)<1e-10,
            "rigid rotation preserves sword length at contact");
    auto blade=tip;blade.local_blade={{0.1,0,0},{1,0,0}};
    auto interior=sweep_rigid_blade(blade,sphere({0.9*std::sqrt(0.5),0.9*std::sqrt(0.5),0}),0.016);
    const double interior_angle=std::asin(0.012/0.9);
    require(interior.status==SweepStatus::contact &&
            std::abs(interior.fraction-(pi/4-interior_angle)/(pi/2))<1e-5 &&
            std::abs(interior.relative_speed_mps-0.9*std::cos(interior_angle)*(pi/2)/0.016)<1e-3,
            "contact inside a finite sword uses velocity at that part of the blade");
    // Rotate the entire experiment around another axis, then add equal linear
    // motion to blade and target. Time and relative speed must be invariant.
    const Quaternion basis{std::sin(0.4),0,0,std::cos(0.4)};
    auto transformed=tip;auto transformed_target=target;
    transformed.previous.orientation=rigid_detail::multiply(basis,tip.previous.orientation);
    transformed.current.orientation=rigid_detail::multiply(basis,tip.current.orientation);
    transformed.current.position={0.08,-0.03,0.04};
    transformed_target.previous.a=transformed_target.previous.b=rigid_detail::rotate(basis,target.previous.a);
    transformed_target.current.a=transformed_target.current.b=transformed_target.previous.a+transformed.current.position;
    auto invariant=sweep_rigid_blade(transformed,transformed_target,0.016);
    // Advancement may stop at different points inside the 1 micrometre
    // contact tolerance because the conservative speed bound is different.
    require(invariant.status==SweepStatus::contact && std::abs(invariant.fraction-hit.fraction)<1e-5 &&
            std::abs(invariant.relative_speed_mps-hit.relative_speed_mps)<1e-8,
            "noncommuting orientation basis and shared translation preserve relative contact");
    require(sweep_capsules(chord,sphere({0.5,0.5,0}),0.016).status == SweepStatus::contact,
            "chord intersects the inner point");
    require(sweep_rigid_blade(tip,sphere({0.5,0.5,0}),0.016).status == SweepStatus::no_contact,
            "arc rejects the inner point that chord interpolation hits");
    auto flipped = tip;
    auto& q=flipped.current.orientation; q={-q.x,-q.y,-q.z,-q.w};
    auto same = sweep_rigid_blade(flipped,target,0.016);
    require(same.status == hit.status && std::abs(same.fraction-hit.fraction)<1e-10,
            "equivalent quaternion signs cannot reverse or lengthen the arc");
    auto shifted = tip; auto shifted_target=target;
    const Vec3 offset{150000,-15000,1200};
    shifted.previous.position=offset; shifted.current.position=offset;
    shifted_target.previous.a=shifted_target.previous.b=target.previous.a+offset;
    shifted_target.current=shifted_target.previous;
    same=sweep_rigid_blade(shifted,shifted_target,0.016);
    require(same.status == SweepStatus::contact && std::abs(same.fraction-hit.fraction)<1e-5,
            "large translation preserves rotation contact");
    auto translate=tip;
    translate.current.orientation=translate.previous.orientation;
    translate.previous.position={-1.2,0,0}; translate.current.position={-0.8,0,0};
    hit=sweep_rigid_blade(translate,sphere({0,0,0}),0.02);
    require(hit.status==SweepStatus::contact && std::abs(hit.fraction-0.47)<1e-5 &&
            std::abs(hit.relative_speed_mps-20)<1e-8,
            "pure rigid translation matches the linear analytical sweep");
    auto moving=target;
    moving.previous=sphere({1,-0.2,0}).previous;
    moving.current=sphere({1,0.2,0}).current;
    auto stationary=tip;stationary.current=stationary.previous;
    hit=sweep_rigid_blade(stationary,moving,0.02);
    require(hit.status==SweepStatus::contact && hit.relative_speed_mps>19.99,
            "enemy motion into a stationary sword is a relative contact candidate");
    require(sweep_rigid_blade(tip,target,0.016,1).status==SweepStatus::inconclusive,
            "work budget exhaustion is neither a hit nor a proven miss");
    auto shared=tip;shared.current.orientation=shared.previous.orientation;
    shared.current.position={0.5,0,0};
    auto neighbour=sphere({1,0.03,0});neighbour.current=sphere({1.5,0.03,0}).current;
    require(sweep_rigid_blade(shared,neighbour,0.016,1).status==SweepStatus::no_contact,
            "equal translation cancels in the bound and proves a near miss in one step");
    auto invalid=tip;invalid.current.orientation={0,0,0,0};
    require(sweep_rigid_blade(invalid,target,0.016).status==SweepStatus::invalid_input,
            "zero quaternion rejected");
    invalid=tip;invalid.current.orientation.x=std::numeric_limits<double>::quiet_NaN();
    require(sweep_rigid_blade(invalid,target,0.016).status==SweepStatus::invalid_input,
            "invalid orientation rejected");
    require(sweep_rigid_blade(tip,target,0.2).status==SweepStatus::invalid_input,
            "stale intervals cannot invent unseen rotation");
    for (int degree=20;degree<=160;degree+=10) {
        const double angle=degree*pi/180;
        auto arc=tip;arc.current.orientation=yaw(angle);
        const double phase=angle*0.6;
        auto result=sweep_rigid_blade(arc,sphere({std::cos(phase),std::sin(phase),0}),0.02);
        require(result.status==SweepStatus::contact &&
                std::abs(result.fraction-(phase-2*std::asin(0.012/2))/angle)<1e-5,
                "analytical contacts over multiple angular displacements");
    }
    std::puts("Rigid sword arc geometry passed; enemy/damage integration is not validated.");
}
