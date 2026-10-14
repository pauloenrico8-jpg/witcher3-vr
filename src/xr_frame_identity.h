#pragma once
#include <cstdint>
namespace w3vr::xr_frame {
struct Identity {
    std::uintptr_t session{};std::uint64_t epoch{},nonce{};
    bool operator==(const Identity&)const=default;
};
inline bool valid(Identity i) noexcept {
    return i.session && i.session!=UINTPTR_MAX && i.epoch && i.epoch!=UINT64_MAX &&
        i.nonce && i.nonce!=UINT64_MAX;
}
// Numeric copied provenance only; no runtime/Game/COM/GPU/loader ownership.
inline bool newer(Identity now,Identity previous) noexcept {
    return valid(now) && valid(previous) && now.nonce>previous.nonce &&
        (now.epoch>previous.epoch || (now.epoch==previous.epoch && now.session==previous.session));
}
} // namespace w3vr::xr_frame
