#include "arm64_interpreter.h"

#include <cstring>

namespace astrans {

GuestMemory::GuestMemory(uint64_t base_address, std::size_t size)
    : base_address_(base_address), bytes_(size, 0) {}

uint64_t GuestMemory::base_address() const noexcept { return base_address_; }
std::size_t GuestMemory::size() const noexcept { return bytes_.size(); }

bool GuestMemory::contains(uint64_t address, std::size_t length) const noexcept {
    if (address < base_address_) return false;
    const uint64_t offset64 = address - base_address_;
    if (offset64 > static_cast<uint64_t>(bytes_.size())) return false;
    const std::size_t offset = static_cast<std::size_t>(offset64);
    return length <= bytes_.size() - offset;
}

bool GuestMemory::read(uint64_t address, void* destination, std::size_t length) const noexcept {
    if (destination == nullptr && length != 0) return false;
    if (!contains(address, length)) return false;
    const std::size_t offset = static_cast<std::size_t>(address - base_address_);
    if (length != 0) std::memcpy(destination, bytes_.data() + offset, length);
    return true;
}

bool GuestMemory::write(uint64_t address, const void* source, std::size_t length) noexcept {
    if (source == nullptr && length != 0) return false;
    if (!contains(address, length)) return false;
    const std::size_t offset = static_cast<std::size_t>(address - base_address_);
    if (length != 0) std::memcpy(bytes_.data() + offset, source, length);
    return true;
}

bool GuestMemory::read32(uint64_t address, uint32_t& value) const noexcept {
    uint8_t raw[4]{};
    if (!read(address, raw, sizeof(raw))) return false;
    value = static_cast<uint32_t>(raw[0]) |
            (static_cast<uint32_t>(raw[1]) << 8U) |
            (static_cast<uint32_t>(raw[2]) << 16U) |
            (static_cast<uint32_t>(raw[3]) << 24U);
    return true;
}

bool GuestMemory::write32(uint64_t address, uint32_t value) noexcept {
    const uint8_t raw[4] = {
        static_cast<uint8_t>(value), static_cast<uint8_t>(value >> 8U),
        static_cast<uint8_t>(value >> 16U), static_cast<uint8_t>(value >> 24U)};
    return write(address, raw, sizeof(raw));
}

bool GuestMemory::read64(uint64_t address, uint64_t& value) const noexcept {
    uint8_t raw[8]{};
    if (!read(address, raw, sizeof(raw))) return false;
    value = 0;
    for (unsigned i = 0; i < 8; ++i) value |= static_cast<uint64_t>(raw[i]) << (i * 8U);
    return true;
}

bool GuestMemory::write64(uint64_t address, uint64_t value) noexcept {
    uint8_t raw[8]{};
    for (unsigned i = 0; i < 8; ++i) raw[i] = static_cast<uint8_t>(value >> (i * 8U));
    return write(address, raw, sizeof(raw));
}

namespace {

uint64_t read_reg(const CpuState& cpu, unsigned index, bool use_sp) {
    if (index == 31U) return use_sp ? cpu.sp : 0U;
    return cpu.x[index];
}

void write_reg(CpuState& cpu, unsigned index, uint64_t value, bool is_64_bit,
               bool use_sp) {
    if (!is_64_bit) value &= 0xFFFFFFFFULL;
    if (index == 31U) {
        if (use_sp) cpu.sp = value;
        return;
    }
    cpu.x[index] = value;
}

int64_t sign_extend(uint64_t value, unsigned width) {
    const uint64_t sign_bit = uint64_t{1} << (width - 1U);
    int64_t result = static_cast<int64_t>(value);
    if ((value & sign_bit) != 0)
        result -= static_cast<int64_t>(uint64_t{1} << width);
    return result;
}

bool condition_holds(const CpuState& cpu, unsigned condition) {
    switch (condition & 0xFU) {
        case 0x0: return cpu.z;
        case 0x1: return !cpu.z;
        case 0x2: return cpu.c;
        case 0x3: return !cpu.c;
        case 0x4: return cpu.n;
        case 0x5: return !cpu.n;
        case 0x6: return cpu.v;
        case 0x7: return !cpu.v;
        case 0x8: return cpu.c && !cpu.z;
        case 0x9: return !cpu.c || cpu.z;
        case 0xA: return cpu.n == cpu.v;
        case 0xB: return cpu.n != cpu.v;
        case 0xC: return !cpu.z && (cpu.n == cpu.v);
        case 0xD: return cpu.z || (cpu.n != cpu.v);
        case 0xE: return true;
        default: return true;
    }
}

void set_nz(CpuState& cpu, uint64_t value, bool is_64_bit) {
    const uint64_t masked = is_64_bit ? value : (value & 0xFFFFFFFFULL);
    const unsigned sign_position = is_64_bit ? 63U : 31U;
    cpu.n = ((masked >> sign_position) & 1U) != 0;
    cpu.z = masked == 0;
}

}  // namespace

StepResult execute_one(CpuState& cpu, GuestMemory& memory, uint32_t instruction) {
    const uint64_t old_pc = cpu.pc;

    if (instruction == 0xD503201FU) {
        cpu.pc += 4U;
        return StepResult::Executed;
    }

    // MOVZ, MOVN and MOVK (W/X register forms).
    const uint32_t move_opcode = instruction & 0x7F800000U;
    if (move_opcode == 0x52800000U || move_opcode == 0x12800000U ||
        move_opcode == 0x72800000U) {
        const bool is_64_bit = (instruction & 0x80000000U) != 0;
        const unsigned rd = instruction & 0x1FU;
        const unsigned shift_index = (instruction >> 21U) & 0x3U;
        if (!is_64_bit && shift_index >= 2U) return StepResult::InvalidEncoding;
        const unsigned shift = shift_index * 16U;
        const uint64_t immediate = (instruction >> 5U) & 0xFFFFU;
        const uint64_t width_mask = is_64_bit ? UINT64_MAX : 0xFFFFFFFFULL;
        const uint64_t shifted = (immediate << shift) & width_mask;
        uint64_t result = 0;
        if (move_opcode == 0x52800000U) {
            result = shifted;
        } else if (move_opcode == 0x12800000U) {
            result = (~shifted) & width_mask;
        } else {
            const uint64_t field_mask = (0xFFFFULL << shift) & width_mask;
            result = (read_reg(cpu, rd, false) & ~field_mask) | shifted;
        }
        write_reg(cpu, rd, result, is_64_bit, false);
        cpu.pc += 4U;
        return StepResult::Executed;
    }

    // ADD/SUB (immediate), including flag-setting forms and SP operands.
    if ((instruction & 0x1F000000U) == 0x11000000U) {
        const bool is_64_bit = (instruction & 0x80000000U) != 0;
        const bool subtract = (instruction & 0x40000000U) != 0;
        const bool set_flags = (instruction & 0x20000000U) != 0;
        const unsigned rd = instruction & 0x1FU;
        const unsigned rn = (instruction >> 5U) & 0x1FU;
        const uint64_t immediate = (instruction >> 10U) & 0xFFFU;
        const unsigned shift = ((instruction >> 22U) & 1U) != 0 ? 12U : 0U;
        const uint64_t width_mask = is_64_bit ? UINT64_MAX : 0xFFFFFFFFULL;
        const uint64_t lhs = read_reg(cpu, rn, true) & width_mask;
        const uint64_t rhs = (immediate << shift) & width_mask;
        const uint64_t result = subtract ? (lhs - rhs) & width_mask : (lhs + rhs) & width_mask;
        write_reg(cpu, rd, result, is_64_bit, !set_flags);
        if (set_flags) {
            set_nz(cpu, result, is_64_bit);
            if (subtract) {
                cpu.c = lhs >= rhs;
                const uint64_t sign = is_64_bit ? (uint64_t{1} << 63U) : (uint64_t{1} << 31U);
                cpu.v = (((lhs ^ rhs) & (lhs ^ result) & sign) != 0);
            } else {
                cpu.c = result < lhs;
                const uint64_t sign = is_64_bit ? (uint64_t{1} << 63U) : (uint64_t{1} << 31U);
                cpu.v = (((~(lhs ^ rhs)) & (lhs ^ result) & sign) != 0);
            }
        }
        cpu.pc += 4U;
        return StepResult::Executed;
    }

    // Integer load/store, unsigned immediate, byte/halfword/word/doubleword.
    if ((instruction & 0x3B000000U) == 0x39000000U) {
        const unsigned size_code = (instruction >> 30U) & 0x3U;
        const unsigned access_size = 1U << size_code;
        const unsigned opc = (instruction >> 22U) & 0x3U;
        if (opc > 1U) return StepResult::Unsupported;
        const bool load = opc == 1U;
        const unsigned rt = instruction & 0x1FU;
        const unsigned rn = (instruction >> 5U) & 0x1FU;
        const uint64_t offset = ((instruction >> 10U) & 0xFFFU) << size_code;
        const uint64_t base = read_reg(cpu, rn, true);
        if (base > UINT64_MAX - offset) return StepResult::MemoryFault;
        const uint64_t address = base + offset;
        uint64_t value = 0;
        if (load) {
            uint8_t raw[8]{};
            if (!memory.read(address, raw, access_size)) return StepResult::MemoryFault;
            for (unsigned i = 0; i < access_size; ++i)
                value |= static_cast<uint64_t>(raw[i]) << (i * 8U);
            write_reg(cpu, rt, value, access_size == 8U, false);
        } else {
            value = read_reg(cpu, rt, false);
            uint8_t raw[8]{};
            for (unsigned i = 0; i < access_size; ++i)
                raw[i] = static_cast<uint8_t>(value >> (i * 8U));
            if (!memory.write(address, raw, access_size)) return StepResult::MemoryFault;
        }
        cpu.pc += 4U;
        return StepResult::Executed;
    }

    // B / BL, signed PC-relative 26-bit word offset.
    if ((instruction & 0x7C000000U) == 0x14000000U) {
        const int64_t offset = sign_extend(instruction & 0x03FFFFFFU, 26U) * 4;
        if ((instruction & 0x80000000U) != 0) cpu.x[30] = old_pc + 4U;
        cpu.pc = static_cast<uint64_t>(static_cast<int64_t>(old_pc) + offset);
        return StepResult::Executed;
    }

    // B.cond, signed PC-relative 19-bit word offset.
    if ((instruction & 0xFF000010U) == 0x54000000U) {
        const unsigned condition = instruction & 0xFU;
        const int64_t offset = sign_extend((instruction >> 5U) & 0x7FFFFU, 19U) * 4;
        cpu.pc = condition_holds(cpu, condition)
            ? static_cast<uint64_t>(static_cast<int64_t>(old_pc) + offset)
            : old_pc + 4U;
        return StepResult::Executed;
    }

    if ((instruction & 0xFFFFFC1FU) == 0xD65F0000U) {
        const unsigned rn = (instruction >> 5U) & 0x1FU;
        cpu.pc = read_reg(cpu, rn, false);
        return StepResult::Executed;
    }

    return StepResult::Unsupported;
}

StepResult run(CpuState& cpu, GuestMemory& memory, uint64_t max_steps,
               uint64_t stop_address) {
    uint64_t steps = 0;
    while (steps < max_steps) {
        if (cpu.pc == stop_address) return StepResult::StopAddressReached;
        uint32_t instruction = 0;
        if (!memory.read32(cpu.pc, instruction)) return StepResult::MemoryFault;
        const StepResult result = execute_one(cpu, memory, instruction);
        if (result != StepResult::Executed) return result;
        ++steps;
        ++cpu.instructions_executed;
    }
    return StepResult::StepLimitReached;
}

int run_self_test() {
    constexpr uint64_t kCodeBase = 0x1000U;
    constexpr uint64_t kDataBase = 0x2000U;
    GuestMemory memory(kCodeBase, 0x2000U);
    constexpr uint32_t program[] = {
        0xD28000E0U,  // MOVZ X0, #7
        0x91001400U,  // ADD X0, X0, #5
        0xF9000020U,  // STR X0, [X1]
        0xF9400022U,  // LDR X2, [X1]
        0x94000004U,  // BL +16 bytes to subroutine
        0xD503201FU,  // NOP after returning from subroutine
        0xD503201FU,  // Stop address follows this instruction
        0xD503201FU,  // Padding
        0x91000442U,  // ADD X2, X2, #1 (subroutine)
        0xD65F03C0U,  // RET
    };
    for (std::size_t i = 0; i < sizeof(program) / sizeof(program[0]); ++i) {
        if (!memory.write32(kCodeBase + i * 4U, program[i])) return -1;
    }
    CpuState cpu{};
    cpu.pc = kCodeBase;
    cpu.x[1] = kDataBase;
    const StepResult result = run(cpu, memory, 32U, kCodeBase + 24U);
    uint64_t stored = 0;
    const bool memory_ok = memory.read64(kDataBase, stored);
    if (result != StepResult::StopAddressReached || cpu.x[0] != 12U ||
        cpu.x[2] != 13U || cpu.x[30] != kCodeBase + 20U ||
        cpu.instructions_executed != 8U || !memory_ok || stored != 12U) {
        return -1;
    }
    return 12;
}

}  // namespace astrans
