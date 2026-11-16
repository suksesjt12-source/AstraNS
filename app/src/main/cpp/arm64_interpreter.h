#ifndef ASTRANS_ARM64_INTERPRETER_H
#define ASTRANS_ARM64_INTERPRETER_H

#include <array>
#include <cstddef>
#include <cstdint>
#include <vector>

namespace astrans {

class GuestMemory {
public:
    GuestMemory(uint64_t base_address, std::size_t size);

    uint64_t base_address() const noexcept;
    std::size_t size() const noexcept;
    bool contains(uint64_t address, std::size_t length) const noexcept;
    bool read(uint64_t address, void* destination, std::size_t length) const noexcept;
    bool write(uint64_t address, const void* source, std::size_t length) noexcept;
    bool read32(uint64_t address, uint32_t& value) const noexcept;
    bool write32(uint64_t address, uint32_t value) noexcept;
    bool read64(uint64_t address, uint64_t& value) const noexcept;
    bool write64(uint64_t address, uint64_t value) noexcept;

private:
    uint64_t base_address_;
    std::vector<uint8_t> bytes_;
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
