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
}

int main() {
    using astrans::CpuState;
    using astrans::GuestMemory;
    using astrans::StepResult;
    using astrans::execute_one;
    using astrans::run;

    GuestMemory memory(0x1000U, 0x1000U);
    check(memory.write64(0x1800U, 0x1122334455667788ULL), "memory write64");
    uint64_t memory_value = 0;
    check(memory.read64(0x1800U, memory_value), "memory read64");
    check(memory_value == 0x1122334455667788ULL, "memory is little endian");
    check(!memory.read64(0x1FFCU, memory_value), "memory rejects out-of-range read");
    check(!memory.write32(0x0FFEU, 0U), "memory rejects address below base");

    CpuState cpu{};
    cpu.pc = 0x1000U;
    cpu.x[1] = 0x1800U;
    check(execute_one(cpu, memory, 0xD28000E0U) == StepResult::Executed, "MOVZ X0, #7");
    check(cpu.x[0] == 7U && cpu.pc == 0x1004U, "MOVZ writes register and advances PC");
    check(execute_one(cpu, memory, 0x91001400U) == StepResult::Executed, "ADD X0, X0, #5");
    check(cpu.x[0] == 12U, "ADD result is 12");

    check(execute_one(cpu, memory, 0xF9000020U) == StepResult::Executed, "STR X0, [X1]");
    uint64_t stored = 0;
    check(memory.read64(cpu.x[1], stored) && stored == 12U, "STR writes guest memory");
    check(execute_one(cpu, memory, 0xF9400022U) == StepResult::Executed, "LDR X2, [X1]");
    check(cpu.x[2] == 12U, "LDR reads guest memory");

    check(execute_one(cpu, memory, 0x529FFFE1U) == StepResult::Executed, "MOVZ W1, #65535");
    check(cpu.x[1] == 65535U, "32-bit MOVZ zero extends");
    check(execute_one(cpu, memory, 0x11000421U) == StepResult::Executed, "ADD W1, W1, #1");
    check(cpu.x[1] == 65536U, "32-bit ADD result");
    check(execute_one(cpu, memory, 0xF1000021U) == StepResult::Executed, "SUBS X1, X1, #0");
    check(cpu.c && !cpu.z, "SUBS updates carry and zero flags");

    CpuState moves{};
    moves.pc = 0x1000U;
    check(execute_one(moves, memory, 0x92800020U) == StepResult::Executed, "MOVN X0, #1");
    check(moves.x[0] == 0xFFFFFFFFFFFFFFFEULL, "MOVN inverts the immediate");
    check(execute_one(moves, memory, 0xF2A24680U) == StepResult::Executed, "MOVK X0, #0x1234, LSL #16");
    check(moves.x[0] == 0xFFFFFFFF1234FFFEULL, "MOVK replaces only the selected halfword");

    CpuState conditional{};
    conditional.pc = 0x1100U;
    conditional.x[1] = 7U;
    check(execute_one(conditional, memory, 0xF1001C21U) == StepResult::Executed, "SUBS X1, X1, #7");
    check(conditional.z && conditional.c, "SUBS sets zero and carry flags");
    check(execute_one(conditional, memory, 0x54000040U) == StepResult::Executed, "B.EQ +8");
    check(conditional.pc == 0x110CU, "taken conditional branch updates PC");

    cpu.pc = 0x1200U;
    check(execute_one(cpu, memory, 0x94000002U) == StepResult::Executed, "BL +8");
    check(cpu.x[30] == 0x1204U && cpu.pc == 0x1208U, "BL stores return address");
    check(execute_one(cpu, memory, 0xD65F03C0U) == StepResult::Executed, "RET X30");
    check(cpu.pc == 0x1204U, "RET branches through X30");

    CpuState loop{};
    loop.pc = 0x1000U;
    check(memory.write32(0x1000U, 0x14000000U), "write B #0");
    check(run(loop, memory, 4U) == StepResult::StepLimitReached, "run loop enforces step limit");
    check(loop.instructions_executed == 4U && loop.pc == 0x1000U, "branch loop state");

    CpuState fault{};
    fault.pc = 0x3000U;
    check(run(fault, memory, 2U) == StepResult::MemoryFault,
          "fetch outside guest memory faults safely");

    CpuState bad{};
    bad.pc = 0x1000U;
    check(execute_one(bad, memory, 0xFFFFFFFFU) == StepResult::Unsupported, "unknown opcode rejected");
    check(astrans::run_self_test() == 12, "integrated guest program passes");

    std::cout << "AstraNS memory + AArch64 execution tests: PASS\n";
    return EXIT_SUCCESS;
}
