#ifndef ASTRANS_GUEST_ADDRESS_SPACE_H
#define ASTRANS_GUEST_ADDRESS_SPACE_H
#include "arm64_interpreter.h"
#include <cstddef>
#include <cstdint>
#include <map>
namespace astrans {
// Software virtual-to-physical mappings; not ARM hardware translation-table descriptors.
class GuestAddressSpace {
public:
    static constexpr std::size_t kPageSize = GuestMemory::kPageSize;
    explicit GuestAddressSpace(GuestMemory& physical_memory) noexcept;
    bool map(uint64_t virtual_address, uint64_t physical_address, std::size_t size, MemoryPermission permissions);
    bool unmap(uint64_t virtual_address, std::size_t size);
    bool protect(uint64_t virtual_address, std::size_t size, MemoryPermission permissions);
    bool translate(uint64_t virtual_address, MemoryPermission required, uint64_t& physical_address) const noexcept;
    std::size_t size() const noexcept;
    bool read(uint64_t virtual_address, void* destination, std::size_t length) const noexcept;
    bool write(uint64_t virtual_address, const void* source, std::size_t length) noexcept;
    bool fetch32(uint64_t virtual_address, uint32_t& value) const noexcept;
    bool read32(uint64_t virtual_address, uint32_t& value) const noexcept;
    bool write32(uint64_t virtual_address, uint32_t value) noexcept;
    bool read64(uint64_t virtual_address, uint64_t& value) const noexcept;
    bool write64(uint64_t virtual_address, uint64_t value) noexcept;
private:
    struct PageMapping { uint64_t physical_page; MemoryPermission permissions; };
    bool copy(uint64_t virtual_address, void* buffer, std::size_t length,
              MemoryPermission required, bool write_access) const noexcept;
    GuestMemory& physical_memory_;
    std::map<uint64_t, PageMapping> pages_;
};
}  // namespace astrans
#endif  // ASTRANS_GUEST_ADDRESS_SPACE_H
