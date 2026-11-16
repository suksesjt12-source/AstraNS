#include "arm64_interpreter.h"

namespace astrans {

StepResult execute_one(CpuState& cpu, uint32_t instruction) {
    if (instruction == 0xD503201FU) {  // NOP
        cpu.pc += 4;
        return StepResult::Executed;
    }

    // MOVZ Wd/Xd, #imm16, LSL #(0|16|32|48).
    if ((instruction & 0x7F800000U) == 0x52800000U) {
        const bool is_64_bit = (instruction & 0x80000000U) != 0;
        const unsigned rd = instruction & 0x1FU;
        const unsigned hw = (instruction >> 21U) & 0x3U;
        const uint64_t imm16 = (instruction >> 5U) & 0xFFFFU;

        if (!is_64_bit && hw >= 2U) return StepResult::InvalidEncoding;

        uint64_t value = imm16 << (hw * 16U);
        if (!is_64_bit) value &= 0xFFFFFFFFULL;
        if (rd != 31U) cpu.x[rd] = value;  // WZR/XZR discards the result.
        cpu.pc += 4;
        return StepResult::Executed;
    }

    // ADD (immediate), without flag updates. SP is not modelled yet.
    if ((instruction & 0x7F000000U) == 0x11000000U) {
        const bool is_64_bit = (instruction & 0x80000000U) != 0;
        const unsigned rd = instruction & 0x1FU;
        const unsigned rn = (instruction >> 5U) & 0x1FU;
        const uint64_t imm12 = (instruction >> 10U) & 0xFFFU;
        const unsigned shift = ((instruction >> 22U) & 0x1U) != 0 ? 12U : 0U;

        if (rd == 31U || rn == 31U) return StepResult::InvalidRegister;

        const uint64_t result = cpu.x[rn] + (imm12 << shift);
        cpu.x[rd] = is_64_bit ? result : (result & 0xFFFFFFFFULL);
        cpu.pc += 4;
        return StepResult::Executed;
    }

    // RET Xn. XZR is accepted as a branch target of zero.
    if ((instruction & 0xFFFFFC1FU) == 0xD65F0000U) {
        const unsigned rn = (instruction >> 5U) & 0x1FU;
        cpu.pc = rn == 31U ? 0U : cpu.x[rn];
        return StepResult::Executed;
    }

    return StepResult::Unsupported;
}

int run_self_test() {
    constexpr uint32_t program[] = {
        0xD28000E0U,  // MOVZ X0, #7
        0x91001400U,  // ADD X0, X0, #5
        0xD65F03C0U,  // RET X30
    };

    CpuState cpu{};
    for (const uint32_t instruction : program) {
        const StepResult result = execute_one(cpu, instruction);
        if (result != StepResult::Executed) return -1;
    }
    return cpu.pc == 0U && cpu.x[0] == 12U ? 12 : -1;
}

}  // namespace astrans
