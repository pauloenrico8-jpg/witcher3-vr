#include "modern_vegetation_view.h"
#include <cstdio>
#include <limits>

namespace veg=w3vr::modern_vegetation_view;
namespace core=w3vr::render_core;
namespace cam=w3vr::engine_camera;
namespace {
int checks{},failures{},calls{};
void require(bool value,const char* why) {++checks;if(!value){++failures;std::fprintf(stderr,"FAIL: %s\n",why);}}
using Bytes=std::array<std::uint8_t,veg::camera_bytes>;
using Matrix=std::array<float,16>;
Matrix projection(float left,float right,float down,float up,float near,float far,bool reverse_depth) {
    Matrix p{};p[0]=2/(right-left);p[5]=2/(up-down);
    p[8]=-(right+left)/(right-left);p[9]=-(up+down)/(up-down);p[11]=1;
    if(reverse_depth){p[10]=-near/(far-near);p[14]=near*far/(far-near);}
    else {p[10]=far/(far-near);p[14]=-near*far/(far-near);}
    return p;
}
Matrix view(float x,float y,float z,float yaw) {
    const float c=std::cos(yaw),s=std::sin(yaw);
    return {c,0,s,0, 0,1,0,0, -s,0,c,0, -x*c+z*s,-y,-x*s-z*c,1};
}
Bytes camera(bool primary,int eye) {
    Bytes bytes{};const float x=primary?(eye==0?-.032f:.032f):.7f;
    const float y=primary?2.f:1.5f,z=primary?12.f:12.2f;
    const float xyz[]{x,y,z};std::memcpy(bytes.data(),xyz,sizeof(xyz));
    const float fov=primary?100.f:75.f;std::memcpy(bytes.data()+0x1C,&fov,4);
    const float lens[]{1.5f,1.f,primary?.2f:.1f,300.f};std::memcpy(bytes.data()+0x28,lens,sizeof(lens));
    const auto v=view(x,y,z,primary?.3f:.35f);std::memcpy(bytes.data()+0x40,v.data(),sizeof(v));
    const float left=primary?(eye==0?-1.25f:-.65f):-.6f;
    const float right=primary?(eye==0?.65f:1.25f):.6f;
    for(const auto route:{veg::Route::ordinary_projection,veg::Route::renderer_projection}) {
        const auto p=projection(left,right,-.8f,1.f,lens[2],lens[3],route==veg::Route::renderer_projection);
        std::memcpy(bytes.data()+veg::projection_offset(route),p.data(),sizeof(p));
    }
    return bytes;
}
veg::Context context(int eye=0) {return {0x100000,0x200000,0x300000,{0x200000,7,55,eye,true,true},true};}
struct CallRecord {
    void* receiver{};const float* position{};const float* projection{};const float* view{};
    veg::Inputs copied{};std::uint32_t far_bits{};bool mode{},result{};
} record;
bool native_spy(void* receiver,const float* position,const float* projection,const float* view,
    float near,float far,bool mode) {
    ++calls;record.receiver=receiver;record.position=position;record.projection=projection;record.view=view;
    if(position)std::memcpy(record.copied.position.data(),position,sizeof(record.copied.position));
    if(projection)std::memcpy(record.copied.projection.data(),projection,sizeof(record.copied.projection));
    if(view)std::memcpy(record.copied.view.data(),view,sizeof(record.copied.view));
    record.copied.near_range=near;std::memcpy(&record.far_bits,&far,4);record.mode=mode;
    return record.result;
}
// An independent row-matrix clip model, not an implementation or execution of
// the native SpeedTree plane/cache algorithm. It consumes the callback's actual
// copied matrices and checks that the eye's boundary points reach that callback.
std::array<double,4> multiply(std::array<double,4> v,const Matrix& m) {
    std::array<double,4> out{};for(int c=0;c<4;++c)for(int r=0;r<4;++r)out[c]+=v[r]*m[r*4+c];return out;
}
bool inside(const veg::Inputs& in,std::array<double,4> point) {
    const auto clip=multiply(multiply(point,in.view),in.projection);
    const double w=clip[3],epsilon=2e-4;
    return w>0 && clip[0]>=-w-epsilon && clip[0]<=w+epsilon &&
        clip[1]>=-w-epsilon && clip[1]<=w+epsilon && clip[2]>=-epsilon && clip[2]<=w+epsilon;
}
std::array<double,4> world_point(int eye,double tangent_x,double tangent_y,double depth) {
    const double x=tangent_x*depth,y=tangent_y*depth,c=std::cos(.3),s=std::sin(.3);
    return {x*c+depth*s+(eye==0?-.032:.032),y+2,-x*s+depth*c+12,1};
}
void test_routing_and_boundary_geometry() {
    for(int eye:{0,1})for(int variant:{0,1}) {
        auto primary=camera(true,eye),secondary=camera(false,eye);const auto before_primary=primary,before_secondary=secondary;
        const auto route=variant?veg::Route::renderer_projection:veg::Route::ordinary_projection;
        const auto caller=variant?0x1C14D79:0x1C14D43;const std::uintptr_t container=0x400000;
        const auto receiver=container+(variant?0x794:0x4BC);veg::Inputs incoming{},output{},expected{};
        require(veg::read_inputs(secondary,route,incoming),"secondary fixture");
        require(veg::read_inputs(primary,route,expected),"primary fixture");
        require(veg::prepare(&cam::remastered_500c,context(eye),7,caller,container,receiver,incoming,false,
            primary,secondary,output),"known core route rejected");
        require(veg::same(output,expected),"primary position/view/projection/near not routed");
        record={};calls=0;record.result=eye==0;const float grid_far=4096.f;
        require(veg::invoke(native_spy,reinterpret_cast<void*>(receiver),incoming.position.data(),
            incoming.projection.data(),incoming.view.data(),incoming.near_range,grid_far,false,&output)==record.result,
            "native boolean result changed");
        require(calls==1 && record.receiver==reinterpret_cast<void*>(receiver) && !record.mode,"original call contract changed");
        std::uint32_t bits{};std::memcpy(&bits,&grid_far,4);
        require(record.far_bits==bits,"grid far replaced with camera far");
        require(veg::same(record.copied,expected),"callback got secondary or wrong variant");
        const double left=eye==0?-1.25:-.65,right=eye==0?.65:1.25;
        int missing_with_secondary{};
        for(double x:{left+1e-5,0.,right-1e-5})for(double y:{-.8+1e-5,0.,1.-1e-5}) {
            const auto point=world_point(eye,x,y,5);
            require(inside(record.copied,point),"eye boundary vanished with actual routed inputs");
            if(!inside(incoming,point))++missing_with_secondary;
        }
        // The two frusta overlap; require a real lost boundary, not that the
        // old camera rejects every point including the shared central region.
        require(missing_with_secondary>0,"fixture has no boundary lost with the secondary camera");
        require(!inside(record.copied,world_point(eye,left-.02,0,5)) &&
            !inside(record.copied,world_point(eye,right+.02,0,5)),"outside horizontal rays were admitted");
        require(primary==before_primary && secondary==before_secondary,"camera bytes mutated");
        primary.fill(0xA5);secondary.fill(0xA5);
        veg::invoke(native_spy,reinterpret_cast<void*>(receiver),incoming.position.data(),incoming.projection.data(),
            incoming.view.data(),incoming.near_range,grid_far,false,&output);
        require(calls==2 && veg::same(record.copied,expected),"owned inputs depended on source buffer after preparation");
    }
}
void test_rejections_preserve_output() {
    const auto primary=camera(true,0),secondary=camera(false,0);veg::Inputs incoming{};
    veg::read_inputs(secondary,veg::Route::ordinary_projection,incoming);
    for(int failure=0;failure<20;++failure) {
        auto ctx=context();auto p=primary,s=secondary;auto original=incoming;
        auto contract=&cam::remastered_500c;std::uint32_t generation=7;
        std::uintptr_t caller=0x1C14D43,container=0x400000,receiver=0x4004BC;bool mode=false;
        std::span<const std::uint8_t> pspan=p,sspan=s;
        switch(failure) {
        case 0:contract=&cam::legacy_404;break;case 1:contract=nullptr;break;
        case 2:ctx.normal_core=false;break;case 3:ctx.scene=0;break;
        case 4:ctx.frame+=16;break;case 5:ctx.label.normal_factory_lineage=false;break;
        case 6:generation=8;break;case 7:ctx.label.eye=2;break;
        case 8:ctx.label.pair=UINT64_MAX;break;case 9:ctx.label.view_valid=false;break;
        case 10:caller=0x1CE6345;break;case 11:receiver+=16;break;
        case 12:container=UINTPTR_MAX-8;break;case 13:mode=true;break;
        case 14:original.position[0]+=.1f;break;case 15:original.projection[8]+=.1f;break;
        case 16:original.view[12]+=.1f;break;case 17:original.near_range+=.1f;break;
        case 18:pspan=pspan.first(pspan.size()-1);break;
        case 19:sspan=sspan.first(sspan.size()-1);break;
        }
        veg::Inputs out=incoming,saved=out;
        require(!veg::prepare(contract,ctx,generation,caller,container,receiver,original,mode,pspan,sspan,out),"invalid route admitted");
        require(veg::same(out,saved),"rejection published a partial replacement");
    }
    for(std::size_t offset:{0u,0x30u,0x40u,0x180u,0x340u}) {
        auto p=primary;const float bad=std::numeric_limits<float>::quiet_NaN();std::memcpy(p.data()+offset,&bad,4);
        const auto route=offset==0x340?veg::Route::renderer_projection:veg::Route::ordinary_projection;
        auto original=incoming;if(route==veg::Route::renderer_projection)veg::read_inputs(secondary,route,original);
        veg::Inputs out=incoming,saved=out;
        require(!veg::prepare(&cam::remastered_500c,context(),7,route==veg::Route::renderer_projection?0x1C14D79:0x1C14D43,
            0x400000,route==veg::Route::renderer_projection?0x400794:0x4004BC,original,false,p,secondary,out),"nonfinite source admitted");
        require(veg::same(out,saved),"nonfinite source changed output");
    }
    // NULL/unknown inputs and unusual native far float words still pass intact
    // in the fallback: the adapter does not reinterpret them or manufacture success.
    record={};calls=0;const std::uint32_t far_bits=0x7FC12345;float far{};std::memcpy(&far,&far_bits,4);
    require(!veg::invoke(native_spy,reinterpret_cast<void*>(0x123),nullptr,nullptr,nullptr,-1,far,true,nullptr),"fallback changed false result");
    require(calls==1 && record.position==nullptr && record.projection==nullptr && record.view==nullptr &&
        record.copied.near_range==-1 && record.far_bits==far_bits && record.mode,"fallback altered native arguments");
}
veg::Context current_context{};
veg::Context read_context(){return current_context;}
void apply_context(const veg::Context& c){current_context=c;}
bool expect_context{};
void core_probe(void*,void*,void*) {
    require(veg::accepts(&cam::remastered_500c,current_context,7)==expect_context,"CPU scope inherited wrong authority");
}
void test_nested_core_masks_and_restores() {
    current_context=context(1);const auto outer=current_context;
    expect_context=false;
    core::invoke(&cam::remastered_500c,0xBAD,core_probe,reinterpret_cast<void*>(0x100000),
        reinterpret_cast<void*>(0x200000),reinterpret_cast<void*>(0x300000),&outer.label,7,
        context(0),veg::Context{},read_context,apply_context);
    require(current_context.label.eye==1 && current_context.frame==outer.frame,"rejected nested core lost outer context");
    expect_context=true;
    core::invoke(&cam::remastered_500c,core::remastered_500c.normal_return,core_probe,
        reinterpret_cast<void*>(0x100000),reinterpret_cast<void*>(0x200000),reinterpret_cast<void*>(0x300000),
        &outer.label,7,context(0),veg::Context{},read_context,apply_context);
    require(current_context.label.eye==1,"accepted nested core did not restore outer eye");
}
}
int main(){test_routing_and_boundary_geometry();test_rejections_preserve_output();test_nested_core_masks_and_restores();
    std::printf("Modern vegetation eye routing: %d checks, %d failures (CPU only)\n",checks,failures);return failures?1:0;}
