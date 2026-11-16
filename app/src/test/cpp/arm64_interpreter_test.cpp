#include "arm64_interpreter.h"

#include <cstdlib>
#include <iostream>

namespace {
void check(bool condition, const char* message) {
    if (!condition) {
        std::cerr << "FAIL: " << message << '\n';
        std::exit(EXIT_FAILURE);
    }
}
}  // namespace

int main() {
    using astrans::CpuState;
    using astrans::StepResult;
    using astrans::execute_one;

    CpuState cpu{};
    check(execute_one(cpu, 0xD28000E0U) == StepResult::Executed, "MOVZ X0, #7 decodes");
    check(cpu.x[0] == 7U && cpu.pc == 4U, "MOVZ updates register and PC");
    check(execute_one(cpu, 0x91001400U) == StepResult::Executed, "ADD X0, X0, #5 decodes");
    check(cpu.x[0] == 12U && cpu.pc == 8U, "ADD produces 12");
    check(execute_one(cpu, 0xD503201FU) == StepResult::Executed, "NOP decodes");
    check(cpu.pc == 12U, "NOP advances PC");

    check(execute_one(cpu, 0x529FFFE1U) == StepResult::Executed, "MOVZ W1, #65535 decodes");
    check(cpu.x[1] == 65535U, "32-bit MOVZ zero extends");
    check(execute_one(cpu, 0x11000421U) == StepResult::Executed, "ADD W1, W1, #1 decodes");
    check(cpu.x[1] == 65536U, "32-bit ADD writes a 32-bit result");

    cpu.x[3] = 0x12345678U;
    check(execute_one(cpu, 0xD65F0060U) == StepResult::Executed, "RET X3 decodes");
    check(cpu.pc == 0x12345678U, "RET branches to X3 without halting the CPU");
    check(execute_one(cpu, 0xD503201FU) == StepResult::Executed, "CPU can execute after a branch");
    check(cpu.pc == 0x1234567CU, "instruction after branch advances the new PC");

    CpuState unsupported{};
    check(execute_one(unsupported, 0xFFFFFFFFU) == StepResult::Unsupported, "unknown opcode is rejected");
    check(astrans::run_self_test() == 12, "JNI self-test program returns 12");

    std::cout << "AstraNS AArch64 tests: PASS\n";
    return EXIT_SUCCESS;
}
