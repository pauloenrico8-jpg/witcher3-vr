#include "modern_dlss_native_hooks.h"
#include <MinHook.h>
#include <atomic>
#include <mutex>

namespace w3vr::modern_dlss_native_hooks {
struct ForwarderData {
    void* target{};
    ResetFn reset{};
    ExecuteFn execute{};
    HMODULE module{};
    std::atomic<bool> active{true}, bound{};
    ~ForwarderData() { if (module) FreeLibrary(module); }
};
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
struct Site {
    void* target{}; void* detour{}; void** original{}; bool created{}; HMODULE module{};
    std::shared_ptr<ForwarderData> forwarded;
};
std::array<Site, 3> sites{};
m::OwnedCommand command_anchor;
m::OwnedQueue queue_anchor;
struct Entry {
    bool outer;
    Entry() : outer(nesting++ == 0) { entered.fetch_add(1, std::memory_order_acq_rel); }
    ~Entry() { entered.fetch_sub(1, std::memory_order_acq_rel); --nesting; }
};
bool observing() noexcept {
    if (!enabled.load(std::memory_order_acquire)) return false;
    for (const auto& site : sites)
        if (site.forwarded && !site.forwarded->active.load(std::memory_order_acquire)) return false;
    return true;
}

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
    const auto proof = command_proof(command, entry.outer && observing());
    command_event(proof, Operation::Close, Moment::Before, E_PENDING);
    close_count.fetch_add(1, std::memory_order_relaxed);
    const auto result = original_close(command); // Exactly one original call.
    command_event(proof, Operation::Close, Moment::After, result);
    return result;
}
HRESULT STDMETHODCALLTYPE reset_hook(ID3D12GraphicsCommandList* command, ID3D12CommandAllocator* allocator,
    ID3D12PipelineState* pipeline) {
    Entry entry;
    const auto proof = command_proof(command, entry.outer && observing());
    command_event(proof, Operation::Reset, Moment::Before, E_PENDING);
    reset_count.fetch_add(1, std::memory_order_relaxed);
    const auto result = original_reset(command, allocator, pipeline);
    command_event(proof, Operation::Reset, Moment::After, result);
    return result;
}
void STDMETHODCALLTYPE execute_hook(ID3D12CommandQueue* queue, UINT count, ID3D12CommandList* const* array) {
    Entry entry;
    m::OwnedQueue proof;
    if (entry.outer && observing()) {
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
    for (auto& site : sites) if (site.forwarded) site.forwarded->bound.store(false, std::memory_order_release);
    for (auto& site : sites) { if (site.module) FreeLibrary(site.module); site = {}; }
    command_anchor = {}; queue_anchor = {}; observer = {}; classify = nullptr; may_have_enabled = false;
    return true;
}
} // namespace
Forwarder::Forwarder(Forwarder&& other) noexcept : data_(other.data_.exchange({})) {}
Forwarder& Forwarder::operator=(Forwarder&& other) noexcept {
    if (this != &other) {
        const auto previous = data_.exchange(other.data_.exchange({}));
        if (previous) previous->active.store(false, std::memory_order_release);
    }
    return *this;
}
Forwarder::~Forwarder() { deactivate(); }
Forwarder::operator bool() const noexcept { return bool(data_.load()); }
void Forwarder::deactivate() noexcept {
    if (const auto data = data_.load()) data->active.store(false, std::memory_order_release);
}
Forwarder reset_forwarder(void* target, ResetFn original) noexcept {
    Forwarder result;
    if (!target || !original) return result;
    try {
        auto data = std::make_shared<ForwarderData>();
        data->target = target; data->reset = original;
        data->module = native_module(target);
        result.data_.store(std::move(data));
    } catch (...) { return {}; }
    return result;
}
Forwarder execute_forwarder(void* target, ExecuteFn original) noexcept {
    Forwarder result;
    if (!target || !original) return result;
    try {
        auto data = std::make_shared<ForwarderData>();
        data->target = target; data->execute = original;
        data->module = native_module(target);
        result.data_.store(std::move(data));
    } catch (...) { return {}; }
    return result;
}
std::shared_ptr<ForwarderData> Delegates::for_site(std::size_t index) const noexcept {
    const auto* forwarder = index == 1 ? reset : index == 2 ? execute : nullptr;
    // A proxy forwarder remains a pass-through. It cannot stand in for the
    // actual native function deeper in that chain or suppress its observation.
    const auto data = forwarder ? forwarder->data_.load() : nullptr;
    return data && data->module ? data : nullptr;
}
HRESULT Forwarder::reset(ID3D12GraphicsCommandList* command, ID3D12CommandAllocator* allocator,
    ID3D12PipelineState* pipeline) const {
    const auto data = data_.load();
    if (!data || !data->reset) return E_UNEXPECTED;
    if (!data->bound.load(std::memory_order_acquire)) return data->reset(command, allocator, pipeline);
    Entry entry;
    const auto proof = command_proof(command, entry.outer && observing() &&
        method(command, 10) == data->target);
    command_event(proof, Operation::Reset, Moment::Before, E_PENDING);
    reset_count.fetch_add(1, std::memory_order_relaxed);
    const auto result = data->reset(command, allocator, pipeline);
    command_event(proof, Operation::Reset, Moment::After, result);
    return result;
}
void Forwarder::execute(ID3D12CommandQueue* queue, UINT count, ID3D12CommandList* const* array) const {
    const auto data = data_.load();
    if (!data || !data->execute) return;
    if (!data->bound.load(std::memory_order_acquire)) { data->execute(queue, count, array); return; }
    Entry entry;
    m::OwnedQueue proof;
    if (entry.outer && observing() && method(queue, 10) == data->target) {
        try { proof = m::acquire_queue(queue, classify); }
        catch (...) { enabled.store(false, std::memory_order_release); }
    }
    execute_event(proof, Moment::Before, count, array);
    execute_count.fetch_add(1, std::memory_order_relaxed);
    data->execute(queue, count, array);
    execute_event(proof, Moment::After, count, array);
}
bool install(const m::OwnedCommand& command, const m::OwnedQueue& queue, m::Classify owner,
    Observer callbacks, Delegates delegates) noexcept {
    try {
        std::scoped_lock guard(mutation);
        if (!command || !queue || !command.command_identity || !queue.queue_identity ||
            !command.device_identity || command.device_identity.Get() != queue.device_identity.Get() ||
            !owner || owner(command.command.Get()) != m::Owner::Native ||
            owner(queue.queue.Get()) != m::Owner::Native ||
            (!callbacks.command && !callbacks.execute)) return false;
        const std::array<void*, 3> targets{method(command.command.Get(), 9),
            method(command.command.Get(), 10), method(queue.queue.Get(), 10)};
        const std::array<std::shared_ptr<ForwarderData>, 3> borrowed{
            nullptr, delegates.for_site(1), delegates.for_site(2)};
        for (std::size_t i = 1; i < borrowed.size(); ++i)
            if (borrowed[i] && (borrowed[i]->target != targets[i] ||
                !borrowed[i]->active.load(std::memory_order_acquire) ||
                (i == 1 ? !borrowed[i]->reset : !borrowed[i]->execute))) return false;
        if (enabled.load(std::memory_order_acquire))
            return targets[0] == sites[0].target && targets[1] == sites[1].target &&
                targets[2] == sites[2].target && owner == classify &&
                callbacks.context == observer.context && callbacks.command == observer.command &&
                callbacks.execute == observer.execute && borrowed[1] == sites[1].forwarded &&
                borrowed[2] == sites[2].forwarded && observing();
        for (const auto& site : sites) if (site.created || site.module) return false; // Retry cleanup first.
        sites = {{{targets[0], reinterpret_cast<void*>(close_hook), reinterpret_cast<void**>(&original_close)},
            {targets[1], reinterpret_cast<void*>(reset_hook), reinterpret_cast<void**>(&original_reset)},
            {targets[2], reinterpret_cast<void*>(execute_hook), reinterpret_cast<void**>(&original_execute)}}};
        for (auto& site : sites) {
            site.module = native_module(site.target);
            if (!site.module) { cleanup(); return false; }
        }
        classify = owner; observer = callbacks; command_anchor = command; queue_anchor = queue;
        for (std::size_t i = 0; i < sites.size(); ++i) {
            auto& site = sites[i];
            site.forwarded = borrowed[i];
            if (site.forwarded) continue; // Owner preserves its existing hook/trampoline.
            if (MH_CreateHook(site.target, site.detour, site.original) != MH_OK) { cleanup(); return false; }
            site.created = true;
        }
        // Never apply a process-wide pending queue owned by other installers.
        // Callbacks remain closed until ALL three native sites are enabled.
        for (auto& site : sites) {
            if (site.forwarded) { site.forwarded->bound.store(true, std::memory_order_release); continue; }
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
bool ready() noexcept {
    try { std::scoped_lock guard(mutation); return observing(); }
    catch (...) { return false; }
}
Stats stats() noexcept { return {close_count.load(), reset_count.load(), execute_count.load(), before_count.load(), after_count.load()}; }
} // namespace w3vr::modern_dlss_native_hooks
