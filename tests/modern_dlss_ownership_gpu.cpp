#include "modern_dlss_ownership.h"
#include "modern_dlss_recording.h"
#include <dxgi1_6.h>
#include <cstdio>
#include <cstring>
#include <stdexcept>

namespace m = w3vr::modern_dlss_ownership;
namespace r = w3vr::engine_dlss_resources;
namespace recording = w3vr::modern_dlss_recording;
using Microsoft::WRL::ComPtr;

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
    initial = {};
    require(!ledger.open_stamp() && ledger.epoch() == 0, "unknown initial recording admitted");
    check(command->Close(), "close initial unobserved recording");
    auto result = command->Reset(allocator.Get(), nullptr);
    check(result, "initial observed reset");
    require(ledger.after_reset(result), "initial reset observation");
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
        require(ledger.after_close(result), "close observation");
        auto batch = ledger.take_closed(stamp);
        require(bool(batch) && batch.evaluations.size() == 1 && !batch.gpu_completion_verified,
            "closed batch is not GPU completion");
        require(!ledger.take_closed(stamp), "batch taken twice");
        auto& submitted = batch.evaluations[0];
        if (iteration == 0) {
            // Release all caller-held texture refs. The acquired record alone
            // must keep these real GPU objects alive through execution/fence.
            for (auto& object : textures) object.Reset();
        }
        require(submitted.resources[3].Get() == original_output, "output identity changed");
        ID3D12CommandList* lists[]{submitted.command.Get()};
        const std::uint64_t value = iteration + 1;
        require(!m::fence_reached(fence->GetCompletedValue(), value), "future fence incorrectly completed");
        // Hold actual GPU execution behind a CPU-signalled gate. Reset uses a
        // DIFFERENT allocator while this exact old batch is still in flight.
        // Always unblock on exceptions so an assertion cannot hang the GPU.
        struct Unblock {
            ID3D12Fence* gate; std::uint64_t value;
            ~Unblock() { gate->Signal(value); }
        } unblock{gate.Get(), value};
        require(m::compatible_queue_device(submission_queue, submitted), "submitted device mismatch");
        check(submission_queue.queue->Wait(gate.Get(), value), "queue wait before submission");
        submission_queue.queue->ExecuteCommandLists(1, lists);
        check(submission_queue.queue->Signal(fence.Get(), value), "signal after real submission");
        require(!m::fence_reached(fence->GetCompletedValue(), value), "gate did not delay GPU work");
        auto* next_allocator = iteration & 1 ? allocator.Get() : alternate_allocator.Get();
        check(next_allocator->Reset(), "unused allocator reset");
        result = command->Reset(next_allocator, nullptr); check(result, "Reset while old batch pending");
        require(ledger.after_reset(result) && ledger.epoch() == stamp.epoch + 1,
            "new recording epoch after early Reset");
        auto stale = m::acquire(receipt, classify);
        require(!ledger.record(std::move(stale), stamp) && bool(stale) && ledger.pending() == 0,
            "stale producer crossed real Reset");
        stale = {};
        require(bool(batch) && batch.stamp == stamp && submitted.resources[3].Get() == original_output,
            "Reset discarded submitted ownership");
        check(gate->Signal(value), "CPU release submission gate");
        check(fence->SetEventOnCompletion(value, event), "completion event");
        require(WaitForSingleObject(event, 10000) == WAIT_OBJECT_0, "GPU fence timeout");
        const auto completed = fence->GetCompletedValue();
        require(m::fence_reached(completed, value), "exact fence not reached");
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
        textures = submitted.resources;
    }
    CloseHandle(event);
    std::puts("PASS 24 real GPU copies: closed batches, early Reset, stale rejection, exact fence and readback");
    std::puts("GAME=false HEADSET=false NATIVE_DLSS=false NOVIGRAD_FPS_MEASURED=false");
    return 0;
} catch (const std::exception& error) {
    std::printf("FAIL %s\n", error.what());
    return 1;
}
