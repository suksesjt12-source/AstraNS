#ifndef ASTRANS_ARM64_INTERPRETER_H
#define ASTRANS_ARM64_INTERPRETER_H

#include <array>
#include <cstddef>
#include <cstdint>
#include <vector>

namespace astrans {

enum class MemoryPermission : uint8_t {
    None = 0,
    Read = 1,
    Write = 2,
    Execute = 4,
};

constexpr MemoryPermission operator|(MemoryPermission left, MemoryPermission right) noexcept {
    return static_cast<MemoryPermission>(static_cast<uint8_t>(left) | static_cast<uint8_t>(right));
}

class GuestMemory {
public:
    static constexpr std::size_t kPageSize = 4096;

    GuestMemory() = default;
    bool map(uint64_t base_address, std::size_t size, MemoryPermission permissions);
    bool unmap(uint64_t base_address);
    bool protect(uint64_t base_address, std::size_t size, MemoryPermission permissions);
    std::size_t size() const noexcept;
    bool contains(uint64_t address, std::size_t length) const noexcept;
    bool read(uint64_t address, void* destination, std::size_t length) const noexcept;
    bool write(uint64_t address, const void* source, std::size_t length) noexcept;
    bool read32(uint64_t address, uint32_t& value) const noexcept;
    bool fetch32(uint64_t address, uint32_t& value) const noexcept;
    bool write32(uint64_t address, uint32_t value) noexcept;
    bool read64(uint64_t address, uint64_t& value) const noexcept;
    bool write64(uint64_t address, uint64_t value) noexcept;

private:
    struct Region {
        uint64_t base_address;
        std::vector<uint8_t> bytes;
        MemoryPermission permissions;
    };

    std::vector<Region> regions_;
};

struct CpuState {
    std::array<uint64_t, 31> x{};
    uint64_t sp = 0;
    uint64_t pc = 0;
    bool n = false;
    bool z = false;
    bool c = false;
    bool v = false;
    uint64_t instructions_executed = 0;
};

enum class StepResult {
    Executed,
    Unsupported,
    InvalidEncoding,
    MemoryFault,
    StopAddressReached,
    StepLimitReached,
};

StepResult execute_one(CpuState& cpu, GuestMemory& memory, uint32_t instruction);
StepResult run(CpuState& cpu, GuestMemory& memory, uint64_t max_steps,
               uint64_t stop_address = UINT64_MAX);
int run_self_test();

}  // namespace astrans

#endif  // ASTRANS_ARM64_INTERPRETER_H
