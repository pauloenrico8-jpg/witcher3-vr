#include "modern_camera_pose.h"
#include <cstdio>
#include <limits>
#include <algorithm>

namespace pose = w3vr::modern_camera_pose;
namespace geo = w3vr::openxr_eye_geometry;
namespace layout = w3vr::engine_camera_layout;
namespace lens = w3vr::modern_camera_projection;
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
        const float parameters[]{1.777f,2.0f,.1f,5000.f};
        std::memcpy(s.data()+offset+0x28,parameters,sizeof(parameters));
        const float center[]{.31f,-.27f};std::memcpy(s.data()+offset+0x520,center,sizeof(center));
        const layout::ProjectionFields jitter{{0,0},{1920,1080}};
        require(layout::write_projection(std::span(s).subspan(offset,0x5E0),&layout::remastered_500c,jitter),"fixture native pixel jitter");
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
// Independent CPU model of the reached modern matrix convention. Perspective
// is multiplied by pixel-jitter translation and then stable lens translation.
// No tested projection derivation or native function is called by this model.
using Matrix4=std::array<double,16>;
float scalar(const std::vector<std::uint8_t>& s,std::size_t offset) {
    float f{};std::memcpy(&f,s.data()+offset,4);return f;
}
Matrix4 product(const Matrix4& a,const Matrix4& b) {
    Matrix4 c{};
    for (std::size_t r=0;r<4;++r) for (std::size_t col=0;col<4;++col)
        for (std::size_t k=0;k<4;++k) c[r*4+col]+=a[r*4+k]*b[k*4+col];
    return c;
}
std::array<double,3> project(const std::vector<std::uint8_t>& s,double x,double y,double z) {
    const auto c=layout::remastered_500c.descriptor_cameras[0];
    const double cot=1/std::tan(scalar(s,c+0x1C)*3.14159265358979323846/360);
    const double n=scalar(s,c+0x30),f=scalar(s,c+0x34),scale=scalar(s,c+0x2C);
    const Matrix4 perspective{scale*cot/scalar(s,c+0x28),0,0,0,
        0,scale*cot,0,0,0,0,f/(f-n),1,0,0,-n*f/(f-n),0};
    std::array<std::uint32_t,2> size{};std::memcpy(size.data(),s.data()+c+0x4C8,8);
    const Matrix4 jitter{1,0,0,0,0,1,0,0,0,0,1,0,
        2*scalar(s,c+0x4C0)/std::max(1u,size[0]),2*scalar(s,c+0x4C4)/std::max(1u,size[1]),0,1};
    const Matrix4 stable{1,0,0,0,0,1,0,0,0,0,1,0,
        scalar(s,c+0x520),scalar(s,c+0x524),0,1};
    const auto m=product(product(perspective,jitter),stable);
    const std::array<double,4> point{x,y,z,1};std::array<double,4> clip{};
    for (std::size_t col=0;col<4;++col) for (std::size_t k=0;k<4;++k)
        clip[col]+=point[k]*m[k*4+col];
    return {clip[0]/clip[3],clip[1]/clip[3],clip[2]/clip[3]};
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
            bool written=false;
            for (auto c:layout::remastered_500c.descriptor_cameras)
                written|=(b>=c && b<c+12)||(b>=c+0x10 && b<c+0x1C);
            const auto c=layout::remastered_500c.descriptor_cameras[0];
            written|=(b>=c+0x1C && b<c+0x20)||(b>=c+0x28 && b<c+0x30)||
                (b>=c+0x520 && b<c+0x528);
            if (!written) guards &= out.descriptors[eye][b]==s[b];
        }
        require(guards,"every byte outside pose/primary-lens fields, including jitter integers matrices history and pointers, unchanged");
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
    // Different, asymmetric lenses must map their actual edge rays to all
    // four image edges. Depth remains native, while old game zoom/center are
    // replaced, not accumulated. Test several distances with row matrices.
    s=source();t=tracked();
    t.views[0].fov={-.9f,.6f,.85f,-.65f};
    t.views[1].fov={-.6f,.9f,.75f,-.8f};
    require(pose::prepare_pair(s,t,{},out),"prepare distinct asymmetric lenses");
    for (std::size_t eye=0;eye<2;++eye) {
        const auto c=layout::remastered_500c.descriptor_cameras[0];
        const auto& camera=out.descriptors[eye];const auto fov=t.views[eye].fov;
        require(scalar(camera,c+0x2C)==1,"old game projection zoom replaced by physical runtime lens");
        require(scalar(camera,c+0x30)==scalar(s,c+0x30) && scalar(camera,c+0x34)==scalar(s,c+0x34),"native near and far range preserved");
        for (double z:{.1,7.,5000.}) for (int xsign:{-1,1}) for (int ysign:{-1,1}) {
            const double tx=std::tan(xsign<0?fov.angleLeft:fov.angleRight);
            const double ty=std::tan(ysign<0?fov.angleDown:fov.angleUp);
            const auto projected=project(camera,tx*z,ty*z,z);
            require(std::fabs(projected[0]-xsign)<2e-6 && std::fabs(projected[1]-ysign)<2e-6,"independent perspective/jitter/stable matrix maps XR corners to image edges");
        }
        const auto near_point=project(camera,0,0,scalar(camera,c+0x30));
        const auto far_point=project(camera,0,0,scalar(camera,c+0x34));
        require(std::fabs(near_point[2])<1e-8 && std::fabs(far_point[2]-1)<1e-8,"native depth endpoints preserved");
        // A later native writer changes pixel jitter/dimensions, not the stable
        // lens center. This is a CPU field model, not a native-hook execution.
        auto updated=camera;const float native_jitter[]{.75f,-.25f};
        const std::uint32_t native_size[]{1600,900};
        std::memcpy(updated.data()+c+0x4C0,native_jitter,8);
        std::memcpy(updated.data()+c+0x4C8,native_size,8);
        const auto a=project(updated,std::tan(fov.angleLeft),std::tan(fov.angleDown),1);
        require(std::fabs(a[0]-(-1+2*.75/1600))<2e-6 &&
            std::fabs(a[1]-(-1-2*.25/900))<2e-6,"temporal pixel jitter remains separate with exact integer dimensions");
        require(std::memcmp(updated.data()+c+0x520,camera.data()+c+0x520,8)==0,"later pixel-jitter update cannot erase lens shift");
        auto recentered=camera;const float different_source_center[]{.6f,-.8f};
        std::memcpy(recentered.data()+c+0x520,different_source_center,8);
        lens::LensFields fields;
        require(lens::derive(std::span(recentered).subspan(c,0x5E0),fov,fields) &&
            lens::write(std::span(recentered).subspan(c,0x5E0),fields),"lens center uses absolute assignment");
        require(recentered==camera,"lens preparation never accumulates an existing source center");
    }
    // Reject bad native perspective inputs before either prepared eye escapes.
    const auto projection_sentinel=out.descriptors;
    for (auto offset:{0x28u,0x2Cu,0x30u,0x34u}) {
        auto invalid=source();const float zero=0;
        std::memcpy(invalid.data()+0x10+offset,&zero,4);
        require(!pose::prepare_pair(invalid,t,{},out),"invalid native perspective or depth rejected");
        require(out.descriptors==projection_sentinel,"bad native lens leaves both eye outputs intact");
    }
    auto writable=out.descriptors[0];const auto unchanged=writable;
    lens::LensFields bad{90,1,1,{0,std::numeric_limits<float>::infinity()}};
    require(!lens::write(std::span(writable).subspan(0x10,0x5E0),bad) && writable==unchanged,"invalid lens write leaves all bytes intact");
    lens::LensFields sentinel_lens{42,3,1,{.2f,.1f}},derived=sentinel_lens;
    require(!lens::derive(std::span(writable).subspan(0x10,0x5DF),t.views[0].fov,derived) &&
        derived.vertical_fov_degrees==sentinel_lens.vertical_fov_degrees,"short camera rejects without publishing lens");
    // Restore the canted-eye fixture for the frozen tracking checks below.
    s=source();t=tracked();t.views[0].pose.orientation=geo::from_hmd_euler_degrees(0,-5,0);
    t.views[1].pose.orientation=geo::from_hmd_euler_degrees(0,5,0);
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
    std::printf("Modern camera pose/lens: %d checks, %d failures; CPU fixtures and matrix model only.\n",checks,failures);
    return failures?1:0;
}
