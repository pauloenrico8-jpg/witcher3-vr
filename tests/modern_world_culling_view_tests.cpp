#include "modern_world_culling_view.h"
#include <cstdio>
#include <thread>

namespace world=w3vr::world_culling_view;
namespace cam=w3vr::engine_camera;
namespace core=w3vr::render_core;
namespace {
int checks{},failures{},copies{},consumers{};
void require(bool ok,const char* why){++checks;if(!ok){++failures;std::fprintf(stderr,"FAIL: %s\n",why);}}
using Bytes=std::array<std::uint8_t,world::camera_bytes>;
using Matrix=std::array<float,16>;
struct alignas(16) Frame {std::array<std::uint8_t,world::secondary_offset+world::camera_bytes> data{};};
void put(Bytes& b,std::size_t offset,float value){std::memcpy(b.data()+offset,&value,4);}
Bytes camera(bool primary,int eye){
    Bytes b{};const float x=primary?(eye?0.032f:-0.032f):0.7f;
    put(b,0,x);put(b,4,2);put(b,8,12);put(b,0x1C,primary?100:75);
    put(b,0x28,1.5f);put(b,0x2C,1);put(b,0x30,.2f);put(b,0x34,300);
    const float left=primary?(eye?-.65f:-1.25f):-.6f,right=primary?(eye?1.25f:.65f):.6f;
    Matrix p{};p[0]=2/(right-left);p[5]=2/1.8f;p[8]=-(right+left)/(right-left);
    p[9]=-.2f/1.8f;p[10]=300/299.8f;p[11]=1;p[14]=-.2f*300/299.8f;
    Matrix v{1,0,0,0,0,1,0,0,0,0,1,0,-x,-2,-12,1};
    std::memcpy(b.data()+0x180,p.data(),sizeof(p));std::memcpy(b.data()+0x40,v.data(),sizeof(v));
    put(b,0x520,p[8]);put(b,0x524,p[9]);b.back()=primary?0xA1:0xB2;return b;
}
world::Context ctx(Frame& frame,int eye=0){const auto address=reinterpret_cast<std::uintptr_t>(frame.data.data());
    return {0x400000,0x300000,address,{address,7,55,eye,true,true},true};}
const float* copied_source{};float* copied_destination{};float* copy_return{};
float* copy_spy(float* destination,const float* source){++copies;copied_source=source;copied_destination=destination;
    if(destination&&source)std::memcpy(destination,source,world::camera_bytes);return copy_return;}
std::array<double,4> mul(std::array<double,4> a,const Matrix& m){std::array<double,4> b{};
    for(int c=0;c<4;++c)for(int r=0;r<4;++r)b[c]+=a[r]*m[r*4+c];return b;}
bool inside(const Bytes& b,std::array<double,4> position){Matrix view{},p{};
    std::memcpy(view.data(),b.data()+0x40,sizeof(view));std::memcpy(p.data(),b.data()+0x180,sizeof(p));
    const auto clip=mul(mul(position,view),p);const double w=clip[3],e=2e-5;
    return w>0&&clip[0]>=-w-e&&clip[0]<=w+e&&clip[1]>=-w-e&&clip[1]<=w+e&&clip[2]>=-e&&clip[2]<=w+e;}
void geometry_and_native_copy_arguments(){
    for(int eye:{0,1}){
        Frame frame{};const auto primary=camera(true,eye),secondary=camera(false,eye);
        std::memcpy(frame.data.data()+world::primary_offset,primary.data(),primary.size());
        std::memcpy(frame.data.data()+world::secondary_offset,secondary.data(),secondary.size());
        const auto before=frame.data;const auto context=ctx(frame,eye);Bytes destination{};
        const auto dst=reinterpret_cast<std::uintptr_t>(destination.data());
        const auto original=context.frame+world::secondary_offset;std::uintptr_t selected{};
        require(world::select_primary(&cam::remastered_500c,context,7,world::camera_copy_return,dst,
            original,primary,secondary,selected),"world private copy not selected");
        require(selected==context.frame+world::primary_offset,"snapshot address used instead of native primary address");
        copies=0;copy_return=reinterpret_cast<float*>(0x12345);
        require(world::invoke_copy(copy_spy,reinterpret_cast<float*>(dst),reinterpret_cast<const float*>(original),selected)==copy_return,
            "native copy return replaced");
        require(copies==1&&copied_destination==reinterpret_cast<float*>(dst)&&
            copied_source==reinterpret_cast<const float*>(selected),"native copy arguments/count changed");
        require(destination==primary&&frame.data==before,"wrong camera copied or source mutated");
        const double left=eye?-.65:-1.25,right=eye?1.25:.65;int lost{};
        for(double x:{left+1e-5,0.,right-1e-5})for(double y:{-.8+1e-5,0.,1.-1e-5}){
            const std::array<double,4> point{x*5+(eye?.032:-.032),y*5+2,17,1};
            require(inside(destination,point),"copied eye lens loses an intended boundary");
            if(!inside(secondary,point))++lost;
        }
        require(lost>0,"secondary fixture has no real lost boundary");
        require(!inside(destination,{(left-.03)*5+(eye?.032:-.032),2,17,1})&&
            !inside(destination,{(right+.03)*5+(eye?.032:-.032),2,17,1}),"outside rays admitted");
    }
}
void rejecting_sources(){
    Frame frame{};const auto primary=camera(true,0),secondary=camera(false,0);Bytes destination{};
    const auto original=ctx(frame);const auto dst=reinterpret_cast<std::uintptr_t>(destination.data());
    for(int failure=0;failure<18;++failure){
        auto c=original;auto contract=&cam::remastered_500c;std::uint32_t generation=7;
        std::uintptr_t caller=world::camera_copy_return,d=dst,source=c.frame+world::secondary_offset;
        std::span<const std::uint8_t> p=primary,s=secondary;
        switch(failure){case 0:contract=nullptr;break;case 1:contract=&cam::legacy_404;break;
        case 2:c.normal_task=false;break;case 3:c.manager=0;break;case 4:c.scene=0;break;
        case 5:c.frame+=16;break;case 6:generation=8;break;case 7:c.label.eye=2;break;
        case 8:c.label.normal_factory_lineage=false;break;case 9:c.label.view_valid=false;break;
        case 10:caller=0x324460;break;case 11:source+=16;break;case 12:d=c.frame+world::primary_offset;break;
        case 13:d=c.frame+world::secondary_offset-16;break;case 14:d=UINTPTR_MAX-8;break;
        case 15:p=p.first(p.size()-1);break;case 16:s=s.first(s.size()-1);break;
        case 17:c.frame=c.label.frame=UINTPTR_MAX-8;break;}
        std::uintptr_t out=0xBAD;
        require(!world::select_primary(contract,c,generation,caller,d,source,p,s,out)&&out==0xBAD,"invalid source admitted or output changed");
    }
    for(auto offset:{0x1Cu,0x28u,0x2Cu,0x30u,0x520u,0x524u}){auto bad=primary;
        put(bad,offset,std::numeric_limits<float>::quiet_NaN());std::uintptr_t out=0xBAD;
        require(!world::select_primary(&cam::remastered_500c,original,7,world::camera_copy_return,dst,
            original.frame+world::secondary_offset,bad,secondary,out)&&out==0xBAD,"nonfinite primary admitted");}
    copies=0;copy_return=nullptr;copied_source=reinterpret_cast<const float*>(0x1);
    require(world::invoke_copy(copy_spy,nullptr,nullptr,0)==nullptr&&copies==1&&copied_source==nullptr&&copied_destination==nullptr,
        "fallback null arguments/return/count changed");
    for(int failure=0;failure<9;++failure){auto c=original;auto contract=&cam::remastered_500c;
        std::uintptr_t caller=world::consumer_return,manager=c.manager,scene=c.scene,frame_address=c.frame,observed=manager;
        std::uint32_t generation=7;switch(failure){case 0:contract=nullptr;break;case 1:contract=&cam::legacy_404;break;
        case 2:caller+=1;break;case 3:manager=0;break;case 4:observed+=16;break;case 5:scene=0;break;
        case 6:frame_address+=16;break;case 7:generation=8;break;case 8:c.label.pair=0;break;}
        world::Context out{};out.frame=0xBAD;
        require(!world::bind(contract,caller,manager,scene,frame_address,c.label,generation,observed,out)&&out.frame==0xBAD,
            "invalid parent binding admitted");
    }
}
thread_local world::Context current{};
world::Context read_context(){return current;}
void apply_context(const world::Context& c){current=c;}
world::Context expected{};bool nested{},throw_probe{};
void* expected_manager{};void* expected_scene{};void* expected_frame{};
void consumer_spy(void* manager,void* scene,void* frame){++consumers;
    require(current.frame==expected.frame&&current.label.eye==expected.label.eye&&current.normal_task==expected.normal_task,
        "consumer inherits incorrect frame/eye authority");
    require(manager==expected_manager&&scene==expected_scene&&frame==expected_frame,"consumer original arguments changed");
    if(nested){nested=false;const auto outer=current;expected={};expected_manager=expected_scene=expected_frame=nullptr;
        world::invoke(consumer_spy,nullptr,nullptr,nullptr,nullptr,read_context,apply_context);
        require(current.frame==outer.frame&&current.label.eye==outer.label.eye,"unknown nested consumer lost outer eye");}
    if(throw_probe){throw_probe=false;throw 23;}
}
void scopes_and_thread_boundary(){Frame frame{};auto c=ctx(frame,1);world::Context bound{};
    require(world::bind(&cam::remastered_500c,world::consumer_return,c.manager,c.scene,c.frame,c.label,7,c.manager,bound),
        "valid actual native frame/scene parent rejected");
    current=ctx(frame,0);const auto outer=current;expected=bound;
    expected_manager=reinterpret_cast<void*>(c.manager);expected_scene=reinterpret_cast<void*>(c.scene);expected_frame=reinterpret_cast<void*>(c.frame);
    consumers=0;world::invoke(consumer_spy,expected_manager,expected_scene,expected_frame,&bound,read_context,apply_context);
    require(consumers==1&&current.label.eye==0,"accepted parent failed to restore outer eye");
    nested=true;expected=bound;consumers=0;
    world::invoke(consumer_spy,expected_manager,expected_scene,expected_frame,&bound,read_context,apply_context);
    require(consumers==2&&current.label.eye==0,"nested parent count/restore failed");
    expected=bound;expected_manager=reinterpret_cast<void*>(c.manager);expected_scene=reinterpret_cast<void*>(c.scene);expected_frame=reinterpret_cast<void*>(c.frame);
    throw_probe=true;bool caught{};try{world::invoke(consumer_spy,expected_manager,expected_scene,expected_frame,&bound,read_context,apply_context);}
    catch(int error){caught=error==23;}require(caught&&current.label.eye==outer.label.eye,"CPU exception propagation/restoration changed");
    bool separate_thread{};std::thread worker([&]{separate_thread=current.frame==0&&!current.normal_task;});worker.join();
    require(separate_thread&&current.frame==outer.frame,"worker borrowed another thread's context");
}
}
int main(){geometry_and_native_copy_arguments();rejecting_sources();scopes_and_thread_boundary();
    std::printf("World culling primary camera: %d checks, %d failures (CPU only)\n",checks,failures);return failures?1:0;}
