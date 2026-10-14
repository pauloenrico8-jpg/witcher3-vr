#include "modern_xr_graphics_binding.h"
#include <cstdlib>
#include <iostream>
#include <string>
namespace g=w3vr::modern_graphics_binding;
namespace xr=w3vr::modern_xr_graphics_binding;
using Microsoft::WRL::ComPtr;
unsigned checks{};
void require(bool value,const char* why){++checks;if(!value){std::cerr<<why<<'\n';std::exit(1);}}
HRESULT resolve(IUnknown* object,REFIID iid,void** out)noexcept{return object?object->QueryInterface(iid,out):E_POINTER;}
struct Fixture {
    ComPtr<ID3D12Device> device;
    ComPtr<ID3D12CommandQueue> queue,other_queue;
    ComPtr<IDXGIFactory4> factory;
    ComPtr<IDXGISwapChain1> chain;
    HWND window{};
    g::Registry graphics{resolve};
    Fixture(){
        // Absolute system paths and dependency search exclude the mod/SDK.
        wchar_t system[MAX_PATH]{};
        require(GetSystemDirectoryW(system,MAX_PATH)>0,"System directory missing");
        const std::wstring directory=std::wstring(system)+L"\\";
        auto dxgi=LoadLibraryExW((directory+L"dxgi.dll").c_str(),nullptr,LOAD_LIBRARY_SEARCH_SYSTEM32);
        auto d3d=LoadLibraryExW((directory+L"d3d12.dll").c_str(),nullptr,LOAD_LIBRARY_SEARCH_SYSTEM32);
        require(dxgi && d3d,"Official system graphics modules missing");
        auto create_device=reinterpret_cast<decltype(&D3D12CreateDevice)>(GetProcAddress(d3d,"D3D12CreateDevice"));
        auto create_factory=reinterpret_cast<decltype(&CreateDXGIFactory1)>(GetProcAddress(dxgi,"CreateDXGIFactory1"));
        require(create_device && create_factory,"System graphics exports missing");
        // Keep module handles until this own process exits; COM objects retain
        // their Native vtables. This test does not initialize the mod or SDK.
        require(SUCCEEDED(create_device(nullptr,D3D_FEATURE_LEVEL_11_0,IID_PPV_ARGS(&device))),"Own D3D12 device creation failed");
        D3D12_COMMAND_QUEUE_DESC q{};q.Type=D3D12_COMMAND_LIST_TYPE_DIRECT;
        require(SUCCEEDED(device->CreateCommandQueue(&q,IID_PPV_ARGS(&queue))) &&
            SUCCEEDED(device->CreateCommandQueue(&q,IID_PPV_ARGS(&other_queue))),"Own direct queues failed");
        require(SUCCEEDED(create_factory(IID_PPV_ARGS(&factory))),"Own DXGI factory failed");
        window=CreateWindowExW(0,L"STATIC",L"Own XR graphics binding test",WS_OVERLAPPEDWINDOW,
            0,0,32,32,nullptr,nullptr,GetModuleHandleW(nullptr),nullptr);
        require(window!=nullptr,"Own hidden window failed");
        DXGI_SWAP_CHAIN_DESC1 desc{};desc.Width=16;desc.Height=16;desc.Format=DXGI_FORMAT_R8G8B8A8_UNORM;
        desc.SampleDesc.Count=1;desc.BufferUsage=DXGI_USAGE_RENDER_TARGET_OUTPUT;
        desc.BufferCount=2;desc.SwapEffect=DXGI_SWAP_EFFECT_FLIP_DISCARD;
        require(SUCCEEDED(factory->CreateSwapChainForHwnd(queue.Get(),window,&desc,nullptr,nullptr,&chain)),"Own swapchain failed");
        require(graphics.publish_factory(S_OK,queue.Get(),chain.Get()),"Real own graphics binding not published");
    }
    ~Fixture(){chain.Reset();if(window)DestroyWindow(window);}
};
void exact_session(Fixture& f){
    auto graphics=f.graphics.acquire();require(bool(graphics),"Own snapshot absent");
    xr::Registry session;xr::Ticket ticket{},sentinel{graphics,44,55};auto out=sentinel;
    require(!session.begin(graphics,1,false,out) && out.instance==44 && out.nonce==55,"Unknown Native proof admitted or changed ticket");
    require(!session.begin(graphics,0,true,out) && !session.begin({},1,true,out),"Null instance or graphics admitted");
    require(session.begin(graphics,1,true,ticket),"Known own session endpoints not staged");
    require(ticket.graphics->queue.Get()==f.queue.Get() && ticket.graphics->device.Get()==f.device.Get(),"Forwarded session endpoints changed");
    require(!session.acquire(1,2,graphics,true) && !session.begin(graphics,1,true,out),"Create-only or retry admitted");
    auto wrong=ticket;wrong.nonce++;require(!session.complete(wrong,true,2,graphics,true),"Foreign completion admitted");
    require(session.complete(ticket,true,2,graphics,true),"Exact completion failed");
    auto fixed=session.acquire(1,2,graphics,true);require(fixed && fixed->graphics.get()==graphics.get(),"Session did not retain creation graph");
    require(!session.complete(ticket,true,2,graphics,true),"Completion callback repeated");
    require(!session.acquire(3,2,graphics,true) && !session.acquire(1,3,graphics,true) &&
        !session.acquire(1,2,graphics,false),"Other instance/session or unknown endpoint admitted");
    auto resized=std::make_shared<g::Data>(*graphics);resized->epoch++;resized->width+=8;
    require(session.current(fixed,1,2,resized,true),"Geometry-only snapshot incorrectly retargeted session");
    auto other=std::make_shared<g::Data>(*resized);other->queue=f.other_queue;other->queue_identity=g::identity(f.other_queue.Get());
    require(other->device.Get()==resized->device.Get() && other->queue.Get()!=resized->queue.Get(),"Own queue isolation fixture invalid");
    require(!session.acquire(1,2,other,true),"Same device accepted different actual direct queue");
    auto missing=std::make_shared<g::Data>(*resized);missing->present_count=0;
    require(!session.acquire(1,2,missing,true),"Unknown Resize1 queue array admitted");
    auto changed_chain=std::make_shared<g::Data>(*resized);changed_chain->chain_identity=g::identity(f.other_queue.Get());
    require(!session.acquire(1,2,changed_chain,true),"Auxiliary chain retargeted session");
    xr::Registry other_session;xr::Ticket other_ticket{};
    require(other_session.begin(graphics,1,true,other_ticket) && other_session.complete(other_ticket,true,2,graphics,true),"Second own fixture failed");
    auto foreign=other_session.acquire(1,2,graphics,true);require(!session.current(foreign,1,2,graphics,true),"Copied/equal foreign session snapshot admitted");
    session.stop_no_release();require(!session.current(fixed,1,2,graphics,true) &&
        fixed->graphics->queue.Get()==f.queue.Get(),"Revocation admitted submission or released borrowed snapshot");
    require(!session.begin(graphics,1,true,out),"Revoked root retargeted or retried");
}
void sticky_failure(Fixture& f){
    auto original=f.graphics.acquire();
    for(unsigned fault=0;fault<6;++fault){
        xr::Registry session;xr::Ticket ticket{};require(session.begin(original,1,true,ticket),"Failure case begin failed");
        g::Snapshot current=original;auto changed=std::make_shared<g::Data>(*original);
        if(fault==2)current={};if(fault==3)current=changed;if(fault==5)session.stop_no_release();
        require(!session.complete(ticket,fault!=0,fault==1?0:2,current,fault!=4),"Incomplete create result admitted");
        require(!session.complete(ticket,true,2,original,true) && !session.acquire(1,2,original,true),"Failure rearmed through later callback");
    }
    std::weak_ptr<const g::Data> weak;
    {
        xr::Registry session;auto private_copy=std::make_shared<g::Data>(*original);weak=private_copy;
        xr::Ticket ticket{};require(session.begin(private_copy,1,true,ticket),"Retention stage failed");
        require(!session.complete(ticket,false,2,private_copy,true),"Failed Native return admitted");
        ticket={};private_copy.reset();require(!weak.expired(),"Failed construction dropped retained graph");
        session.stop_no_release();require(!weak.expired(),"Atomic revocation released interfaces");
    }
    require(weak.expired(),"Own fully stopped test fixture leaked after normal destruction");
}
int main(){Fixture own;exact_session(own);sticky_failure(own);
    std::cout<<checks<<" checks; real own D3D12 device, two direct queues and hidden DXGI swapchain. "
        "XR handles/results and Native class admission are fabricated; no OpenXR runtime, SDK, Witcher or Quest executed.\n";
}
