#pragma once
#include <windows.h>
#include <array>
#include <atomic>
#include <cstddef>

namespace w3vr::modern_backbuffer_hooks {
enum class Site : std::size_t { Reset, Execute, Close, Barrier, Count };
using Targets=std::array<void*,static_cast<std::size_t>(Site::Count)>;
// Only concrete native targets are receipts. An enabled proxy target does not
// prove that the actual native endpoint reaches our observer. Module identity
// is checked again at admission; this does not pin modules or prove unload.
inline bool native_target(void* target) {
    HMODULE module{};
    if(!target || !GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS |
        GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,reinterpret_cast<LPCWSTR>(target),&module))return false;
    wchar_t path[MAX_PATH]{};
    const auto size=GetModuleFileNameW(module,path,MAX_PATH);
    if(!size || size==MAX_PATH)return false;
    const auto* slash=wcsrchr(path,L'\\');const auto* name=slash?slash+1:path;
    return _wcsicmp(name,L"D3D12Core.dll")==0 || _wcsicmp(name,L"d3d12.dll")==0;
}
class Receipts {
    struct Installed {std::atomic<void*> target{},original{};std::atomic<bool> enabled{};};
    std::array<Installed,static_cast<std::size_t>(Site::Count)> sites_{};
    std::atomic<bool> failed_{};
public:
    // Called only after this exact CreateHook+EnableHook result is known.
    // A pointer filled by CreateHook alone is never an enabled receipt.
    bool publish(Site site,void* target,void* original,bool enabled) {
        const auto index=static_cast<std::size_t>(site);
        if(index>=sites_.size() || !enabled || !original || !native_target(target)) {
            failed_.store(true,std::memory_order_release);return false;
        }
        auto& installed=sites_[index];void* empty{};
        if(!installed.target.compare_exchange_strong(empty,target,std::memory_order_acq_rel) && empty!=target) {
            failed_.store(true,std::memory_order_release);return false;
        }
        empty=nullptr;
        if(!installed.original.compare_exchange_strong(empty,original,std::memory_order_acq_rel) && empty!=original) {
            failed_.store(true,std::memory_order_release);return false;
        }
        installed.enabled.store(true,std::memory_order_release);return !failed_.load(std::memory_order_acquire);
    }
    bool accepts(const Targets& actual)const {
        if(failed_.load(std::memory_order_acquire))return false;
        for(std::size_t i=0;i<sites_.size();++i) {
            if(!sites_[i].enabled.load(std::memory_order_acquire) || !sites_[i].original.load(std::memory_order_acquire) ||
                sites_[i].target.load(std::memory_order_acquire)!=actual[i] || !native_target(actual[i]))return false;
        }
        return !failed_.load(std::memory_order_acquire);
    }
    bool ready()const{Targets actual{};for(std::size_t i=0;i<sites_.size();++i)actual[i]=sites_[i].target.load(std::memory_order_acquire);return accepts(actual);}
    void revoke(){failed_.store(true,std::memory_order_release);}
};
} // namespace w3vr::modern_backbuffer_hooks
