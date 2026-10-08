#include "modern_camera_pose.h"
#include <cstdio>
#include <limits>
#include <algorithm>

namespace pose = w3vr::modern_camera_pose;
namespace geo = w3vr::openxr_eye_geometry;
namespace layout = w3vr::engine_camera_layout;
namespace {
int failures{},checks{};
void require(bool ok,const char* why) { ++checks; if (!ok) {++failures;std::fprintf(stderr,"FAIL: %s\n",why);} }
bool near(float a,float b,float eps=2e-4f) { return std::fabs(a-b)<=eps; }
using Matrix=std::array<float,9>;
// Independent Euler matrix: rows are right/forward/up in world coordinates.
// Does not call the tested quaternion conversion or any native function.
Matrix basis(float roll,float pitch,float yaw) {
    constexpr float rad=3.14159265358979323846f/180;
    const float cr=std::cos(roll*rad),sr=std::sin(roll*rad);
    const float cp=std::cos(pitch*rad),sp=std::sin(pitch*rad);
    const float cy=std::cos(yaw*rad),sy=std::sin(yaw*rad);
    return {cy*cr-sy*sp*sr,sy*cr+cy*sp*sr,-cp*sr,
        -sy*cp,cy*cp,sp,sy*sr+cy*sp*cr,cy*sr-sy*sp*cr,cp*cr};
}
layout::PoseFields get(const std::vector<std::uint8_t>& descriptor,std::size_t index=0) {
    layout::PoseFields p;
    require(pose::read_pose(std::span(descriptor).subspan(layout::remastered_500c.descriptor_cameras[index],0x5E0),p),"read output pose");
    return p;
}
std::vector<std::uint8_t> source() {
    std::vector<std::uint8_t> s(0xF750,0xA5);
    const std::uint32_t count=0;std::memcpy(s.data()+0xECA0,&count,4);
    const float fov=75;
    for (auto offset:layout::remastered_500c.descriptor_cameras) {
        layout::write_pose(std::span(s).subspan(offset,0x5E0),&layout::remastered_500c,{});
        std::memcpy(s.data()+offset+0x1C,&fov,4);
    }
    return s;
}
pose::TrackingSample tracked() {
    pose::TrackingSample t;t.valid=true;t.display_time=100;
    for (auto& v:t.views) v.fov={-.8f,.8f,.8f,-.8f};
    t.views[0].pose={{0,0,0,1},{-0.032f,0,0}};
    t.views[1].pose={{0,0,0,1},{0.032f,0,0}};
    return t;
}
void set_pose(std::vector<std::uint8_t>& s,std::size_t index,layout::PoseFields p) {
    require(layout::write_pose(std::span(s).subspan(layout::remastered_500c.descriptor_cameras[index],0x5E0),&layout::remastered_500c,p),"set fixture pose");
}
void place(pose::TrackingSample& t,XrQuaternionf q,XrVector3f center) {
    const auto left=geo::rotate(q,{-0.032f,0,0});
    const auto right=geo::rotate(q,{0.032f,0,0});
    t.views[0].pose={q,pose::add(center,left)};t.views[1].pose={q,pose::add(center,right)};
}
}
int main() {
    auto s=source();const auto untouched=s;
    auto t=tracked();pose::PreparedPair out;
    require(pose::prepare_pair(s,t,{},out),"prepare both eyes");
    require(s==untouched,"original descriptor unchanged");
    require(near(get(out.descriptors[0]).position[0],-0.032f) &&
        near(get(out.descriptors[1]).position[0],0.032f),"64mm physical eye separation");
    for (std::size_t eye=0;eye<2;++eye) {
        bool guards=true;
        for (std::size_t b=0;b<s.size();++b) {
            bool pose_byte=false;
            for (auto c:layout::remastered_500c.descriptor_cameras)
                pose_byte|=(b>=c && b<c+12)||(b>=c+0x10 && b<c+0x1C);
            if (!pose_byte) guards &= out.descriptors[eye][b]==s[b];
        }
        require(guards,"every non-pose byte including matrices history FOV pointer fields unchanged");
    }
    set_pose(s,0,{{10,20,30},{0,0,90}});set_pose(s,1,{{10,20,30},{0,0,90}});
    t=tracked();place(t,{0,0,0,1},{1,2,-3});
    require(pose::prepare_pair(s,t,{1,true},out),"translated yawed game anchor");
    auto p=get(out.descriptors[0]);
    require(near(p.position[0],7) && near(p.position[1],20.968f) && near(p.position[2],32),"right up forward map onto yawed game world");
    require(pose::prepare_pair(s,t,{0,true},out),"zero room translation scale");
    auto left=get(out.descriptors[0]);auto right=get(out.descriptors[1]);
    require(near(left.position[0],10) && near(left.position[2],30) &&
        near(right.position[1]-left.position[1],0.064f),"translation scale never shrinks physical IPD");
    // Recenter heading and room origin cancel only their common components.
    s=source();t=tracked();t.origin_heading=geo::from_hmd_euler_degrees(0,90,0);t.origin_position={3,4,5};
    place(t,t.origin_heading,t.origin_position);
    require(pose::prepare_pair(s,t,{},out),"heading-only recenter");
    p=get(out.descriptors[0]);require(near(p.position[0],-0.032f)&&near(p.position[1],0)&&near(p.position[2],0)&&near(p.rotation_degrees[2],0),"recenter removes heading and position but preserves eyes");
    // Explicit expected basis checks exercise roll sign, pitch sign and heading.
    t=tracked();place(t,geo::from_hmd_euler_degrees(23,37,11),{});
    require(pose::prepare_pair(s,t,{},out),"mixed physical head orientation");
    p=get(out.descriptors[0]);const auto actual=basis(p.rotation_degrees[0],p.rotation_degrees[1],p.rotation_degrees[2]);
    const auto expected=basis(-11,23,37);
    bool same=true;for (std::size_t i=0;i<9;++i) same &= near(actual[i],expected[i]);
    require(same,"independent matrix agrees with physical up right forward signs");
    // Native cameras are a rig, not HMD eyes: preserve secondary relative pose.
    set_pose(s,0,{{10,20,30},{4,19,25}});set_pose(s,1,{{12,23,34},{-7,-12,50}});
    t=tracked();place(t,geo::from_hmd_euler_degrees(17,-31,9),{.2f,.4f,-.3f});
    require(pose::prepare_pair(s,t,{1,false},out),"unequal internal camera rig");
    for (std::size_t eye=0;eye<2;++eye) {
        auto a=get(out.descriptors[eye],0),b=get(out.descriptors[eye],1);
        auto world_delta=pose::subtract(pose::position(b),pose::position(a));
        auto relative=geo::rotate(geo::conjugate(pose::orientation(a)),world_delta);
        auto original_relative=geo::rotate(geo::conjugate(geo::from_redengine_view_euler_degrees(4,19,25)),{2,3,4});
        require(near(relative.x,original_relative.x)&&near(relative.y,original_relative.y)&&near(relative.z,original_relative.z),"secondary offset stays fixed relative to primary camera");
        auto relative_q=geo::multiply(geo::conjugate(pose::orientation(a)),pose::orientation(b));
        auto original_q=geo::multiply(geo::conjugate(geo::from_redengine_view_euler_degrees(4,19,25)),geo::from_redengine_view_euler_degrees(-7,-12,50));
        require(near(std::fabs(relative_q.x*original_q.x+relative_q.y*original_q.y+relative_q.z*original_q.z+relative_q.w*original_q.w),1),"secondary orientation stays fixed relative to primary camera");
    }
    // Near vertical pitch must retain an equivalent orientation, even at exact
    // gimbal lock and nonzero coupled roll/yaw. Compare independent matrices.
    for (float pitch:{-90.f,-89.99f,-40.f,0.f,40.f,89.99f,90.f})
      for (float roll:{-170.f,-20.f,0.f,35.f,170.f})
       for (float yaw:{-179.f,-25.f,70.f,179.f}) {
        std::array<float,3> encoded{};
        require(pose::encode_orientation(geo::from_redengine_view_euler_degrees(roll,pitch,yaw),{roll,pitch,yaw},encoded),"encode orientation around singularities");
        auto a=basis(roll,pitch,yaw),b=basis(encoded[0],encoded[1],encoded[2]);
        bool equivalent=true;for (std::size_t i=0;i<9;++i) equivalent &= near(a[i],b[i],.002f);
        require(equivalent,"independent matrix remains equivalent at wrapped and vertical angles");
    }
    // Canted runtime eyes each carry their own orientation, not a fixed-offset
    // assumption. Eye position follows the actual runtime pose as well.
    s=source();t=tracked();t.views[0].pose.orientation=geo::from_hmd_euler_degrees(0,-5,0);
    t.views[1].pose.orientation=geo::from_hmd_euler_degrees(0,5,0);
    require(pose::prepare_pair(s,t,{},out),"canted eye pair");
    require(near(get(out.descriptors[0]).rotation_degrees[2],-5)&&
        near(get(out.descriptors[1]).rotation_degrees[2],5),"distinct eye cant retained");
    const auto frozen=t;place(t,geo::from_hmd_euler_degrees(0,40,0),{});
    require(pose::prepare_pair(s,frozen,{},out),"copied locate result is immutable");
    require(near(get(out.descriptors[0]).rotation_degrees[2],-5),"later tracking changes cannot change frozen input");
    const auto sentinel=out.descriptors;
    const auto fail=[&](std::span<const std::uint8_t> input,pose::TrackingSample tracking,pose::Options options={}) {
        require(!pose::prepare_pair(input,tracking,options,out),"invalid input rejected");
        require(out.descriptors==sentinel,"failure preserves both existing output descriptors");
    };
    t=tracked();t.valid=false;fail(s,t);t=tracked();t.display_time=0;fail(s,t);
    t=tracked();t.origin_heading={.3f,0,0,.9f};fail(s,t);
    t=tracked();t.views[0].pose.orientation={0,0,0,0};fail(s,t);
    t=tracked();t.views[1].pose.position.x=10;fail(s,t);
    t=tracked();t.views[0].next=reinterpret_cast<void*>(1);fail(s,t);
    t=tracked();t.origin_position.x=std::numeric_limits<float>::infinity();fail(s,t);
    t=tracked();t.views[1].fov.angleUp=std::numeric_limits<float>::quiet_NaN();fail(s,t);
    t=tracked();t.views[0].fov.angleLeft=-1.6f;fail(s,t);
    t=tracked();t.views[0].fov.angleLeft=2;fail(s,t);
    t=tracked();fail(std::span(s).first(0xF74F),t);fail(s,t,{-1,true});
    const auto count=std::uint32_t{5};std::memcpy(s.data()+0xECA0,&count,4);fail(s,t);
    s=source();float nan=std::numeric_limits<float>::quiet_NaN();std::memcpy(s.data()+0x5F0+0x14,&nan,4);fail(s,t);
    s=source();float ortho=0;std::memcpy(s.data()+0x5F0+0x1C,&ortho,4);fail(s,t);
    require(source()!=s,"fixtures exercised failures");
    std::printf("Modern camera pose: %d checks, %d failures; CPU fixtures only.\n",checks,failures);
    return failures?1:0;
}
