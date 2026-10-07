#include "modern_dlss_ownership.h"
#include "modern_dlss_recording.h"
#include "modern_dlss_retirement.h"
#include "modern_dlss_native_hooks.h"
#include <MinHook.h>
#include <dxgi1_6.h>
#include <cstdio>
#include <cstring>
#include <stdexcept>

namespace m = w3vr::modern_dlss_ownership;
namespace r = w3vr::engine_dlss_resources;
namespace recording = w3vr::modern_dlss_recording;
namespace retirement = w3vr::modern_dlss_retirement;
namespace native_hooks = w3vr::modern_dlss_native_hooks;
using Microsoft::WRL::ComPtr;

struct ProbeEvents {
    recording::Ledger* ledger{};
    const IUnknown* command{};
    bool reset_accepted{}, close_accepted{}, failed_reset_seen{}, failed_close_seen{};
    bool try_busy_uninstall{}, busy_uninstall_refused{};
};
struct ActiveSubmission {
    retirement::Timeline* timeline{};
    retirement::Ticket ticket;
    const IUnknown* queue{};
    UINT count{};
    ID3D12CommandList* const* array{};
    bool before{}, after{}, mismatch{};
    HRESULT result{E_PENDING};
};
thread_local ActiveSubmission* active_submission{};
void command_observer(void* context, const m::OwnedCommand& command, native_hooks::Operation operation,
    native_hooks::Moment moment, HRESULT result) noexcept {
    auto& events = *static_cast<ProbeEvents*>(context);
    if (moment == native_hooks::Moment::Before) {
        if (events.try_busy_uninstall && operation == native_hooks::Operation::Reset) {
            events.try_busy_uninstall = false;
            events.busy_uninstall_refused = !native_hooks::uninstall();
        }
        return;
    }
    if (FAILED(result)) {
        if (operation == native_hooks::Operation::Reset) events.failed_reset_seen = true;
        else events.failed_close_seen = true;
    }
    if (command.command_identity.Get() != events.command) return;
    if (operation == native_hooks::Operation::Reset)
        events.reset_accepted = events.ledger->after_reset(result);
    else if (events.ledger->open_stamp()) // Bootstrap Close has no known epoch.
        events.close_accepted = events.ledger->after_close(result);
}
void execute_observer(void*, const m::OwnedQueue& queue, native_hooks::Moment moment,
    UINT count, ID3D12CommandList* const* array) noexcept {
    auto* submission = active_submission;
    if (!submission) return;
    if (queue.queue_identity.Get() != submission->queue || count != submission->count ||
        array != submission->array) { submission->mismatch = true; return; }
    if (moment == native_hooks::Moment::Before)
        submission->before = submission->timeline->before_execute(submission->ticket);
    else {
        submission->result = submission->timeline->after_execute(submission->ticket);
        submission->after = true;
    }
}
HRESULT observed_execute(retirement::Timeline& timeline, retirement::Ticket ticket,
    const m::OwnedQueue& queue, UINT count, ID3D12CommandList* const* array) {
    ActiveSubmission observation{&timeline, ticket, queue.queue_identity.Get(), count, array};
    active_submission = &observation;
    queue.queue->ExecuteCommandLists(count, array);
    active_submission = nullptr;
    if (!observation.before || !observation.after || observation.mismatch) {
        // Never cancel after an original Execute might have run. Preserve
        // the test's ownership conservatively before reporting a missing hook.
        if (!observation.before) timeline.before_execute(ticket);
        timeline.after_execute(ticket);
        throw std::runtime_error("native Execute callback missing or arguments changed");
    }
    return observation.result;
}
using ForeignReset = HRESULT (STDMETHODCALLTYPE*)(ID3D12GraphicsCommandList*, ID3D12CommandAllocator*, ID3D12PipelineState*);
ForeignReset foreign_original{};
unsigned foreign_calls{};
HRESULT STDMETHODCALLTYPE foreign_reset(ID3D12GraphicsCommandList* command, ID3D12CommandAllocator* allocator,
    ID3D12PipelineState* pipeline) {
    ++foreign_calls; return foreign_original(command, allocator, pipeline);
}

// Explicit failure seam: this HRESULT is SIMULATED. Remaining queue work,
// retry Signal, private fences, early Reset and pixel copies are physical.
bool fail_signal_once = true;
HRESULT probe_signal(ID3D12CommandQueue* queue, ID3D12Fence* fence, std::uint64_t value) {
    if (fail_signal_once) { fail_signal_once = false; return E_FAIL; }
    return queue->Signal(fence, value);
}

void require(bool value, const char* step) {
    if (!value) throw std::runtime_error(step);
}
void check(HRESULT value, const char* step) {
    if (FAILED(value)) {
        std::printf("FAIL %s HRESULT=%08lx\n", step, static_cast<unsigned long>(value));
        throw std::runtime_error(step);
    }
}
m::Owner classify(const IUnknown* input) {
    if (!input) return m::Owner::Unknown;
    const auto table = *reinterpret_cast<void* const* const*>(input);
    HMODULE owner{};
    GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS |
        GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
        reinterpret_cast<LPCWSTR>(table[0]), &owner);
    const auto core = GetModuleHandleW(L"D3D12Core.dll");
    const auto api = GetModuleHandleW(L"d3d12.dll");
    return owner != nullptr && (owner == core || owner == api)
        ? m::Owner::Native : m::Owner::Unknown;
}
ComPtr<ID3D12Resource> texture(ID3D12Device* device, unsigned size = 16, bool uav = true) {
    D3D12_HEAP_PROPERTIES heap{}; heap.Type = D3D12_HEAP_TYPE_DEFAULT;
    D3D12_RESOURCE_DESC desc{};
    desc.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
    desc.Width = size; desc.Height = size; desc.DepthOrArraySize = 1;
    desc.MipLevels = 1; desc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
    desc.SampleDesc.Count = 1;
    desc.Flags = uav ? D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS : D3D12_RESOURCE_FLAG_NONE;
    ComPtr<ID3D12Resource> result;
    check(device->CreateCommittedResource(&heap, D3D12_HEAP_FLAG_NONE, &desc,
        D3D12_RESOURCE_STATE_COMMON, nullptr, IID_PPV_ARGS(&result)), "texture");
    return result;
}
ComPtr<ID3D12Resource> buffer(ID3D12Device* device, D3D12_HEAP_TYPE type,
    D3D12_RESOURCE_STATES state) {
    D3D12_HEAP_PROPERTIES heap{}; heap.Type = type;
    D3D12_RESOURCE_DESC desc{};
    desc.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER;
    desc.Width = 4096; desc.Height = 1; desc.DepthOrArraySize = 1;
    desc.MipLevels = 1; desc.SampleDesc.Count = 1; desc.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
    ComPtr<ID3D12Resource> result;
    check(device->CreateCommittedResource(&heap, D3D12_HEAP_FLAG_NONE, &desc,
        state, nullptr, IID_PPV_ARGS(&result)), "buffer");
    return result;
}
void transition(ID3D12GraphicsCommandList* command, ID3D12Resource* resource,
    D3D12_RESOURCE_STATES before, D3D12_RESOURCE_STATES after) {
    D3D12_RESOURCE_BARRIER barrier{};
    barrier.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
    barrier.Transition = {resource, D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES, before, after};
    command->ResourceBarrier(1, &barrier);
}

int main() try {
    SetErrorMode(SEM_FAILCRITICALERRORS | SEM_NOGPFAULTERRORBOX);
    ComPtr<IDXGIFactory6> factory;
    check(CreateDXGIFactory1(IID_PPV_ARGS(&factory)), "factory");
    ComPtr<IDXGIAdapter1> adapter;
    check(factory->EnumAdapterByGpuPreference(0, DXGI_GPU_PREFERENCE_HIGH_PERFORMANCE,
        IID_PPV_ARGS(&adapter)), "hardware adapter");
    DXGI_ADAPTER_DESC1 adapter_desc{};
    check(adapter->GetDesc1(&adapter_desc), "adapter description");
    require((adapter_desc.Flags & DXGI_ADAPTER_FLAG_SOFTWARE) == 0, "physical GPU required");
    std::printf("GPU %ls vendor=%04x dedicated_bytes=%llu\n", adapter_desc.Description,
        adapter_desc.VendorId, static_cast<unsigned long long>(adapter_desc.DedicatedVideoMemory));
    ComPtr<ID3D12Device> device;
    check(D3D12CreateDevice(adapter.Get(), D3D_FEATURE_LEVEL_11_0, IID_PPV_ARGS(&device)), "device");
    D3D12_COMMAND_QUEUE_DESC queue_desc{}; queue_desc.Type = D3D12_COMMAND_LIST_TYPE_DIRECT;
    ComPtr<ID3D12CommandQueue> queue;
    check(device->CreateCommandQueue(&queue_desc, IID_PPV_ARGS(&queue)), "queue");
    ComPtr<ID3D12CommandAllocator> allocator;
    check(device->CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE_DIRECT, IID_PPV_ARGS(&allocator)), "allocator");
    ComPtr<ID3D12GraphicsCommandList> command;
    check(device->CreateCommandList(0, D3D12_COMMAND_LIST_TYPE_DIRECT, allocator.Get(),
        nullptr, IID_PPV_ARGS(&command)), "command list");
    ComPtr<ID3D12CommandAllocator> alternate_allocator;
    check(device->CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE_DIRECT,
        IID_PPV_ARGS(&alternate_allocator)), "alternate allocator");
    ComPtr<ID3D12Fence> fence;
    check(device->CreateFence(0, D3D12_FENCE_FLAG_NONE, IID_PPV_ARGS(&fence)), "fence");
    ComPtr<ID3D12Fence> gate;
    check(device->CreateFence(0, D3D12_FENCE_FLAG_NONE, IID_PPV_ARGS(&gate)), "submission gate");
    const HANDLE event = CreateEventW(nullptr, FALSE, FALSE, nullptr);
    require(event != nullptr, "event");
    // All four lab textures are real. The receipt is manufactured for this
    // standalone probe: it does not claim a native DLSS call or game image.
    std::array<ComPtr<ID3D12Resource>, 4> textures;
    r::CpuResourceReceipt receipt{};
    receipt.valid = true; receipt.identity = {1, 1, 0};
    receipt.viewport = w3vr::engine_dlss::constructed_viewport;
    receipt.command = command.Get(); receipt.token = reinterpret_cast<void*>(0x100000100ull);
    constexpr std::array<unsigned, 4> types{0, 1, 3, 4};
    for (std::size_t i = 0; i < textures.size(); ++i) {
        textures[i] = texture(device.Get());
        auto& b = receipt.bindings[i];
        b.tag = {0x100000200ull + i * 0x100, types[i], 1, {0, 0, 16, 16}};
        b.resource.native = reinterpret_cast<std::uint64_t>(textures[i].Get());
        b.resource.state = i == 3 ? 8 : 0;
    }
    {
        auto owned = m::acquire(receipt, classify);
        require(bool(owned) && owned.command.Get() == command.Get(), "native acquisition");
        auto queue_owned = m::acquire_queue(queue.Get(), classify);
        require(bool(queue_owned) && m::compatible_queue_device(queue_owned, owned) &&
            !queue_owned.gpu_completion_verified, "native queue acquisition/device match");
        ComPtr<ID3D12CommandQueue> independent_queue;
        check(device->CreateCommandQueue(&queue_desc, IID_PPV_ARGS(&independent_queue)), "independent queue");
        auto independent = m::acquire_queue(independent_queue.Get(), classify);
        require(bool(independent) && m::compatible_queue_device(independent, owned) &&
            independent.queue_identity.Get() != queue_owned.queue_identity.Get(),
            "independent queues collapsed to device identity");
        auto compute_desc = queue_desc; compute_desc.Type = D3D12_COMMAND_LIST_TYPE_COMPUTE;
        ComPtr<ID3D12CommandQueue> compute_queue;
        check(device->CreateCommandQueue(&compute_desc, IID_PPV_ARGS(&compute_queue)), "compute queue");
        require(m::acquire_queue(compute_queue.Get(), classify).failure == m::Failure::Interface,
            "non DIRECT queue admitted");
        ComPtr<ID3D12CommandAllocator> compute_allocator;
        ComPtr<ID3D12GraphicsCommandList> compute_command;
        check(device->CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE_COMPUTE,
            IID_PPV_ARGS(&compute_allocator)), "compute allocator");
        check(device->CreateCommandList(0, D3D12_COMMAND_LIST_TYPE_COMPUTE, compute_allocator.Get(),
            nullptr, IID_PPV_ARGS(&compute_command)), "compute command");
        const auto rejected_command = m::acquire_command(compute_command.Get(), classify);
        require(rejected_command.failure == m::Failure::Interface && !rejected_command.command &&
            !rejected_command.device, "compute command admitted or leaked partial ownership");
        check(compute_command->Close(), "compute probe close");
        require(!owned.gpu_completion_verified, "ownership is not GPU completion");
        auto bad = receipt; bad.bindings[1].resource.native = bad.bindings[0].resource.native;
        require(m::acquire(bad, classify).failure == m::Failure::AliasedResource, "alias rejection");
        auto undersized = texture(device.Get(), 8);
        bad = receipt; bad.bindings[2].resource.native = reinterpret_cast<std::uint64_t>(undersized.Get());
        require(m::acquire(bad, classify).failure == m::Failure::Texture, "undersized rejection");
        auto no_uav = texture(device.Get(), 16, false);
        bad = receipt; bad.bindings[3].resource.native = reinterpret_cast<std::uint64_t>(no_uav.Get());
        require(m::acquire(bad, classify).failure == m::Failure::Texture, "missing UAV rejection");
        auto wrong_dimension = buffer(device.Get(), D3D12_HEAP_TYPE_DEFAULT, D3D12_RESOURCE_STATE_COMMON);
        bad = receipt; bad.bindings[0].resource.native = reinterpret_cast<std::uint64_t>(wrong_dimension.Get());
        require(m::acquire(bad, classify).failure == m::Failure::Texture, "buffer rejection");
        ComPtr<IDXGIAdapter> warp;
        check(factory->EnumWarpAdapter(IID_PPV_ARGS(&warp)), "second adapter for rejection");
        ComPtr<ID3D12Device> other_device;
        check(D3D12CreateDevice(warp.Get(), D3D_FEATURE_LEVEL_11_0, IID_PPV_ARGS(&other_device)), "second device");
        ComPtr<ID3D12CommandQueue> other_queue;
        check(other_device->CreateCommandQueue(&queue_desc, IID_PPV_ARGS(&other_queue)), "foreign queue");
        auto foreign_queue = m::acquire_queue(other_queue.Get(), classify);
        require(bool(foreign_queue) && !m::compatible_queue_device(foreign_queue, owned), "foreign queue device matched");
        auto foreign = texture(other_device.Get());
        bad = receipt; bad.bindings[0].resource.native = reinterpret_cast<std::uint64_t>(foreign.Get());
        require(m::acquire(bad, classify).failure == m::Failure::Device, "foreign device rejection");
        std::puts("PASS real-resource admission and alias/dimension/size/UAV/device rejection");
        std::puts("PASS real queue ownership: DIRECT only, distinct identities, same/foreign device checks");
    }
    auto upload = buffer(device.Get(), D3D12_HEAP_TYPE_UPLOAD, D3D12_RESOURCE_STATE_GENERIC_READ);
    auto readback = buffer(device.Get(), D3D12_HEAP_TYPE_READBACK, D3D12_RESOURCE_STATE_COPY_DEST);
    auto initial = m::acquire(receipt, classify);
    auto submission_queue = m::acquire_queue(queue.Get(), classify);
    require(bool(submission_queue) && m::compatible_queue_device(submission_queue, initial), "submission queue ownership");
    queue.Reset(); // All loop submissions use only the acquired native queue.
    recording::Ledger ledger(initial);
    const auto command_id = initial.command_identity.Get();
    initial = {};
    require(!ledger.open_stamp() && ledger.epoch() == 0, "unknown initial recording admitted");
    require(MH_Initialize() == MH_OK, "MinHook initialization");
    struct UninstallNative {
        ~UninstallNative() { if (native_hooks::uninstall(true)) MH_Uninitialize(); }
    } native_cleanup;
    ProbeEvents events{&ledger, command_id};
    const native_hooks::Observer observer{&events, command_observer, execute_observer};
    auto native_command = m::acquire_command(command.Get(), classify);
    require(bool(native_command), "endpoint before producer without invented receipt");
    auto* reset_target = (*reinterpret_cast<void***>(command.Get()))[10];
    require(MH_CreateHook(reset_target, reinterpret_cast<void*>(foreign_reset),
        reinterpret_cast<void**>(&foreign_original)) == MH_OK, "foreign reset reservation");
    require(!native_hooks::install(native_command, submission_queue, classify, observer) &&
        !native_hooks::ready(), "native install ignored target collision");
    require(MH_EnableHook(reset_target) == MH_OK, "failed install removed foreign hook");
    check(command->Close(), "foreign initial Close");
    check(command->Reset(allocator.Get(), nullptr), "foreign original Reset");
    require(foreign_calls == 1, "foreign original forwarded more than once");
    require(MH_DisableHook(reset_target) == MH_OK && MH_RemoveHook(reset_target) == MH_OK, "foreign cleanup");
    require(native_hooks::install(native_command, submission_queue, classify, observer) &&
        native_hooks::ready(), "three native endpoint hooks installed");
    std::puts("PASS native endpoint transaction: foreign collision preserved, three observers activated");
    check(command->Close(), "close initial unobserved recording");
    auto result = command->Reset(allocator.Get(), nullptr);
    check(result, "initial observed reset");
    require(events.reset_accepted && ledger.epoch() == 1, "actual native Reset starts first epoch");
    // Real failing Close and Reset on a separate native command list must not
    // supply a usable recording. No failure is injected into the main queue.
    {
        ComPtr<ID3D12CommandAllocator> failure_allocator;
        check(device->CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE_DIRECT,
            IID_PPV_ARGS(&failure_allocator)), "failure allocator");
        ComPtr<ID3D12GraphicsCommandList> failure_command;
        check(device->CreateCommandList(0, D3D12_COMMAND_LIST_TYPE_DIRECT,
            failure_allocator.Get(), nullptr, IID_PPV_ARGS(&failure_command)), "failure list");
        auto other_receipt = receipt; other_receipt.command = failure_command.Get();
        auto other = m::acquire(other_receipt, classify);
        require(bool(other), "second command acquisition");
        auto wrong = m::acquire(other_receipt, classify);
        require(!ledger.record(std::move(wrong), ledger.open_stamp()) && bool(wrong),
            "foreign command recording admitted or consumed");
        recording::Ledger failed(other);
        check(failure_command->Close(), "failure list initial close");
        result = failure_command->Reset(failure_allocator.Get(), nullptr);
        check(result, "failure list first reset"); require(failed.after_reset(result), "failure epoch");
        const auto old_epoch = failed.epoch();
        result = failure_command->Reset(failure_allocator.Get(), nullptr); // Still open.
        require(FAILED(result) && !failed.after_reset(result) && failed.epoch() == old_epoch &&
            !failed.open_stamp(), "failed native Reset became a new epoch");
        check(failure_command->Close(), "failure list close");
        result = failure_command->Close(); // Already closed.
        require(FAILED(result) && !failed.after_close(result) && !failed.open_stamp(),
            "failed native Close admitted a batch");
        result = failure_command->Reset(failure_allocator.Get(), nullptr);
        require(!failed.after_reset(result), "broken observer ledger reopened");
        std::puts("PASS real failed Reset/Close and foreign native command rejection");
    }
    auto* original_output = textures[3].Get();
    retirement::Timeline timeline(submission_queue, probe_signal);
    require(bool(timeline), "native private-fence timeline");
    ComPtr<ID3D12CommandQueue> unrelated_queue;
    check(device->CreateCommandQueue(&queue_desc, IID_PPV_ARGS(&unrelated_queue)), "other retirement queue");
    auto unrelated_owned = m::acquire_queue(unrelated_queue.Get(), classify);
    retirement::Timeline unrelated(unrelated_owned);
    require(bool(unrelated), "independent private-fence timeline");
    for (unsigned iteration = 0; iteration < 24; ++iteration) {
        const auto stamp = ledger.open_stamp();
        require(bool(stamp) && stamp.epoch == iteration + 1ull, "current recording stamp");
        receipt.identity = {iteration + 1ull, 1, static_cast<int>(iteration & 1)};
        auto owned = m::acquire(receipt, classify);
        require(bool(owned), "per-recording acquisition");
        require(owned.command_identity.Get() == stamp.command, "canonical command mismatch");
        void* data{}; D3D12_RANGE no_read{0, 0};
        check(upload->Map(0, &no_read, &data), "upload map");
        const auto pattern = static_cast<unsigned char>(0x31 + iteration);
        std::memset(data, pattern, 4096); upload->Unmap(0, nullptr);
        D3D12_TEXTURE_COPY_LOCATION source{};
        source.pResource = upload.Get(); source.Type = D3D12_TEXTURE_COPY_TYPE_PLACED_FOOTPRINT;
        source.PlacedFootprint.Footprint = {DXGI_FORMAT_R8G8B8A8_UNORM, 16, 16, 1, 256};
        D3D12_TEXTURE_COPY_LOCATION target{};
        target.pResource = owned.resources[3].Get(); target.Type = D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX;
        transition(command.Get(), target.pResource, iteration == 0 ? D3D12_RESOURCE_STATE_COMMON :
            D3D12_RESOURCE_STATE_COPY_SOURCE, D3D12_RESOURCE_STATE_COPY_DEST);
        command->CopyTextureRegion(&target, 0, 0, 0, &source, nullptr);
        transition(command.Get(), target.pResource, D3D12_RESOURCE_STATE_COPY_DEST, D3D12_RESOURCE_STATE_COPY_SOURCE);
        source.pResource = readback.Get();
        command->CopyTextureRegion(&source, 0, 0, 0, &target, nullptr);
        require(ledger.record(std::move(owned), stamp), "record ownership transfer");
        auto duplicate = m::acquire(receipt, classify);
        require(!ledger.record(std::move(duplicate), stamp) && bool(duplicate), "duplicate evaluation admitted");
        duplicate = {};
        require(!ledger.take_closed(stamp), "open list admitted for submission");
        result = command->Close(); check(result, "close command list");
        require(events.close_accepted, "actual native Close observation");
        auto batch = ledger.take_closed(stamp);
        require(bool(batch) && batch.evaluations.size() == 1 && !batch.gpu_completion_verified,
            "closed batch is not GPU completion");
        require(!ledger.take_closed(stamp), "batch taken twice");
        auto held = retirement::retain(std::move(batch));
        require(bool(held), "retain immutable recording");
        // Copy already closed lab references into a dormant handle, never
        // admitted to a timeline. Successful Reset must abandon it as well.
        recording::ClosedRecording dormant_batch;
        dormant_batch.stamp = held.get()->stamp;
        dormant_batch.evaluations = held.get()->evaluations;
        auto dormant = retirement::retain(std::move(dormant_batch));
        const auto& submitted = held.get()->evaluations[0];
        // Release every caller-held texture ref in every iteration.
        for (auto& object : textures) object.Reset();
        require(submitted.resources[3].Get() == original_output, "output identity changed");
        ID3D12CommandList* lists[]{submitted.command.Get()};
        IUnknown* ids[]{submitted.command_identity.Get()};
        if (iteration == 0) {
            std::array<retirement::Ticket, 64> reserved;
            for (auto& item : reserved) {
                item = timeline.prepare(held, stamp, submission_queue, ids);
                require(bool(item), "bounded pre-Execute reservation");
            }
            require(!timeline.prepare(held, stamp, submission_queue, ids), "reservation capacity exceeded");
            for (const auto& item : reserved) require(timeline.cancel(item), "safe pre-Execute cancellation");
            require(timeline.pending() == 0 && !timeline.find(stamp), "cancel retained unused recording");
        }
        const auto ticket = timeline.prepare(held, stamp, submission_queue, ids);
        require(bool(ticket) && timeline.status(ticket) == retirement::Status::Prepared,
            "prepare before Execute");
        require(!timeline.prepare(held, stamp, unrelated_owned, ids), "wrong queue accepted on same device");
        require(unrelated.status(ticket) == retirement::Status::Unknown && !unrelated.cancel(ticket),
            "foreign private fence accepted ticket");
        IUnknown* absent[]{submission_queue.queue_identity.Get()};
        require(!timeline.prepare(held, stamp, submission_queue, absent), "command absent from Execute array admitted");
        const std::uint64_t value = iteration * 2ull + 1;
        // Hold actual GPU execution behind a CPU-signalled gate. Reset uses a
        // DIFFERENT allocator while this exact old batch is still in flight.
        // Always unblock on exceptions so an assertion cannot hang the GPU.
        struct Unblock {
            ID3D12Fence* gate; std::uint64_t value;
            ~Unblock() { gate->Signal(value); }
        } unblock{gate.Get(), value};
        require(m::compatible_queue_device(submission_queue, submitted), "submitted device mismatch");
        check(submission_queue.queue->Wait(gate.Get(), value), "queue wait before submission");
        result = observed_execute(timeline, ticket, submission_queue, 1, lists);
        require(!timeline.before_execute(ticket) && !timeline.cancel(ticket), "native observer retained before original");
        if (iteration == 0) {
            require(FAILED(result) && timeline.status(ticket) == retirement::Status::Quarantined &&
                !timeline.release_completed(ticket) && !timeline.prepare(held, stamp, submission_queue, ids),
                "failed Signal released resources or allowed new admission");
            check(timeline.retry_signal(ticket), "retry real queue Signal after simulated failure");
            std::puts("PASS simulated Signal failure quarantines ownership; real retry uses a new value");
        } else check(result, "private Signal after real submission");
        require(FAILED(timeline.after_execute(ticket)) && FAILED(timeline.retry_signal(ticket)),
            "duplicate completion Signal admitted");
        // An unrelated fence can advance arbitrarily on another DIRECT queue.
        // It must not retire the private fence of this gated submission.
        check(unrelated_owned.queue->Signal(fence.Get(), value + 1000), "unrelated fence advance");
        check(fence->SetEventOnCompletion(value + 1000, event), "unrelated fence event");
        require(WaitForSingleObject(event, 10000) == WAIT_OBJECT_0, "unrelated fence timeout");
        require(timeline.status(ticket) == retirement::Status::Pending &&
            !timeline.release_completed(ticket) && !timeline.completed_recording(ticket),
            "another queue/fence prematurely completed submission");
        // Replay after the first completion must still own textures before any
        // successful Reset. Iteration 1 starts/ends in COPY_SOURCE for replay.
        if (iteration == 1) {
            check(gate->Signal(value), "release first replay submission");
            check(timeline.completion_event(ticket, event), "first replay fence event");
            require(WaitForSingleObject(event, 10000) == WAIT_OBJECT_0 &&
                timeline.release_completed(ticket), "first replay completion");
            held = {};
            auto replay = timeline.find(stamp);
            require(bool(replay), "completed recording lost replay ownership");
            const auto replay_ticket = timeline.prepare(replay, stamp, submission_queue, ids);
            check(submission_queue.queue->Wait(gate.Get(), value + 1), "replay queue gate");
            check(observed_execute(timeline, replay_ticket, submission_queue, 1, lists), "native replay private Signal");
            check(gate->Signal(value + 1), "release replay");
            check(timeline.completion_event(replay_ticket, event), "replay event");
            require(WaitForSingleObject(event, 10000) == WAIT_OBJECT_0 &&
                timeline.release_completed(replay_ticket), "replay retirement");
            held = timeline.find(stamp);
            require(bool(held), "second completion discarded replayable recording");
            textures = held.get()->evaluations[0].resources; // Replay already completed.
            unblock.value = value + 1;
            std::puts("PASS actual repeated Execute retains recording after first private fence completion");
        }
        auto* next_allocator = iteration & 1 ? allocator.Get() : alternate_allocator.Get();
        check(next_allocator->Reset(), "unused allocator reset");
        result = command->Reset(next_allocator, nullptr); check(result, "Reset while old batch pending");
        require(events.reset_accepted && ledger.epoch() == stamp.epoch + 1,
            "new recording epoch after early Reset");
        retirement::observe_reset(ledger.open_stamp());
        require(!timeline.find(stamp) && !timeline.prepare(held, stamp, submission_queue, ids),
            "successful Reset allowed replay of old recording");
        require(!unrelated.prepare(dormant, stamp, unrelated_owned, ids),
            "Reset missed dormant handle outside every timeline");
        dormant = {};
        auto stale = m::acquire(receipt, classify);
        require(!ledger.record(std::move(stale), stamp) && bool(stale) && ledger.pending() == 0,
            "stale producer crossed real Reset");
        stale = {};
        require(bool(held) && held.get()->stamp == stamp && submitted.resources[3].Get() == original_output,
            "Reset discarded submitted ownership");
        // All caller recording refs now released. Timeline entries alone keep
        // GPU objects alive in pending iterations; copy refs for next iteration
        // only after a verified completion below.
        held = {};
        check(gate->Signal(unblock.value), "CPU release submission gate");
        retirement::Recording completed_recording;
        if (iteration != 1) {
            check(timeline.completion_event(ticket, event), "private completion event");
            require(WaitForSingleObject(event, 10000) == WAIT_OBJECT_0, "GPU private fence timeout");
            require(timeline.status(ticket) == retirement::Status::Complete, "exact private fence not reached");
            completed_recording = timeline.completed_recording(ticket);
            require(bool(completed_recording), "completed resource access");
        }
        check(device->GetDeviceRemovedReason(), "device remains available");
        D3D12_RANGE range{0, 4096};
        check(readback->Map(0, &range, &data), "readback after fence");
        const auto* bytes = static_cast<const unsigned char*>(data);
        for (unsigned row = 0; row < 16; ++row)
            for (unsigned column = 0; column < 64; ++column)
                require(bytes[row * 256 + column] == pattern, "texture data mismatch");
        readback->Unmap(0, &no_read);
        // Reacquire the caller refs before the record retires; the next Reset
        // reuses the command object but starts a different recording.
        if (iteration != 1) textures = completed_recording.get()->evaluations[0].resources;
        if (iteration != 1) require(timeline.release_completed(ticket), "completed ticket release");
        completed_recording = {};
        require(timeline.pending() == 0, "submission entry leak");
    }
    {
        require(retirement::orphan_count() == 0, "unexpected preexisting orphan");
        ComPtr<ID3D12CommandAllocator> orphan_allocator;
        check(device->CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE_DIRECT,
            IID_PPV_ARGS(&orphan_allocator)), "orphan allocator");
        ComPtr<ID3D12GraphicsCommandList> orphan_command;
        check(device->CreateCommandList(0, D3D12_COMMAND_LIST_TYPE_DIRECT, orphan_allocator.Get(),
            nullptr, IID_PPV_ARGS(&orphan_command)), "orphan command");
        auto orphan_receipt = receipt; orphan_receipt.command = orphan_command.Get();
        std::array<ComPtr<ID3D12Resource>, 4> orphan_textures;
        for (std::size_t i = 0; i < orphan_textures.size(); ++i) {
            orphan_textures[i] = texture(device.Get());
            orphan_receipt.bindings[i].resource.native = reinterpret_cast<std::uint64_t>(orphan_textures[i].Get());
        }
        auto orphan_proof = m::acquire(orphan_receipt, classify);
        require(bool(orphan_proof), "orphan acquisition");
        recording::Ledger orphan_ledger(orphan_proof);
        orphan_proof = {};
        check(orphan_command->Close(), "orphan initial close");
        result = orphan_command->Reset(orphan_allocator.Get(), nullptr); check(result, "orphan first Reset");
        require(orphan_ledger.after_reset(result), "orphan recording epoch");
        const auto orphan_stamp = orphan_ledger.open_stamp();
        auto orphan_evaluation = m::acquire(orphan_receipt, classify);
        D3D12_TEXTURE_COPY_LOCATION source{};
        source.pResource = upload.Get(); source.Type = D3D12_TEXTURE_COPY_TYPE_PLACED_FOOTPRINT;
        source.PlacedFootprint.Footprint = {DXGI_FORMAT_R8G8B8A8_UNORM, 16, 16, 1, 256};
        D3D12_TEXTURE_COPY_LOCATION target{};
        target.pResource = orphan_evaluation.resources[3].Get();
        target.Type = D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX;
        transition(orphan_command.Get(), target.pResource, D3D12_RESOURCE_STATE_COMMON, D3D12_RESOURCE_STATE_COPY_DEST);
        orphan_command->CopyTextureRegion(&target, 0, 0, 0, &source, nullptr);
        transition(orphan_command.Get(), target.pResource, D3D12_RESOURCE_STATE_COPY_DEST, D3D12_RESOURCE_STATE_COPY_SOURCE);
        source.pResource = readback.Get();
        orphan_command->CopyTextureRegion(&source, 0, 0, 0, &target, nullptr);
        require(orphan_ledger.record(std::move(orphan_evaluation), orphan_stamp), "orphan ownership transfer");
        result = orphan_command->Close(); check(result, "orphan Close");
        require(orphan_ledger.after_close(result), "orphan close observation");
        auto held = retirement::retain(orphan_ledger.take_closed(orphan_stamp));
        require(bool(held), "orphan closed recording");
        for (auto& object : orphan_textures) object.Reset();
        struct Unblock { ID3D12Fence* gate; ~Unblock() { gate->Signal(1000); } } unblock{gate.Get()};
        {
            retirement::Timeline doomed(submission_queue);
            IUnknown* ids[]{held.get()->evaluations[0].command_identity.Get()};
            const auto ticket = doomed.prepare(held, orphan_stamp, submission_queue, ids);
            ID3D12CommandList* lists[]{orphan_command.Get()};
            check(submission_queue.queue->Wait(gate.Get(), 1000), "orphan gate");
            check(observed_execute(doomed, ticket, submission_queue, 1, lists), "native orphan private Signal");
            check(doomed.completion_event(ticket, event), "orphan event registration");
            held = {}; // QueueState alone owns all four lab textures.
            require(doomed.status(ticket) == retirement::Status::Pending, "orphan GPU gate");
        } // Destructor must move its already allocated state to quarantine.
        require(retirement::orphan_count() == 1 && retirement::collect_orphans() == 0,
            "destructor released an in-flight recording");
        check(gate->Signal(1000), "release orphan GPU work");
        require(WaitForSingleObject(event, 10000) == WAIT_OBJECT_0, "orphan completion timeout");
        void* data{}; D3D12_RANGE range{0, 4096};
        check(readback->Map(0, &range, &data), "orphan readback");
        for (unsigned row = 0; row < 16; ++row)
            for (unsigned column = 0; column < 64; ++column)
                require(static_cast<unsigned char*>(data)[row * 256 + column] == 0x31 + 23,
                    "orphan pixel mismatch");
        D3D12_RANGE no_write{0, 0}; readback->Unmap(0, &no_write);
        // Completion retires the submission, but the closed command remains
        // replayable and must still own textures even after timeline deletion.
        require(retirement::collect_orphans() == 0 && retirement::orphan_count() == 1,
            "orphan lost replayable recording after completion");
        check(orphan_allocator->Reset(), "orphan allocator after completion");
        result = orphan_command->Reset(orphan_allocator.Get(), nullptr); check(result, "orphan new Reset");
        require(orphan_ledger.after_reset(result), "orphan new epoch");
        retirement::observe_reset(orphan_ledger.open_stamp());
        require(retirement::collect_orphans() == 1 && retirement::orphan_count() == 0,
            "completed and abandoned orphan did not retire");
        std::puts("PASS physical copy survives timeline destruction; orphan retains replay until successful Reset");
    }
    require(events.failed_reset_seen && events.failed_close_seen, "native observer missed failed HRESULTs");
    check(command->Close(), "final native Close");
    events.try_busy_uninstall = true;
    check(command->Reset(allocator.Get(), nullptr), "Reset while observer requests cleanup");
    require(events.busy_uninstall_refused && !native_hooks::ready(), "busy cleanup removed active trampoline");
    const auto native_stats = native_hooks::stats();
    require(native_stats.execute == 26 && native_stats.before == native_stats.after, "native forwarding/event counts mismatch");
    require(native_hooks::uninstall(true) && !native_hooks::ready(), "quiescent native cleanup");
    std::printf("PASS actual native hooks: Close=%llu Reset=%llu Execute=%llu balanced_events=%llu; exact original array/count/queue\n",
        native_stats.close, native_stats.reset, native_stats.execute, native_stats.before);
    std::puts("PASS native Reset/Close drive Ledger; native Execute callbacks retain and Signal private fences");
    CloseHandle(event);
    std::puts("PASS 26 real GPU copies: 24 recordings, one replay and one orphan; exact private fences/readback");
    std::puts("GAME=false HEADSET=false NATIVE_DLSS=false STEREO_IMAGE=false");
    return 0;
} catch (const std::exception& error) {
    std::printf("FAIL %s\n", error.what());
    return 1;
}
