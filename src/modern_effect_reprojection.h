#pragma once
#include "engine_camera_temporal.h"
#include <algorithm>
namespace w3vr::effect_reprojection {
using Matrix=std::array<float,16>;
using DoubleMatrix=std::array<double,16>;
enum class SourceRegister { rdi,rsi,rbx };
struct Producer {std::uintptr_t caller;std::size_t bytes,matrix;std::uintptr_t fragment;SourceRegister source;};
inline constexpr std::array<Producer,3> producers{{{0x01C099AD,0x110,0x60,0x01C097EC,SourceRegister::rdi},{0x01C0B28D,0x50,0x10,0x01C0B180,SourceRegister::rsi},{0x01C6588B,0x50,0x10,0x01C6563D,SourceRegister::rbx}}};
inline const Producer* producer(const engine_camera::TemporalContract* c,std::uintptr_t caller) noexcept {
    if(c!=&engine_camera::remastered_1048522)return nullptr;
    for(const auto& p:producers)if(p.caller==caller)return &p;
    return nullptr;
}
inline bool source_matches(const Producer* p,std::uintptr_t descriptor,std::uintptr_t rdi,std::uintptr_t rsi,std::uintptr_t rbx) noexcept {
    if(!p || !descriptor)return false;
    bool known=false;for(const auto& item:producers)known|=p==&item;if(!known)return false;
    const auto actual=p->source==SourceRegister::rdi ? rdi : p->source==SourceRegister::rsi ? rsi : rbx;
    return actual==descriptor;
}
struct Snapshot {
    std::uint8_t valid{};
    Matrix observed{},current_projection{},previous_projection{},current_view{},previous_view{};
    std::array<float,2> center{},previous_center{};
    bool operator==(const Snapshot&)const=default;
};
inline bool read(std::span<const std::uint8_t> camera,Snapshot& out) noexcept {
    if(camera.size()<0x5E0)return false;
    Snapshot s;s.valid=camera[0x530];
    for(auto entry:std::array<std::pair<std::size_t,Matrix*>,5>{{{0x400,&s.observed},{0x440,&s.current_projection},{0x480,&s.previous_projection},{0x40,&s.current_view},{0x560,&s.previous_view}}})
        std::memcpy(entry.second->data(),camera.data()+entry.first,64);
    std::memcpy(s.center.data(),camera.data()+0x520,8);std::memcpy(s.previous_center.data(),camera.data()+0x5C0,8);
    out=s;return true;
}
inline DoubleMatrix multiply(const DoubleMatrix& a,const DoubleMatrix& b) noexcept {
    DoubleMatrix o{};for(unsigned i=0;i<4;++i)for(unsigned j=0;j<4;++j)for(unsigned k=0;k<4;++k)o[i*4+j]+=a[i*4+k]*b[k*4+j];return o;
}
inline bool inverse(const DoubleMatrix& m,DoubleMatrix& out) noexcept {
    std::array<std::array<double,8>,4> a{};
    for(unsigned i=0;i<4;++i)for(unsigned j=0;j<4;++j){if(!std::isfinite(m[i*4+j]))return false;a[i][j]=m[i*4+j];a[i][4+j]=i==j;}
    for(unsigned i=0;i<4;++i){unsigned p=i;for(unsigned j=i+1;j<4;++j)if(std::fabs(a[j][i])>std::fabs(a[p][i]))p=j;
        if(std::fabs(a[p][i])<1e-12)return false;std::swap(a[p],a[i]);const double scale=a[i][i];for(auto& x:a[i])x/=scale;
        for(unsigned j=0;j<4;++j)if(j!=i){const double factor=a[j][i];for(unsigned k=0;k<8;++k)a[j][k]-=factor*a[i][k];}}
    DoubleMatrix staged{};for(unsigned i=0;i<4;++i)for(unsigned j=0;j<4;++j){const auto x=a[i][4+j];if(!std::isfinite(x))return false;staged[i*4+j]=x;}out=staged;return true;
}
inline DoubleMatrix widen(const Matrix& m) noexcept {DoubleMatrix d{};std::copy(m.begin(),m.end(),d.begin());return d;}
inline bool consistent(const Snapshot& s) noexcept {
    if(s.valid!=1 || std::memcmp(s.center.data(),s.previous_center.data(),8))return false;
    for(auto c:s.center)if(!std::isfinite(c))return false;
    for(const auto* m:std::array<const Matrix*,5>{&s.observed,&s.current_projection,&s.previous_projection,&s.current_view,&s.previous_view})for(float x:*m)if(!std::isfinite(x))return false;
    // Native 400 is built BEFORE center/jitter, in reversed clip-depth space.
    const DoubleMatrix flip{1,0,0,0,0,1,0,0,0,0,-1,0,0,0,1,1};
    DoubleMatrix inverse_projection{},inverse_view{};
    if(!inverse(multiply(widen(s.current_projection),flip),inverse_projection) || !inverse(widen(s.current_view),inverse_view))return false;
    const auto expected=multiply(multiply(multiply(inverse_projection,inverse_view),widen(s.previous_view)),multiply(widen(s.previous_projection),flip));
    for(unsigned i=0;i<16;++i){const double e=expected[i],v=s.observed[i];if(!std::isfinite(e) || std::fabs(v-e)>2e-6+4e-6*std::max(std::fabs(v),std::fabs(e)))return false;}
    return true;
}
inline bool corrected(const Snapshot& s,Matrix& out) noexcept {
    if(!consistent(s))return false;
    DoubleMatrix from{1,0,0,0,0,1,0,0,0,0,1,0,-s.center[0],-s.center[1],0,1};
    DoubleMatrix to{1,0,0,0,0,1,0,0,0,0,1,0,s.previous_center[0],s.previous_center[1],0,1};
    const auto result=multiply(multiply(from,widen(s.observed)),to);Matrix staged{};
    for(unsigned i=0;i<16;++i){staged[i]=static_cast<float>(result[i]);if(!std::isfinite(staged[i]))return false;}out=staged;return true;
}
// Copies only the three independently classified packets. No Native object,
// resource ID, descriptor or camera is written; caller owns the output buffer.
inline bool prepare(const engine_camera::TemporalContract* c,std::uintptr_t caller,const Snapshot& s,
    std::span<const std::uint8_t> packet,std::span<std::uint8_t> out) noexcept {
    const auto* p=producer(c,caller);Matrix value{};
    if(!p || packet.size()!=p->bytes || out.size()<p->bytes ||
        std::memcmp(packet.data()+p->matrix,s.observed.data(),64) || !corrected(s,value))return false;
    // Stage all bytes first: refusal and source/output aliases preserve input.
    std::array<std::uint8_t,0x110> staged{};std::memcpy(staged.data(),packet.data(),p->bytes);
    std::memcpy(staged.data()+p->matrix,value.data(),64);std::memmove(out.data(),staged.data(),p->bytes);return true;
}
} // namespace w3vr::effect_reprojection
