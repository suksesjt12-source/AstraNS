#include "guest_address_space.h"
#include <algorithm>
#include <limits>
namespace astrans {
namespace {
bool has_permission(MemoryPermission available, MemoryPermission required) noexcept {
    const auto a = static_cast<uint8_t>(available);
    const auto r = static_cast<uint8_t>(required);
    return (a & r) == r;
}
bool valid_page_range(uint64_t address, std::size_t length) noexcept {
    return length != 0 && length % GuestAddressSpace::kPageSize == 0 &&
        address % GuestAddressSpace::kPageSize == 0 &&
        address <= std::numeric_limits<uint64_t>::max() - static_cast<uint64_t>(length);
}
}
GuestAddressSpace::GuestAddressSpace(GuestMemory& physical_memory) noexcept
    : physical_memory_(physical_memory) {}
bool GuestAddressSpace::map(uint64_t va, uint64_t pa, std::size_t length,
                            MemoryPermission permissions) {
    if (!valid_page_range(va, length) || !valid_page_range(pa, length) ||
        permissions == MemoryPermission::None || !physical_memory_.contains(pa, length)) return false;
    const std::size_t count = length / kPageSize;
    for (std::size_t i = 0; i < count; ++i)
        if (pages_.find(va + i * kPageSize) != pages_.end()) return false;
    for (std::size_t i = 0; i < count; ++i)
        pages_.emplace(va + i * kPageSize, PageMapping{pa + i * kPageSize, permissions});
    return true;
}
bool GuestAddressSpace::unmap(uint64_t va, std::size_t length) {
    if (!valid_page_range(va, length)) return false;
    const std::size_t count = length / kPageSize;
    for (std::size_t i = 0; i < count; ++i)
        if (pages_.find(va + i * kPageSize) == pages_.end()) return false;
    for (std::size_t i = 0; i < count; ++i) pages_.erase(va + i * kPageSize);
    return true;
}
bool GuestAddressSpace::protect(uint64_t va, std::size_t length, MemoryPermission permissions) {
    if (!valid_page_range(va, length)) return false;
    const std::size_t count = length / kPageSize;
    for (std::size_t i = 0; i < count; ++i)
        if (pages_.find(va + i * kPageSize) == pages_.end()) return false;
    for (std::size_t i = 0; i < count; ++i) pages_.at(va + i * kPageSize).permissions = permissions;
    return true;
}
bool GuestAddressSpace::translate(uint64_t va, MemoryPermission required, uint64_t& pa) const noexcept {
    const uint64_t page = va & ~(static_cast<uint64_t>(kPageSize) - 1U);
    const auto it = pages_.find(page);
    if (it == pages_.end() || !has_permission(it->second.permissions, required)) return false;
    const uint64_t offset = va - page;
    if (it->second.physical_page > std::numeric_limits<uint64_t>::max() - offset) return false;
    pa = it->second.physical_page + offset;
    return true;
}
std::size_t GuestAddressSpace::size() const noexcept { return pages_.size() * kPageSize; }
bool GuestAddressSpace::copy(uint64_t va, void* buffer, std::size_t length,
                             MemoryPermission required, bool writing) const noexcept {
    if (length == 0) return true;
    if (buffer == nullptr || va > std::numeric_limits<uint64_t>::max() -
        static_cast<uint64_t>(length - 1U)) return false;
    // Preflight every page first: failed cross-page writes must not partially commit.
    uint64_t cursor = va;
    std::size_t remaining = length;
    uint8_t probe = 0;
    while (remaining != 0) {
        uint64_t pa = 0;
        if (!translate(cursor, required, pa)) return false;
        const std::size_t offset = static_cast<std::size_t>(cursor & (kPageSize - 1U));
        const std::size_t chunk = std::min(remaining, kPageSize - offset);
        const bool allowed = writing ? physical_memory_.write(pa, &probe, 0)
                                     : physical_memory_.read(pa, &probe, 0);
        if (!allowed) return false;
        cursor += chunk;
        remaining -= chunk;
    }
    cursor = va;
    remaining = length;
    std::size_t copied = 0;
    auto* bytes = static_cast<uint8_t*>(buffer);
    while (remaining != 0) {
        uint64_t pa = 0;
        if (!translate(cursor, required, pa)) return false;
        const std::size_t offset = static_cast<std::size_t>(cursor & (kPageSize - 1U));
        const std::size_t chunk = std::min(remaining, kPageSize - offset);
        const bool ok = writing ? physical_memory_.write(pa, bytes + copied, chunk)
                                : physical_memory_.read(pa, bytes + copied, chunk);
        if (!ok) return false;
        cursor += chunk; copied += chunk; remaining -= chunk;
    }
    return true;
}
bool GuestAddressSpace::read(uint64_t va, void* dst, std::size_t length) const noexcept {
    return copy(va, dst, length, MemoryPermission::Read, false);
}
bool GuestAddressSpace::write(uint64_t va, const void* src, std::size_t length) noexcept {
    return copy(va, const_cast<void*>(src), length, MemoryPermission::Write, true);
}
bool GuestAddressSpace::fetch32(uint64_t va, uint32_t& value) const noexcept {
    if ((va & 3U) != 0) return false;
    uint64_t pa = 0;
    return translate(va, MemoryPermission::Execute, pa) && physical_memory_.fetch32(pa, value);
}
bool GuestAddressSpace::read32(uint64_t va, uint32_t& value) const noexcept {
    uint8_t raw[4]{};
    if (!read(va, raw, sizeof(raw))) return false;
    value = static_cast<uint32_t>(raw[0]) | (static_cast<uint32_t>(raw[1]) << 8U) |
        (static_cast<uint32_t>(raw[2]) << 16U) | (static_cast<uint32_t>(raw[3]) << 24U);
    return true;
}
bool GuestAddressSpace::write32(uint64_t va, uint32_t value) noexcept {
    const uint8_t raw[4] = {static_cast<uint8_t>(value), static_cast<uint8_t>(value >> 8U),
        static_cast<uint8_t>(value >> 16U), static_cast<uint8_t>(value >> 24U)};
    return write(va, raw, sizeof(raw));
}
bool GuestAddressSpace::read64(uint64_t va, uint64_t& value) const noexcept {
    uint8_t raw[8]{};
    if (!read(va, raw, sizeof(raw))) return false;
    value = 0;
    for (unsigned i = 0; i < 8; ++i) value |= static_cast<uint64_t>(raw[i]) << (i * 8U);
    return true;
}
bool GuestAddressSpace::write64(uint64_t va, uint64_t value) noexcept {
    uint8_t raw[8]{};
    for (unsigned i = 0; i < 8; ++i) raw[i] = static_cast<uint8_t>(value >> (i * 8U));
    return write(va, raw, sizeof(raw));
}
}  // namespace astrans
