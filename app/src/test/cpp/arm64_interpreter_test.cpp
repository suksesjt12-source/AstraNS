#include "arm64_interpreter.h"
#include "guest_address_space.h"
#include "nro_loader.h"
#include "nso_loader.h"
#include "mod0_relocator.h"

#include <cstdlib>
#include <iostream>

namespace {

uint32_t encode_add_sub_shifted(bool sf, bool subtract, bool set_flags,
                                 unsigned shift, unsigned rm, unsigned amount,
                                 unsigned rn, unsigned rd) {
    return (static_cast<uint32_t>(sf) << 31U) |
           (static_cast<uint32_t>(subtract) << 30U) |
           (static_cast<uint32_t>(set_flags) << 29U) | 0x0B000000U |
           (shift << 22U) | (rm << 16U) | (amount << 10U) |
           (rn << 5U) | rd;
}
uint32_t encode_logical_shifted(bool sf, unsigned opc, bool invert,
                                unsigned shift, unsigned rm, unsigned amount,
                                unsigned rn, unsigned rd) {
    return (static_cast<uint32_t>(sf) << 31U) | (opc << 29U) |
           0x0A000000U | (static_cast<uint32_t>(invert) << 21U) |
           (shift << 22U) | (rm << 16U) | (amount << 10U) |
           (rn << 5U) | rd;
}

uint32_t encode_logical_immediate(bool sf, unsigned opc, unsigned n,
                                  unsigned immr, unsigned imms,
                                  unsigned rn, unsigned rd) {
    return (static_cast<uint32_t>(sf) << 31U) | (opc << 29U) |
           0x12000000U | (n << 22U) | (immr << 16U) |
           (imms << 10U) | (rn << 5U) | rd;
}
uint32_t encode_compare_branch(bool sf, bool nonzero, unsigned immediate,
                               unsigned rt) {
    return (static_cast<uint32_t>(sf) << 31U) |
           (static_cast<uint32_t>(nonzero) << 24U) | 0x34000000U |
           ((immediate & 0x7FFFFU) << 5U) | rt;
}
uint32_t encode_test_branch(unsigned bit_number, bool nonzero,
                            unsigned immediate, unsigned rt) {
    return ((bit_number >> 5U) << 31U) |
           (static_cast<uint32_t>(nonzero) << 24U) | 0x36000000U |
           ((bit_number & 0x1FU) << 19U) |
           ((immediate & 0x3FFFU) << 5U) | rt;
}
uint32_t encode_add_sub_extended(bool sf, bool subtract, bool set_flags,
                                   unsigned rm, unsigned option, unsigned amount,
                                   unsigned rn, unsigned rd) {
    return (static_cast<uint32_t>(sf) << 31U) |
           (static_cast<uint32_t>(subtract) << 30U) |
           (static_cast<uint32_t>(set_flags) << 29U) | 0x0B200000U |
           (rm << 16U) | (option << 13U) | (amount << 10U) | (rn << 5U) | rd;
}
uint32_t encode_adr(bool page_relative, int64_t immediate, unsigned rd) {
    const uint64_t imm = static_cast<uint64_t>(immediate) & 0x1FFFFFU;
    return (page_relative ? 0x90000000U : 0x10000000U) |
           (static_cast<uint32_t>((imm & 0x3U) << 29U)) |
           (static_cast<uint32_t>(((imm >> 2U) & 0x7FFFFU) << 5U)) | rd;
}
uint8_t hex_byte(char a, char b) {
    auto digit=[](char c)->uint8_t {
        if(c>='0'&&c<='9') return static_cast<uint8_t>(c-'0');
        if(c>='a'&&c<='f') return static_cast<uint8_t>(c-'a'+10);
        return static_cast<uint8_t>(c-'A'+10);
    };
    return static_cast<uint8_t>((digit(a)<<4U)|digit(b));
}
void write_u32_le(std::vector<uint8_t>& data, std::size_t offset, uint32_t value) {
    data[offset] = static_cast<uint8_t>(value);
    data[offset + 1U] = static_cast<uint8_t>(value >> 8U);
    data[offset + 2U] = static_cast<uint8_t>(value >> 16U);
    data[offset + 3U] = static_cast<uint8_t>(value >> 24U);
}
uint32_t encode_madd(bool sf, bool subtract, unsigned rm, unsigned ra,
                     unsigned rn, unsigned rd) {
    return (static_cast<uint32_t>(sf) << 31U) |
           (static_cast<uint32_t>(subtract) << 15U) | 0x1B000000U |
           (rm << 16U) | (ra << 10U) | (rn << 5U) | rd;
}
uint32_t encode_unsigned_memory(unsigned size, unsigned opc, unsigned byte_offset,
                                unsigned rn, unsigned rt) {
    const unsigned scaled_offset = byte_offset >> size;
    return (size << 30U) | 0x39000000U | (opc << 22U) |
           ((scaled_offset & 0xFFFU) << 10U) | (rn << 5U) | rt;
}
uint32_t encode_pair(bool is_64_bit, bool load, unsigned mode, int immediate,
                      unsigned rt2, unsigned rn, unsigned rt) {
    const unsigned size = is_64_bit ? 2U : 0U;
    const unsigned scaled = static_cast<unsigned>(immediate / (is_64_bit ? 8 : 4)) & 0x7FU;
    return (size << 30U) | 0x28000000U | (mode << 23U) |
           (static_cast<uint32_t>(load) << 22U) | (scaled << 15U) |
           (rt2 << 10U) | (rn << 5U) | rt;
}
uint32_t encode_csel(bool sf, unsigned rm, unsigned condition, unsigned rn, unsigned rd) {
    return (static_cast<uint32_t>(sf) << 31U) | 0x1A800000U |
           (rm << 16U) | (condition << 12U) | (rn << 5U) | rd;
}
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
    using astrans::GuestAddressSpace;
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
    CpuState register_alu{};
    register_alu.pc = 0x1300U;
    register_alu.x[1] = 5U;
    register_alu.x[2] = 3U;
    check(execute_one(register_alu, memory,
          encode_add_sub_shifted(true, false, false, 0U, 2U, 2U, 1U, 3U)) == StepResult::Executed,
          "ADD X3, X1, X2, LSL #2");
    check(register_alu.x[3] == 17U, "shifted ADD result");
    check(execute_one(register_alu, memory,
          encode_add_sub_shifted(true, true, true, 0U, 1U, 0U, 3U, 4U)) == StepResult::Executed,
          "SUBS X4, X3, X1");
    check(register_alu.x[4] == 12U && register_alu.c && !register_alu.z && !register_alu.v,
          "shifted SUBS result and flags");
    check(execute_one(register_alu, memory,
          encode_logical_shifted(true, 1U, false, 0U, 2U, 0U, 1U, 5U)) == StepResult::Executed,
          "ORR X5, X1, X2");
    check(register_alu.x[5] == 7U, "ORR logical result");
    check(execute_one(register_alu, memory,
          encode_logical_shifted(true, 2U, false, 0U, 2U, 0U, 3U, 6U)) == StepResult::Executed,
          "EOR X6, X3, X2");
    check(register_alu.x[6] == 18U, "EOR logical result");
    check(execute_one(register_alu, memory,
          encode_logical_shifted(true, 3U, false, 0U, 2U, 0U, 1U, 7U)) == StepResult::Executed,
          "ANDS X7, X1, X2");
    check(register_alu.x[7] == 1U && !register_alu.z && !register_alu.c && !register_alu.v,
          "ANDS updates NZCV");
    check(execute_one(register_alu, memory,
          encode_add_sub_shifted(true, true, true, 0U, 1U, 0U, 1U, 31U)) == StepResult::Executed,
          "CMP register alias");
    check(register_alu.z && register_alu.c, "CMP sets zero and carry");
    check(execute_one(register_alu, memory,
          encode_csel(true, 2U, 0U, 1U, 8U)) == StepResult::Executed,
          "CSEL EQ");
    check(register_alu.x[8] == 5U, "CSEL chooses first operand when condition holds");
    check(execute_one(register_alu, memory,
          encode_csel(true, 2U, 1U, 1U, 9U)) == StepResult::Executed,
          "CSEL NE");
    check(register_alu.x[9] == 3U, "CSEL chooses second operand when condition fails");

    CpuState shifts{};
    shifts.pc = 0x1400U;
    shifts.x[1] = 0x8000000000000000ULL;
    check(execute_one(shifts, memory,
          encode_logical_shifted(true, 1U, false, 2U, 1U, 1U, 1U, 2U)) == StepResult::Executed,
          "ORR with ASR-shifted register");
    check(shifts.x[2] == 0xC000000000000000ULL, "ASR preserves sign bit");
    check(execute_one(shifts, memory,
          encode_logical_shifted(true, 1U, false, 1U, 1U, 1U, 31U, 3U)) == StepResult::Executed,
          "ORR with LSR-shifted register and XZR");
    check(shifts.x[3] == 0x4000000000000000ULL, "LSR inserts zero at high bit");
    check(execute_one(shifts, memory,
          encode_logical_shifted(true, 1U, false, 3U, 1U, 1U, 31U, 4U)) == StepResult::Executed,
          "ORR with ROR-shifted register");
    check(shifts.x[4] == 0x4000000000000000ULL, "ROR rotates across register width");
    check(execute_one(shifts, memory,
          encode_add_sub_shifted(false, false, false, 0U, 1U, 32U, 1U, 0U)) ==
          StepResult::InvalidEncoding, "32-bit shifted ADD rejects shift amounts >= 32");

    std::vector<uint8_t> nro_file(0x3000U, 0U);
    nro_file[0x10U] = 'N'; nro_file[0x11U] = 'R';
    nro_file[0x12U] = 'O'; nro_file[0x13U] = '0';
    write_u32_le(nro_file, 0x18U, 0x3000U);
    write_u32_le(nro_file, 0x20U, 0x0000U);
    write_u32_le(nro_file, 0x24U, 0x1000U);
    write_u32_le(nro_file, 0x28U, 0x1000U);
    write_u32_le(nro_file, 0x2CU, 0x1000U);
    write_u32_le(nro_file, 0x30U, 0x2000U);
    write_u32_le(nro_file, 0x34U, 0x1000U);
    write_u32_le(nro_file, 0x38U, 0x80U);
    write_u32_le(nro_file, 0x00U, 0x14000020U); // Entry stub branches to code at +0x80.
    write_u32_le(nro_file, 0x80U, 0xD2800540U); // MOVZ X0, #42
    write_u32_le(nro_file, 0x84U, 0xD65F03C0U); // RET X30
    nro_file[0x1000U] = 0xA5U;
    nro_file[0x2000U] = 0x5AU;
    GuestMemory nro_memory;
    astrans::LoadedNro loaded_nro{};
    astrans::NroLoadError nro_error = astrans::NroLoadError::None;
    check(astrans::load_nro(nro_memory, nro_file.data(), nro_file.size(),
                            0x100000U, loaded_nro, nro_error),
          "load valid uncompressed NRO image");
    check(nro_error == astrans::NroLoadError::None, "NRO load error state is clear");
    check(loaded_nro.entry_point == 0x100000U && loaded_nro.bss_start == 0x103000U &&
          loaded_nro.bss_size == 0x80U, "NRO entry point and BSS boundaries");
    check(nro_memory.fetch32(0x100000U, fetched) && fetched == 0x14000020U,
          "NRO text segment is executable");
    check(nro_memory.read(0x101000U, &fetched, 1U) && (fetched & 0xFFU) == 0xA5U,
          "NRO rodata bytes are mapped");
    check(!nro_memory.write32(0x101000U, 0U), "NRO rodata is read-only after load");
    check(nro_memory.read(0x102000U, &fetched, 1U) && (fetched & 0xFFU) == 0x5AU,
          "NRO data bytes are copied");
    check(nro_memory.write32(0x102000U, 0x12345678U), "NRO data remains writable");
    uint64_t bss_zero = 1U;
    check(nro_memory.read64(0x103000U, bss_zero) && bss_zero == 0U,
          "NRO BSS is zero-initialized");
    check(!nro_memory.write32(0x100000U, 0U), "NRO text is read-execute after load");
    CpuState nro_cpu{};
    nro_cpu.pc = loaded_nro.entry_point;
    nro_cpu.x[30] = 0x9000U;
    check(run(nro_cpu, nro_memory, 8U, 0x9000U) == StepResult::StopAddressReached,
          "execute loaded NRO entry stub through the AArch64 interpreter");
    check(nro_cpu.x[0] == 42U && nro_cpu.instructions_executed == 3U,
          "NRO entry branch, MOVZ and RET execute correctly");

    GuestMemory rollback_memory;
    check(rollback_memory.map(0x201000U, GuestMemory::kPageSize,
                              MemoryPermission::Read | MemoryPermission::Write),
          "reserve second NRO segment to force mapping collision");
    check(!astrans::load_nro(rollback_memory, nro_file.data(), nro_file.size(),
                             0x200000U, loaded_nro, nro_error) &&
          nro_error == astrans::NroLoadError::MappingFailed,
          "NRO loader reports segment mapping collision");
    check(!rollback_memory.contains(0x200000U, 4U) &&
          rollback_memory.contains(0x201000U, 4U),
          "NRO loader rolls back earlier segments without disturbing existing mapping");

    std::vector<uint8_t> invalid_nro = nro_file;
    invalid_nro[0x10U] = 'X';
    GuestMemory invalid_memory;
    check(!astrans::load_nro(invalid_memory, invalid_nro.data(), invalid_nro.size(),
                             0x200000U, loaded_nro, nro_error) &&
          nro_error == astrans::NroLoadError::BadMagic,
          "NRO loader rejects incorrect magic without mapping memory");
    std::vector<uint8_t> compressed_nro = nro_file;
    write_u32_le(compressed_nro, 0x1CU, 1U << 1U);
    check(!astrans::load_nro(invalid_memory, compressed_nro.data(), compressed_nro.size(),
                             0x200000U, loaded_nro, nro_error) &&
          nro_error == astrans::NroLoadError::CompressedNroUnsupported,
          "NRO loader explicitly rejects compressed images");

    GuestMemory mod0_memory;
    const auto mod0_rwx=MemoryPermission::Read|MemoryPermission::Write|MemoryPermission::Execute;
    check(mod0_memory.map(0x600000U,GuestMemory::kPageSize*4U,mod0_rwx),
          "map synthetic MOD0 image");
    check(mod0_memory.write32(0x600004U,0x100U),"write MOD0 relative pointer");
    const uint64_t mod0_address=0x600100U;
    const char mod0_magic[]={'M','O','D','0'};
    check(mod0_memory.write(mod0_address,mod0_magic,sizeof(mod0_magic)),"write MOD0 magic");
    check(mod0_memory.write32(mod0_address+4U,0x200U),"write MOD0 dynamic-section offset");
    check(mod0_memory.write32(mod0_address+8U,0x2F00U),"write MOD0 BSS start");
    check(mod0_memory.write32(mod0_address+12U,0x3000U),"write MOD0 BSS end");
    auto write_dynamic=[&](uint64_t address,uint64_t tag,uint64_t value) {
        return mod0_memory.write64(address,tag)&&mod0_memory.write64(address+8U,value);
    };
    const uint64_t dynamic=0x600300U;
    check(write_dynamic(dynamic,7U,0x400U),"write DT_RELA pointer");
    check(write_dynamic(dynamic+16U,8U,24U),"write DT_RELASZ");
    check(write_dynamic(dynamic+32U,9U,24U),"write DT_RELAENT");
    check(write_dynamic(dynamic+48U,36U,0x500U),"write DT_RELR pointer");
    check(write_dynamic(dynamic+64U,35U,8U),"write DT_RELRSZ");
    check(write_dynamic(dynamic+80U,37U,8U),"write DT_RELRENT");
    check(write_dynamic(dynamic+96U,0U,0U),"write DT_NULL");
    check(mod0_memory.write64(0x600400U,0x2000U),"write RELA target offset");
    check(mod0_memory.write64(0x600408U,1027U),"write R_AARCH64_RELATIVE info");
    check(mod0_memory.write64(0x600410U,0x1234U),"write RELA addend");
    check(mod0_memory.write64(0x600500U,0x2008U),"write RELR direct entry");
    check(mod0_memory.write64(0x602000U,0U),"zero RELA target");
    check(mod0_memory.write64(0x602008U,0x1000U),"initialize RELR target");
    std::array<uint8_t,0x100> bss_pattern{};
    bss_pattern.fill(0xABU);
    check(mod0_memory.write(0x603000U,bss_pattern.data(),bss_pattern.size()),
          "initialize BSS bytes before MOD0");
    astrans::Mod0Error mod0_error=astrans::Mod0Error::None;
    check(astrans::apply_mod0_relative_relocations(mod0_memory,0x600000U,
          GuestMemory::kPageSize*4U,GuestMemory::kPageSize,mod0_error),
          "apply MOD0 relative relocations");
    check(mod0_error==astrans::Mod0Error::None,"MOD0 relocation status");
    check(mod0_memory.read64(0x602000U,memory_value)&&memory_value==0x601234U,
          "RELA adds image base to signed addend");
    check(mod0_memory.read64(0x602008U,memory_value)&&memory_value==0x601000U,
          "RELR direct entry relocates target");
    check(mod0_memory.read64(0x603000U,memory_value)&&memory_value==0U,
          "MOD0 clears BSS");
    GuestMemory no_mod0_memory;
    check(no_mod0_memory.map(0x700000U,GuestMemory::kPageSize,
                             MemoryPermission::Read|MemoryPermission::Write|MemoryPermission::Execute),
          "map image without MOD0");
    check(no_mod0_memory.write32(0x700004U,0U),"write no-MOD0 pointer");
    check(astrans::apply_mod0_relative_relocations(no_mod0_memory,0x700000U,
          GuestMemory::kPageSize,GuestMemory::kPageSize,mod0_error)&&
          mod0_error==astrans::Mod0Error::NoModuleHeader,
          "image without MOD0 is reported separately");

    std::vector<uint8_t> nso_file(0x2130U,0U);
    nso_file[0]='N'; nso_file[1]='S'; nso_file[2]='O'; nso_file[3]='0';
    write_u32_le(nso_file,0x0CU,0x09U);
    write_u32_le(nso_file,0x10U,0x100U);
    write_u32_le(nso_file,0x14U,0x0000U);
    write_u32_le(nso_file,0x18U,0x1000U);
    write_u32_le(nso_file,0x20U,0x130U);
    write_u32_le(nso_file,0x24U,0x1000U);
    write_u32_le(nso_file,0x28U,0x1000U);
    write_u32_le(nso_file,0x30U,0x1130U);
    write_u32_le(nso_file,0x34U,0x2000U);
    write_u32_le(nso_file,0x38U,0x1000U);
    write_u32_le(nso_file,0x3CU,0x80U);
    write_u32_le(nso_file,0x60U,36U);
    write_u32_le(nso_file,0x64U,0x1000U);
    write_u32_le(nso_file,0x68U,0x1000U);
    std::size_t lz=0x100U;
    nso_file[lz++]=0xFFU;
    nso_file[lz++]=1U; // 16 literal bytes.
    const uint8_t text_prefix[16]={
        0x02U,0x00U,0x00U,0x14U, // B +0x10
        0x00U,0x00U,0x00U,0x00U, // MOD0 offset = 0 (no MOD0 header)
        0xE0U,0x00U,0x80U,0xD2U, // MOVZ X0, #7
        0xC0U,0x03U,0x5FU,0xD6U  // RET X30
    };
    for(uint8_t byte:text_prefix) nso_file[lz++]=byte;
    nso_file[lz++]=1U; nso_file[lz++]=0U;
    for(unsigned i=0;i<15U;++i) nso_file[lz++]=0xFFU;
    nso_file[lz++]=0xECU;
    std::fill(nso_file.begin()+0x130,nso_file.begin()+0x1130,0xA5U);
    std::fill(nso_file.begin()+0x1130,nso_file.end(),0x5AU);
    const char expected_text_hash[]="3b34fa39c06746e1718e3add6633aeaf40a7f58df6cd668ad208cb6afb69f5f0";
    for(unsigned i=0;i<32U;++i)
        nso_file[0xA0U+i]=hex_byte(expected_text_hash[i*2U],expected_text_hash[i*2U+1U]);

    GuestMemory nso_memory;
    astrans::LoadedNso loaded_nso{};
    astrans::NsoLoadError nso_error=astrans::NsoLoadError::None;
    const bool nso_loaded = astrans::load_nso(nso_memory,nso_file.data(),nso_file.size(),
                                                 0x400000U,loaded_nso,nso_error);
    if (!nso_loaded) {
        std::cerr << "NSO load error code: " << static_cast<unsigned>(nso_error) << std::endl;
    }
    check(nso_loaded, "load NSO with LZ4 text and requested SHA-256 validation");
    check(nso_error==astrans::NsoLoadError::None,"NSO loader success status");
    check(loaded_nso.entry_point==0x400000U&&loaded_nso.bss_start==0x403000U,
          "NSO entry point and BSS boundaries");
    check(nso_memory.fetch32(0x400000U,fetched)&&fetched==0x14000002U,
          "NSO LZ4 text is decompressed to executable guest memory");
    check(nso_memory.read(0x401000U,&fetched,1U)&&(fetched&0xFFU)==0xA5U,
          "NSO read-only data is copied");
    check(!nso_memory.write32(0x401000U,0U),"NSO rodata permission is read-only");
    check(nso_memory.read(0x402000U,&fetched,1U)&&(fetched&0xFFU)==0x5AU,
          "NSO data is copied");
    check(nso_memory.write32(0x402000U,0x76543210U),"NSO data permission is writable");
    CpuState nso_cpu{};
    nso_cpu.pc=loaded_nso.entry_point;
    nso_cpu.x[30]=0xA000U;
    check(run(nso_cpu,nso_memory,8U,0xA000U)==StepResult::StopAddressReached,
          "execute loaded NSO entry stub through the AArch64 interpreter");
    check(nso_cpu.x[0]==7U && nso_cpu.instructions_executed==3U,
          "NSO entry branch, MOVZ and RET execute correctly");
    bss_zero=1U;
    check(nso_memory.read64(0x403000U,bss_zero)&&bss_zero==0U,
          "NSO BSS is zero initialized");

    std::vector<uint8_t> bad_hash_nso=nso_file;
    bad_hash_nso[0xA0U]^=0xFFU;
    GuestMemory bad_hash_memory;
    check(!astrans::load_nso(bad_hash_memory,bad_hash_nso.data(),bad_hash_nso.size(),
                             0x500000U,loaded_nso,nso_error)&&
          nso_error==astrans::NsoLoadError::HashMismatch,
          "NSO bad SHA-256 is rejected before guest mapping");
    std::vector<uint8_t> zstd_nso=nso_file;
    write_u32_le(zstd_nso,0x0CU,0x89U);
    check(!astrans::load_nso(bad_hash_memory,zstd_nso.data(),zstd_nso.size(),
                             0x500000U,loaded_nso,nso_error)&&
          nso_error==astrans::NsoLoadError::UnsupportedCompression,
          "NSO Zstandard compression is reported as unsupported");

    CpuState multiply_cpu{};
    multiply_cpu.pc = 0x1780U;
    multiply_cpu.x[1] = 7U;
    multiply_cpu.x[2] = 3U;
    multiply_cpu.x[3] = 2U;
    check(execute_one(multiply_cpu, memory,
          encode_madd(true, false, 2U, 3U, 1U, 4U)) == StepResult::Executed,
          "MADD X4, X1, X2, X3");
    check(multiply_cpu.x[4] == 23U, "MADD multiplies and accumulates");
    check(execute_one(multiply_cpu, memory,
          encode_madd(true, true, 2U, 3U, 1U, 5U)) == StepResult::Executed,
          "MSUB X5, X1, X2, X3");
    check(multiply_cpu.x[5] == UINT64_MAX - 18U, "MSUB subtracts product from accumulator");
    check(execute_one(multiply_cpu, memory,
          encode_madd(true, false, 2U, 31U, 1U, 6U)) == StepResult::Executed,
          "MUL X6, X1, X2 alias");
    check(multiply_cpu.x[6] == 21U, "MUL alias multiplies registers");

    CpuState signed_loads{};
    signed_loads.pc = 0x1790U;
    signed_loads.x[1] = 0x2800U;
    const uint8_t byte_value = 0x80U;
    const uint8_t half_value[2] = {0x01U, 0x80U};
    const uint8_t word_value[4] = {0x00U, 0x00U, 0x00U, 0x80U};
    check(memory.write(0x2800U, &byte_value, 1U), "write signed byte test value");
    check(memory.write(0x2802U, half_value, sizeof(half_value)), "write signed halfword test value");
    check(memory.write(0x2804U, word_value, sizeof(word_value)), "write signed word test value");
    check(execute_one(signed_loads, memory,
          encode_unsigned_memory(0U, 2U, 0U, 1U, 2U)) == StepResult::Executed,
          "LDRSB X2, [X1]");
    check(signed_loads.x[2] == 0xFFFFFFFFFFFFFF80ULL, "LDRSB to X sign extends to 64 bits");
    check(execute_one(signed_loads, memory,
          encode_unsigned_memory(0U, 3U, 0U, 1U, 3U)) == StepResult::Executed,
          "LDRSB W3, [X1]");
    check(signed_loads.x[3] == 0xFFFFFF80ULL, "LDRSB to W sign extends to 32 bits");
    check(execute_one(signed_loads, memory,
          encode_unsigned_memory(1U, 2U, 2U, 1U, 4U)) == StepResult::Executed,
          "LDRSH X4, [X1, #2]");
    check(signed_loads.x[4] == 0xFFFFFFFFFFFF8001ULL, "LDRSH sign extends halfword");
    check(execute_one(signed_loads, memory,
          encode_unsigned_memory(2U, 2U, 4U, 1U, 5U)) == StepResult::Executed,
          "LDRSW X5, [X1, #4]");
    check(signed_loads.x[5] == 0xFFFFFFFF80000000ULL, "LDRSW sign extends word");
    check(execute_one(signed_loads, memory,
          encode_unsigned_memory(1U, 1U, 2U, 1U, 6U)) == StepResult::Executed,
          "LDRH W6, [X1, #2]");
    check(signed_loads.x[6] == 0x8001U, "unsigned LDRH zero extends halfword");

    CpuState logical_immediate_cpu{};
    logical_immediate_cpu.pc = 0x1800U;
    logical_immediate_cpu.x[1] = 0x123456789ABCDEF0ULL;
    check(execute_one(logical_immediate_cpu, memory,
          encode_logical_immediate(true, 0U, 1U, 0U, 7U, 1U, 2U)) == StepResult::Executed,
          "AND X2, X1, #0xFF");
    check(logical_immediate_cpu.x[2] == 0xF0U, "logical immediate AND mask");
    check(execute_one(logical_immediate_cpu, memory,
          encode_logical_immediate(true, 1U, 1U, 0U, 7U, 1U, 3U)) == StepResult::Executed,
          "ORR X3, X1, #0xFF");
    check(logical_immediate_cpu.x[3] == 0x123456789ABCDEFFULL, "logical immediate ORR");
    check(execute_one(logical_immediate_cpu, memory,
          encode_logical_immediate(true, 2U, 1U, 0U, 7U, 1U, 4U)) == StepResult::Executed,
          "EOR X4, X1, #0xFF");
    check(logical_immediate_cpu.x[4] == 0x123456789ABCDE0FULL, "logical immediate EOR");
    check(execute_one(logical_immediate_cpu, memory,
          encode_logical_immediate(true, 3U, 1U, 0U, 7U, 1U, 5U)) == StepResult::Executed,
          "ANDS X5, X1, #0xFF");
    check(logical_immediate_cpu.x[5] == 0xF0U && !logical_immediate_cpu.z &&
          !logical_immediate_cpu.c && !logical_immediate_cpu.v,
          "logical immediate ANDS flags");
    check(execute_one(logical_immediate_cpu, memory,
          encode_logical_immediate(false, 0U, 1U, 0U, 7U, 1U, 2U)) ==
          StepResult::InvalidEncoding, "32-bit logical immediate rejects N=1");

    CpuState flow_cpu{};
    flow_cpu.pc = 0x1900U;
    flow_cpu.x[1] = 0U;
    check(execute_one(flow_cpu, memory, encode_compare_branch(false, false, 2U, 1U)) ==
          StepResult::Executed, "CBZ W1, +8");
    check(flow_cpu.pc == 0x1908U, "CBZ takes branch for zero");
    flow_cpu.pc = 0x1900U;
    flow_cpu.x[1] = 1U;
    check(execute_one(flow_cpu, memory, encode_compare_branch(true, true, 2U, 1U)) ==
          StepResult::Executed, "CBNZ X1, +8");
    check(flow_cpu.pc == 0x1908U, "CBNZ takes branch for nonzero");
    flow_cpu.pc = 0x1A00U;
    flow_cpu.x[2] = 0U;
    check(execute_one(flow_cpu, memory, encode_test_branch(5U, false, 2U, 2U)) ==
          StepResult::Executed, "TBZ X2, #5, +8");
    check(flow_cpu.pc == 0x1A08U, "TBZ branches when tested bit is clear");
    flow_cpu.pc = 0x1A00U;
    flow_cpu.x[2] = 1ULL << 40U;
    check(execute_one(flow_cpu, memory, encode_test_branch(40U, true, 2U, 2U)) ==
          StepResult::Executed, "TBNZ X2, #40, +8");
    check(flow_cpu.pc == 0x1A08U, "TBNZ branches when high bit is set");
    flow_cpu.pc = 0x1B00U;
    flow_cpu.x[3] = 0x1C00U;
    check(execute_one(flow_cpu, memory, 0xD63F0000U | (3U << 5U)) == StepResult::Executed,
          "BLR X3");
    check(flow_cpu.pc == 0x1C00U && flow_cpu.x[30] == 0x1B04U,
          "BLR sets return address and branches");
    flow_cpu.x[3] = 0x1D00U;
    check(execute_one(flow_cpu, memory, 0xD61F0000U | (3U << 5U)) == StepResult::Executed,
          "BR X3");
    check(flow_cpu.pc == 0x1D00U && flow_cpu.x[30] == 0x1B04U,
          "BR branches without changing link register");

    CpuState address_cpu{};
    address_cpu.pc = 0x1600U;
    check(execute_one(address_cpu, memory, encode_adr(false, 16, 0U)) == StepResult::Executed,
          "ADR X0, PC + 16");
    check(address_cpu.x[0] == 0x1610U, "ADR computes PC-relative byte address");
    address_cpu.pc = 0x12345678U;
    check(execute_one(address_cpu, memory, encode_adr(true, 1, 1U)) == StepResult::Executed,
          "ADRP X1, PC page + 1");
    check(address_cpu.x[1] == 0x12346000U, "ADRP computes page-relative address");

    CpuState extended_alu{};
    extended_alu.pc = 0x1700U;
    extended_alu.x[1] = 10U;
    extended_alu.x[2] = 3U;
    check(execute_one(extended_alu, memory,
          encode_add_sub_extended(true, false, false, 2U, 2U, 2U, 1U, 3U)) == StepResult::Executed,
          "ADD X3, X1, W2, UXTW #2");
    check(extended_alu.x[3] == 22U, "UXTW extended ADD result");
    extended_alu.x[2] = 0xFFFFFFFFU;
    check(execute_one(extended_alu, memory,
          encode_add_sub_extended(true, false, false, 2U, 6U, 0U, 1U, 4U)) == StepResult::Executed,
          "ADD X4, X1, W2, SXTW");
    check(extended_alu.x[4] == 9U, "SXTW sign extends a negative word");
    extended_alu.x[2] = 0xFFU;
    check(execute_one(extended_alu, memory,
          encode_add_sub_extended(true, false, false, 2U, 0U, 0U, 1U, 5U)) == StepResult::Executed,
          "ADD X5, X1, W2, UXTB");
    check(extended_alu.x[5] == 265U, "UXTB zero extends a byte");
    extended_alu.x[2] = 0xFFFFFFFFU;
    check(execute_one(extended_alu, memory,
          encode_add_sub_extended(true, true, true, 2U, 6U, 0U, 1U, 6U)) == StepResult::Executed,
          "SUBS X6, X1, W2, SXTW");
    check(extended_alu.x[6] == 11U && !extended_alu.c && !extended_alu.z,
          "extended SUBS updates arithmetic flags including unsigned borrow");

    CpuState pair_cpu{};
    pair_cpu.pc = 0x1500U;
    pair_cpu.x[1] = 0x2800U;
    pair_cpu.x[2] = 0x1122334455667788ULL;
    pair_cpu.x[3] = 0x8877665544332211ULL;
    check(execute_one(pair_cpu, memory,
          encode_pair(true, false, 2U, 16, 3U, 1U, 2U)) == StepResult::Executed,
          "STP X2, X3, [X1, #16]");
    check(memory.read64(0x2810U, memory_value) && memory_value == pair_cpu.x[2],
          "STP writes first 64-bit register");
    check(memory.read64(0x2818U, memory_value) && memory_value == pair_cpu.x[3],
          "STP writes second 64-bit register");
    check(execute_one(pair_cpu, memory,
          encode_pair(true, true, 2U, 16, 5U, 1U, 4U)) == StepResult::Executed,
          "LDP X4, X5, [X1, #16]");
    check(pair_cpu.x[4] == pair_cpu.x[2] && pair_cpu.x[5] == pair_cpu.x[3],
          "LDP restores both registers");

    pair_cpu.sp = 0x2900U;
    check(execute_one(pair_cpu, memory, 0xA9BF7BFDU) == StepResult::Executed,
          "STP X29, X30, [SP, #-16]!");
    check(pair_cpu.sp == 0x28F0U, "pre-index pair store updates SP before access");
    pair_cpu.x[29] = 0U;
    pair_cpu.x[30] = 0U;
    check(execute_one(pair_cpu, memory, 0xA8C17BFDU) == StepResult::Executed,
          "LDP X29, X30, [SP], #16");
    check(pair_cpu.x[29] == 0U, "post-index pair load restores saved frame pointer");
    check(pair_cpu.x[30] == 0U && pair_cpu.sp == 0x2900U,
          "post-index pair load restores link register and SP");

    pair_cpu.x[6] = 0xFFFFFFFFU;
    pair_cpu.x[7] = 0x80000000U;
    check(execute_one(pair_cpu, memory,
          encode_pair(false, false, 2U, 8, 7U, 1U, 6U)) == StepResult::Executed,
          "store 32-bit register pair");
    check(execute_one(pair_cpu, memory,
          encode_pair(false, true, 2U, 8, 9U, 1U, 8U)) == StepResult::Executed,
          "LDP W8, W9, [X1, #8]");
    check(pair_cpu.x[8] == 0xFFFFFFFFU && pair_cpu.x[9] == 0x80000000U,
          "32-bit pair loads zero-extend");

    const uint64_t original_base = pair_cpu.x[1];
    check(execute_one(pair_cpu, memory,
          encode_pair(true, false, 3U, -16, 3U, 1U, 2U)) == StepResult::Executed,
          "pre-indexed STP pair");
    check(pair_cpu.x[1] == original_base - 16U, "pre-indexed pair writes back base");
    check(execute_one(pair_cpu, memory,
          encode_pair(true, true, 1U, 16, 5U, 1U, 4U)) == StepResult::Executed,
          "post-indexed LDP pair");
    check(pair_cpu.x[1] == original_base, "post-indexed pair writes back base");

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
