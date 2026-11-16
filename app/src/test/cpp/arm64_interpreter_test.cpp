#include "arm64_interpreter.h"
#include "guest_address_space.h"

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
    using astrans::MemoryPermission;
    using astrans::StepResult;
    using astrans::execute_one;
    using astrans::run;

    GuestMemory memory;
    const auto code_rw = MemoryPermission::Read | MemoryPermission::Write | MemoryPermission::Execute;
    const auto data_rw = MemoryPermission::Read | MemoryPermission::Write;
    check(memory.map(0x1000U, GuestMemory::kPageSize, code_rw), "map code page");
    check(memory.map(0x2000U, GuestMemory::kPageSize, data_rw), "map data page");
    check(!memory.map(0x1000U, GuestMemory::kPageSize, code_rw), "reject overlapping mapping");
    check(!memory.map(0x3100U, GuestMemory::kPageSize, code_rw), "reject unaligned mapping");
    check(memory.write64(0x2800U, 0x1122334455667788ULL), "memory write64");
    uint64_t memory_value = 0;
    check(memory.read64(0x2800U, memory_value), "memory read64");
    check(memory_value == 0x1122334455667788ULL, "memory is little endian");
    check(!memory.read64(0x2FFCU, memory_value), "memory rejects out-of-range read");
    check(!memory.write32(0x0FFEU, 0U), "memory rejects address below mapping");
    check(memory.write32(0x1000U, 0xD503201FU), "write guest code before protecting page");
    uint32_t fetched = 0;
    check(memory.fetch32(0x1000U, fetched) && fetched == 0xD503201FU, "executable page can be fetched");
    check(memory.protect(0x1000U, GuestMemory::kPageSize,
                         MemoryPermission::Read | MemoryPermission::Execute),
          "protect code page as read-execute");
    check(!memory.write32(0x1000U, 0U), "read-execute page blocks writes");
    check(memory.fetch32(0x1000U, fetched), "read-execute page still fetches");
    check(!memory.fetch32(0x2000U, fetched), "non-executable data page blocks instruction fetch");
    check(memory.protect(0x1000U, GuestMemory::kPageSize, code_rw), "restore code test page permissions");

    CpuState cpu{};
    cpu.pc = 0x1000U;
    cpu.x[1] = 0x2800U;
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
    fault.pc = 0x4000U;
    check(run(fault, memory, 2U) == StepResult::MemoryFault,
          "fetch outside guest memory faults safely");

    CpuState bad{};
    bad.pc = 0x1000U;
    check(execute_one(bad, memory, 0xFFFFFFFFU) == StepResult::Unsupported, "unknown opcode rejected");
    check(astrans::run_self_test() == 12, "integrated guest program passes");

    GuestMemory physical;
    const auto physical_rwx = MemoryPermission::Read | MemoryPermission::Write | MemoryPermission::Execute;
    check(physical.map(0x8000U, GuestMemory::kPageSize * 3U, physical_rwx), "map physical pages");
    GuestAddressSpace address_space(physical);
    const auto virtual_rw = MemoryPermission::Read | MemoryPermission::Write;
    check(address_space.map(0x400000U, 0x8000U, GuestMemory::kPageSize * 2U, virtual_rw), "map virtual pages");
    check(!address_space.map(0x400000U, 0xA000U, GuestMemory::kPageSize, virtual_rw), "reject virtual overlap");
    check(address_space.size() == GuestMemory::kPageSize * 2U, "count mapped virtual pages");
    check(address_space.write64(0x400FFCU, 0xA1B2C3D4E5F60718ULL), "write across page boundary");
    uint64_t alias_value = 0;
    check(address_space.read64(0x400FFCU, alias_value) && alias_value == 0xA1B2C3D4E5F60718ULL,
          "read across page boundary");
    check(address_space.map(0x500000U, 0x8000U, GuestMemory::kPageSize * 2U, MemoryPermission::Read),
          "map read-only alias");
    check(address_space.read64(0x500FFCU, alias_value) && alias_value == 0xA1B2C3D4E5F60718ULL,
          "alias sees same physical bytes");
    check(!address_space.write32(0x500000U, 9U), "read-only mapping blocks writes");
    uint64_t translated = 0;
    check(address_space.translate(0x400123U, MemoryPermission::Read, translated) && translated == 0x8123U,
          "virtual-to-physical translation");
    check(!address_space.translate(0x400123U, MemoryPermission::Execute, translated),
          "translation enforces execute permission");
    check(address_space.protect(0x400000U, GuestMemory::kPageSize, MemoryPermission::Read),
          "protect virtual page");
    check(!address_space.write32(0x400010U, 99U), "protected page blocks writes");
    check(address_space.unmap(0x400000U, GuestMemory::kPageSize), "unmap virtual page");
    check(!address_space.read32(0x400010U, fetched), "unmapped virtual page faults");

    check(address_space.map(0x600000U, 0xA000U, GuestMemory::kPageSize,
          MemoryPermission::Read | MemoryPermission::Execute), "map executable virtual page");
    check(physical.write32(0xA000U, 0xD28000E0U), "write guest MOVZ instruction");
    check(address_space.fetch32(0x600000U, fetched) && fetched == 0xD28000E0U,
          "instruction fetch uses virtual translation");
    CpuState virtual_cpu{};
    virtual_cpu.pc = 0x600000U;
    check(run(virtual_cpu, address_space, 2U, 0x600004U) == StepResult::StopAddressReached,
          "CPU run loop executes through virtual memory");
    check(virtual_cpu.x[0] == 7U, "virtual instruction updates CPU register");

    check(address_space.map(0x700000U, 0x8000U, GuestMemory::kPageSize, virtual_rw),
          "map page for fault-atomicity test");
    check(address_space.write32(0x700FFCU, 0x11223344U), "initialize last word in page");
    check(!address_space.write64(0x700FFCU, 0xFFEEDDCCBBAA9988ULL),
          "cross-page write faults on unmapped next page");
    check(address_space.read32(0x700FFCU, fetched) && fetched == 0x11223344U,
          "failed cross-page write leaves old data intact");
    check(!address_space.unmap(0x700000U, GuestMemory::kPageSize * 2U),
          "unmap rejects partially unmapped range");

    std::cout << "AstraNS guest memory + virtual MMU + AArch64 tests: PASS\n";
    return EXIT_SUCCESS;
}
