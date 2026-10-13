#pragma once
#include "engine_camera_temporal.h"
#include <bit>
#include <algorithm>
namespace w3vr::camera_history_math {
using Matrix=std::array<float,16>;
inline Matrix multiply(const Matrix& a,const Matrix& b) noexcept {
    Matrix out{};
    for(unsigned row=0;row<4;++row)for(unsigned col=0;col<4;++col){
        const float high=a[row*4+2]*b[8+col]+a[row*4+3]*b[12+col];
        const float low=a[row*4]*b[col]+a[row*4+1]*b[4+col];
        out[row*4+col]=high+low;
    }
    return out;
}
// OWN scalar mathematics. No Game function, object pointer or SDK is invoked.
// Source prefix/time are copied values; destination is always staged locally.
inline bool build(const engine_camera::TemporalContract* c,std::span<const std::uint8_t> prefix,
    float time,engine_camera::TemporalRecord& out) noexcept {
    engine_camera::TemporalInputs in{};
    if(c!=&engine_camera::remastered_1048522 || !engine_camera::read_temporal_inputs(prefix,time,in) ||
        in.fov>=179.9f || in.projection_scale<=0)return false;
    // Match the independently examined float conversion constants. Keep raw
    // Euler values in the record; never wrap cached inputs or modify Game pose.
    constexpr float degrees=std::bit_cast<float>(0x3C8EFA36u);
    constexpr float half_degrees=std::bit_cast<float>(0x3C0EFA36u);
    float sx{},sy{},sz{},cx{},cy{},cz{};
    const float x=in.rotation[0]*degrees,y=in.rotation[1]*degrees,z=in.rotation[2]*degrees;
    if(!std::isfinite(x)||!std::isfinite(y)||!std::isfinite(z))return false;
    sx=std::sin(x);sy=std::sin(y);sz=std::sin(z);cx=std::cos(x);cy=std::cos(y);cz=std::cos(z);
    const Matrix rotation{cz*cx-sz*sy*sx,sz*cx+cz*sy*sx,-cy*sx,0,
        -sz*cy,cz*cy,sy,0,cz*sx+sz*sy*cx,sz*sx-cz*sy*cx,cy*cx,0,0,0,0,1};
    Matrix transpose{};for(unsigned row=0;row<4;++row)for(unsigned col=0;col<4;++col)transpose[row*4+col]=rotation[col*4+row];
    const Matrix translation{1,0,0,0,0,1,0,0,0,0,1,0,-in.position[0],-in.position[1],-in.position[2],1};
    const Matrix basis{1,0,0,0,0,0,1,0,0,1,0,0,0,0,0,1};
    const auto view=multiply(multiply(translation,transpose),basis);
    const float tangent=std::tan(in.fov*half_degrees);
    const float inverse=1/(in.far_plane-in.near_plane);
    const float depth=inverse*in.far_plane;
    Matrix projection{};projection[0]=(1/tangent)/in.aspect*in.projection_scale;
    projection[5]=(1/tangent)*in.projection_scale;projection[10]=depth;
    projection[11]=1;projection[14]=-(depth*in.near_plane);
    for(const auto* matrix:std::array<const Matrix*,2>{&view,&projection})for(float value:*matrix)if(!std::isfinite(value))return false;
    if(projection[0]<=0 || projection[5]<=0)return false;
    engine_camera::TemporalRecord staged{};staged[0]=1;
    std::memcpy(staged.data()+4,&time,4);std::memcpy(staged.data()+8,&in.fov,4);
    std::memcpy(staged.data()+0x10,in.position.data(),16);std::memcpy(staged.data()+0x20,in.rotation.data(),12);
    std::memcpy(staged.data()+0x30,view.data(),64);std::memcpy(staged.data()+0x70,projection.data(),64);
    out=staged;return true;
}
// Validate against THIS normal builder result already observed by the host.
// Metadata/pose must match bit-for-bit. Matrix comparison permits rounding
// differences between the Game's SIMD polynomials and this OWN scalar model.
// This tests local compatibility, not rendered images, ownership or GPU work.
inline bool matches_reference(const engine_camera::TemporalContract* c,std::span<const std::uint8_t> prefix,
    float time,const engine_camera::TemporalRecord& native) noexcept {
    engine_camera::TemporalRecord expected{};
    if(native[0]!=1 || !build(c,prefix,time,expected) ||
        std::memcmp(native.data()+4,expected.data()+4,8) || std::memcmp(native.data()+0x10,expected.data()+0x10,0x1C))return false;
    engine_camera::TemporalInputs input{};if(!engine_camera::read_temporal_inputs(prefix,time,input))return false;
    const double magnitude=std::fabs(static_cast<double>(input.position[0]))+
        std::fabs(static_cast<double>(input.position[1]))+std::fabs(static_cast<double>(input.position[2]));
    for(unsigned index=0;index<32;++index){float a{},b{};
        std::memcpy(&a,native.data()+0x30+index*4,4);std::memcpy(&b,expected.data()+0x30+index*4,4);
        if(!std::isfinite(a)||!std::isfinite(b))return false;
        const double base=index>=12 && index<15 ? 2e-6*static_cast<double>(magnitude) : 2e-6;
        const double tolerance=base+4e-6*std::max(std::fabs(static_cast<double>(a)),std::fabs(static_cast<double>(b)));
        if(std::fabs(static_cast<double>(a)-b)>tolerance)return false;
    }
    return true;
}
} // namespace w3vr::camera_history_math
