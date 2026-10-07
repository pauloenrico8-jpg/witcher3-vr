#include "modern_dlss_native_hooks.h"
#include <MinHook.h>
#include <atomic>
#include <mutex>

namespace w3vr::modern_dlss_native_hooks {
namespace {
namespace m = modern_dlss_ownership;
using Close = HRESULT (STDMETHODCALLTYPE*)(ID3D12GraphicsCommandList*);
using Reset = HRESULT (STDMETHODCALLTYPE*)(ID3D12GraphicsCommandList*, ID3D12CommandAllocator*, ID3D12PipelineState*);
using Execute = void (STDMETHODCALLTYPE*)(ID3D12CommandQueue*, UINT, ID3D12CommandList* const*);
Close original_close{};
Reset original_reset{};
Execute original_execute{};
std::mutex mutation;
std::atomic<bool> enabled{};
std::atomic<unsigned> entered{};
std::atomic<std::uint64_t> close_count{}, reset_count{}, execute_count{}, before_count{}, after_count{};
thread_local unsigned nesting{};
m::Classify classify{};
Observer observer{};
bool may_have_enabled{};
struct Site { void* target{}; void* detour{}; void** original{}; bool created{}; HMODULE module{}; };
std::array<Site, 3> sites{};
m::OwnedCommand command_anchor;
m::OwnedQueue queue_anchor;
struct Entry {
    bool outer;
    Entry() : outer(nesting++ == 0) { entered.fetch_add(1, std::memory_order_acq_rel); }
    ~Entry() { entered.fetch_sub(1, std::memory_order_acq_rel); --nesting; }
};

void command_event(const m::OwnedCommand& command, Operation operation, Moment moment, HRESULT result) noexcept {
    if (!command || !observer.command) return;
    (moment == Moment::Before ? before_count : after_count).fetch_add(1, std::memory_order_relaxed);
    observer.command(observer.context, command, operation, moment, result);
}
void execute_event(const m::OwnedQueue& queue, Moment moment, UINT count, ID3D12CommandList* const* array) noexcept {
    if (!queue || !observer.execute) return;
    (moment == Moment::Before ? before_count : after_count).fetch_add(1, std::memory_order_relaxed);
    observer.execute(observer.context, queue, moment, count, array);
}
m::OwnedCommand command_proof(ID3D12GraphicsCommandList* command, bool observe) noexcept {
    if (!observe) return {};
    try { return m::acquire_command(command, classify); }
    catch (...) { enabled.store(false, std::memory_order_release); return {}; }
}
HRESULT STDMETHODCALLTYPE close_hook(ID3D12GraphicsCommandList* command) {
    Entry entry;
    const auto proof = command_proof(command, entry.outer && enabled.load(std::memory_order_acquire));
    command_event(proof, Operation::Close, Moment::Before, E_PENDING);
    close_count.fetch_add(1, std::memory_order_relaxed);
    const auto result = original_close(command); // Exactly one original call.
    command_event(proof, Operation::Close, Moment::After, result);
    return result;
}
HRESULT STDMETHODCALLTYPE reset_hook(ID3D12GraphicsCommandList* command, ID3D12CommandAllocator* allocator,
    ID3D12PipelineState* pipeline) {
    Entry entry;
    const auto proof = command_proof(command, entry.outer && enabled.load(std::memory_order_acquire));
    command_event(proof, Operation::Reset, Moment::Before, E_PENDING);
    reset_count.fetch_add(1, std::memory_order_relaxed);
    const auto result = original_reset(command, allocator, pipeline);
    command_event(proof, Operation::Reset, Moment::After, result);
    return result;
}
void STDMETHODCALLTYPE execute_hook(ID3D12CommandQueue* queue, UINT count, ID3D12CommandList* const* array) {
    Entry entry;
    m::OwnedQueue proof;
    if (entry.outer && enabled.load(std::memory_order_acquire)) {
        try { proof = m::acquire_queue(queue, classify); }
        catch (...) { enabled.store(false, std::memory_order_release); }
    }
    execute_event(proof, Moment::Before, count, array);
    execute_count.fetch_add(1, std::memory_order_relaxed);
    original_execute(queue, count, array); // Preserve queue, pointer and count.
    execute_event(proof, Moment::After, count, array);
}
void* method(IUnknown* object, std::size_t slot) noexcept {
    __try { return (*reinterpret_cast<void***>(object))[slot]; }
    __except (EXCEPTION_EXECUTE_HANDLER) { return nullptr; }
}
HMODULE native_module(void* target) {
    HMODULE module{};
    if (!target || !GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS,
        reinterpret_cast<LPCWSTR>(target), &module)) return nullptr;
    wchar_t name[MAX_PATH]{};
    const auto length = GetModuleFileNameW(module, name, MAX_PATH);
    const auto* slash = wcsrchr(name, L'\\');
    const auto* base = slash ? slash + 1 : name;
    if (!length || length == MAX_PATH || (_wcsicmp(base, L"d3d12.dll") != 0 &&
        _wcsicmp(base, L"D3D12Core.dll") != 0)) {
        FreeLibrary(module); return nullptr;
    }
    return module; // Added module reference lives through trampoline removal.
}
bool cleanup(bool quiescent = false) {
    enabled.store(false, std::memory_order_release);
    bool disabled = true;
    for (auto& site : sites) if (site.created) {
        const auto result = MH_DisableHook(site.target);
        disabled = disabled && (result == MH_OK || result == MH_ERROR_DISABLED);
    }
    if (!disabled || entered.load(std::memory_order_acquire) != 0 ||
        (may_have_enabled && !quiescent)) return false;
    bool removed = true;
    for (auto& site : sites) if (site.created) {
        if (MH_RemoveHook(site.target) == MH_OK) { site.created = false; *site.original = nullptr; }
        else removed = false;
    }
    if (!removed) return false;
    for (auto& site : sites) { if (site.module) FreeLibrary(site.module); site = {}; }
    command_anchor = {}; queue_anchor = {}; observer = {}; classify = nullptr; may_have_enabled = false;
    return true;
}
} // namespace
bool install(const m::OwnedCommand& command, const m::OwnedQueue& queue, m::Classify owner, Observer callbacks) noexcept {
    try {
        std::scoped_lock guard(mutation);
        if (!command || !queue || !command.command_identity || !queue.queue_identity ||
            !command.device_identity || command.device_identity.Get() != queue.device_identity.Get() ||
            !owner || owner(command.command.Get()) != m::Owner::Native ||
            owner(queue.queue.Get()) != m::Owner::Native ||
            (!callbacks.command && !callbacks.execute)) return false;
        const std::array<void*, 3> targets{method(command.command.Get(), 9),
            method(command.command.Get(), 10), method(queue.queue.Get(), 10)};
        if (enabled.load(std::memory_order_acquire))
            return targets[0] == sites[0].target && targets[1] == sites[1].target &&
                targets[2] == sites[2].target && owner == classify &&
                callbacks.context == observer.context && callbacks.command == observer.command &&
                callbacks.execute == observer.execute;
        for (const auto& site : sites) if (site.created || site.module) return false; // Retry cleanup first.
        sites = {{{targets[0], reinterpret_cast<void*>(close_hook), reinterpret_cast<void**>(&original_close)},
            {targets[1], reinterpret_cast<void*>(reset_hook), reinterpret_cast<void**>(&original_reset)},
            {targets[2], reinterpret_cast<void*>(execute_hook), reinterpret_cast<void**>(&original_execute)}}};
        for (auto& site : sites) {
            site.module = native_module(site.target);
            if (!site.module) { cleanup(); return false; }
        }
        classify = owner; observer = callbacks; command_anchor = command; queue_anchor = queue;
        for (auto& site : sites) {
            if (MH_CreateHook(site.target, site.detour, site.original) != MH_OK) { cleanup(); return false; }
            site.created = true;
        }
        // Never apply a process-wide pending queue owned by other installers.
        // Callbacks remain closed until ALL three native sites are enabled.
        for (auto& site : sites) {
            may_have_enabled = true;
            if (MH_EnableHook(site.target) != MH_OK) { cleanup(); return false; }
        }
        enabled.store(true, std::memory_order_release);
        return true;
    } catch (...) { enabled.store(false, std::memory_order_release); return false; }
}
bool uninstall(bool quiescent) noexcept {
    try { std::scoped_lock guard(mutation); return cleanup(quiescent); }
    catch (...) { enabled.store(false, std::memory_order_release); return false; }
}
bool ready() noexcept { return enabled.load(std::memory_order_acquire); }
Stats stats() noexcept { return {close_count.load(), reset_count.load(), execute_count.load(), before_count.load(), after_count.load()}; }
} // namespace w3vr::modern_dlss_native_hooks
