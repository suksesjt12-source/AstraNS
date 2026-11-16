#ifndef ASTRANS_ARM64_INTERPRETER_H
#define ASTRANS_ARM64_INTERPRETER_H

#include <array>
#include <cstdint>

namespace astrans {

struct CpuState {
    std::array<uint64_t, 31> x{};
    uint64_t pc = 0;
};

enum class StepResult {
    Executed,
    Unsupported,
    InvalidRegister,
    InvalidEncoding,
};

StepResult execute_one(CpuState& cpu, uint32_t instruction);
int run_self_test();

}  // namespace astrans

#endif  // ASTRANS_ARM64_INTERPRETER_H
