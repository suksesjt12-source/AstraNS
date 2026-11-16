#include "arm64_interpreter.h"
#include "guest_address_space.h"

#include <cstring>

namespace astrans {

namespace {

bool permission_allows(MemoryPermission available, MemoryPermission required) {
    const auto bits = static_cast<uint8_t>(available);
    const auto requested = static_cast<uint8_t>(required);
    return (bits & requested) == requested;
}

bool region_contains(uint64_t base, std::size_t region_size,
                     uint64_t address, std::size_t length) {
    if (address < base) return false;
    const uint64_t offset64 = address - base;
    if (offset64 > static_cast<uint64_t>(region_size)) return false;
    const std::size_t offset = static_cast<std::size_t>(offset64);
    return length <= region_size - offset;
}

}  // namespace

bool GuestMemory::map(uint64_t base_address, std::size_t region_size,
                      MemoryPermission permissions) {
    if (region_size == 0 || region_size % kPageSize != 0 ||
        base_address % kPageSize != 0 || permissions == MemoryPermission::None) {
        return false;
    }
    const uint64_t size64 = static_cast<uint64_t>(region_size);
    if (base_address > UINT64_MAX - size64) return false;
    const uint64_t end = base_address + size64;
    for (const Region& region : regions_) {
        const uint64_t existing_end = region.base_address + region.bytes.size();
        if (base_address < existing_end && region.base_address < end) return false;
    }
    regions_.push_back(Region{base_address, std::vector<uint8_t>(region_size, 0), permissions});
    return true;
}

bool GuestMemory::unmap(uint64_t base_address) {
    for (auto it = regions_.begin(); it != regions_.end(); ++it) {
        if (it->base_address == base_address) {
            regions_.erase(it);
            return true;
        }
    }
    return false;
}

bool GuestMemory::protect(uint64_t base_address, std::size_t region_size,
                          MemoryPermission permissions) {
    if (permissions == MemoryPermission::None) return false;
    for (Region& region : regions_) {
        if (region.base_address == base_address && region.bytes.size() == region_size) {
            region.permissions = permissions;
            return true;
        }
    }
    return false;
}

std::size_t GuestMemory::size() const noexcept {
    std::size_t total = 0;
    for (const Region& region : regions_) total += region.bytes.size();
    return total;
}

bool GuestMemory::contains(uint64_t address, std::size_t length) const noexcept {
    for (const Region& region : regions_) {
        if (region_contains(region.base_address, region.bytes.size(), address, length))
            return true;
    }
    return false;
}

bool GuestMemory::read(uint64_t address, void* destination, std::size_t length) const noexcept {
    if (destination == nullptr && length != 0) return false;
    for (const Region& region : regions_) {
        if (!region_contains(region.base_address, region.bytes.size(), address, length)) continue;
        if (!permission_allows(region.permissions, MemoryPermission::Read)) return false;
        const std::size_t offset = static_cast<std::size_t>(address - region.base_address);
        if (length != 0) std::memcpy(destination, region.bytes.data() + offset, length);
        return true;
    }
    return false;
}

bool GuestMemory::write(uint64_t address, const void* source, std::size_t length) noexcept {
    if (source == nullptr && length != 0) return false;
    for (Region& region : regions_) {
        if (!region_contains(region.base_address, region.bytes.size(), address, length)) continue;
        if (!permission_allows(region.permissions, MemoryPermission::Write)) return false;
        const std::size_t offset = static_cast<std::size_t>(address - region.base_address);
        if (length != 0) std::memcpy(region.bytes.data() + offset, source, length);
        return true;
    }
    return false;
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

bool GuestMemory::fetch32(uint64_t address, uint32_t& value) const noexcept {
    uint8_t raw[4]{};
    for (const Region& region : regions_) {
        if (!region_contains(region.base_address, region.bytes.size(), address, sizeof(raw))) continue;
        if (!permission_allows(region.permissions, MemoryPermission::Execute)) return false;
        const std::size_t offset = static_cast<std::size_t>(address - region.base_address);
        std::memcpy(raw, region.bytes.data() + offset, sizeof(raw));
        value = static_cast<uint32_t>(raw[0]) |
                (static_cast<uint32_t>(raw[1]) << 8U) |
                (static_cast<uint32_t>(raw[2]) << 16U) |
                (static_cast<uint32_t>(raw[3]) << 24U);
        return true;
    }
    return false;
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


uint64_t shift_operand(uint64_t value, unsigned shift_type,
                       unsigned amount, bool is_64_bit) {
    const unsigned width = is_64_bit ? 64U : 32U;
    const uint64_t mask = is_64_bit ? UINT64_MAX : 0xFFFFFFFFULL;
    value &= mask;
    if (amount == 0U) return value;
    switch (shift_type) {
        case 0U: return (value << amount) & mask;  // LSL
        case 1U: return value >> amount;            // LSR
        case 2U: {                                  // ASR
            const uint64_t sign = uint64_t{1} << (width - 1U);
            uint64_t result = value >> amount;
            if ((value & sign) != 0U) result |= (mask << (width - amount)) & mask;
            return result & mask;
        }
        case 3U: {                                  // ROR
            const unsigned rotation = amount % width;
            if (rotation == 0U) return value;
            return ((value >> rotation) | (value << (width - rotation))) & mask;
        }
        default: return 0U;
    }
}

void set_add_sub_flags(CpuState& cpu, uint64_t lhs, uint64_t rhs,
                       uint64_t result, bool is_64_bit, bool subtract) {
    const uint64_t sign = is_64_bit ? (uint64_t{1} << 63U) : (uint64_t{1} << 31U);
    set_nz(cpu, result, is_64_bit);
    if (subtract) {
        cpu.c = lhs >= rhs;
        cpu.v = ((lhs ^ rhs) & (lhs ^ result) & sign) != 0U;
    } else {
        cpu.c = result < lhs;
        cpu.v = ((~(lhs ^ rhs)) & (lhs ^ result) & sign) != 0U;
    }
}

}  // namespace

template <typename Memory>
StepResult execute_one_impl(CpuState& cpu, Memory& memory, uint32_t instruction) {
    const uint64_t old_pc = cpu.pc;

    if (instruction == 0xD503201FU) {
        cpu.pc += 4U;
        return StepResult::Executed;
    }

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


    // Data-processing (register): ADD/SUB with shifted register operand.
    if ((instruction & 0x1F200000U) == 0x0B000000U) {
        const bool is_64_bit = (instruction & 0x80000000U) != 0U;
        const bool subtract = (instruction & 0x40000000U) != 0U;
        const bool set_flags = (instruction & 0x20000000U) != 0U;
        const unsigned shift_type = (instruction >> 22U) & 0x3U;
        const unsigned rm = (instruction >> 16U) & 0x1FU;
        const unsigned amount = (instruction >> 10U) & 0x3FU;
        const unsigned rn = (instruction >> 5U) & 0x1FU;
        const unsigned rd = instruction & 0x1FU;
        if (shift_type == 3U || (!is_64_bit && amount >= 32U))
            return StepResult::InvalidEncoding;
        const uint64_t mask = is_64_bit ? UINT64_MAX : 0xFFFFFFFFULL;
        const uint64_t lhs = read_reg(cpu, rn, false) & mask;
        const uint64_t rhs = shift_operand(read_reg(cpu, rm, false), shift_type, amount, is_64_bit);
        const uint64_t result = (subtract ? lhs - rhs : lhs + rhs) & mask;
        write_reg(cpu, rd, result, is_64_bit, false);
        if (set_flags) set_add_sub_flags(cpu, lhs, rhs, result, is_64_bit, subtract);
        cpu.pc += 4U;
        return StepResult::Executed;
    }

    // Logical shifted-register instructions: AND, ORR, EOR and ANDS.
    if ((instruction & 0x1F000000U) == 0x0A000000U) {
        const bool is_64_bit = (instruction & 0x80000000U) != 0U;
        const unsigned opc = (instruction >> 29U) & 0x3U;
        const unsigned shift_type = (instruction >> 22U) & 0x3U;
        const bool invert = (instruction & 0x00200000U) != 0U;
        const unsigned rm = (instruction >> 16U) & 0x1FU;
        const unsigned amount = (instruction >> 10U) & 0x3FU;
        const unsigned rn = (instruction >> 5U) & 0x1FU;
        const unsigned rd = instruction & 0x1FU;
        if (!is_64_bit && amount >= 32U) return StepResult::InvalidEncoding;
        const uint64_t mask = is_64_bit ? UINT64_MAX : 0xFFFFFFFFULL;
        const uint64_t lhs = read_reg(cpu, rn, false) & mask;
        uint64_t rhs = shift_operand(read_reg(cpu, rm, false), shift_type, amount, is_64_bit);
        if (invert) rhs = (~rhs) & mask;
        uint64_t result = 0U;
        switch (opc) {
            case 0U: result = lhs & rhs; break;
            case 1U: result = lhs | rhs; break;
            case 2U: result = lhs ^ rhs; break;
            case 3U: result = lhs & rhs; break;
            default: return StepResult::InvalidEncoding;
        }
        result &= mask;
        write_reg(cpu, rd, result, is_64_bit, false);
        if (opc == 3U) {
            set_nz(cpu, result, is_64_bit);
            cpu.c = false;
            cpu.v = false;
        }
        cpu.pc += 4U;
        return StepResult::Executed;
    }

    // CSEL. CSINC/CSINV/CSNEG remain unsupported until separately implemented.
    if ((instruction & 0x7FE00C00U) == 0x1A800000U) {
        const bool is_64_bit = (instruction & 0x80000000U) != 0U;
        const unsigned rm = (instruction >> 16U) & 0x1FU;
        const unsigned condition = (instruction >> 12U) & 0xFU;
        const unsigned rn = (instruction >> 5U) & 0x1FU;
        const unsigned rd = instruction & 0x1FU;
        const unsigned selected_reg = condition_holds(cpu, condition) ? rn : rm;
        write_reg(cpu, rd, read_reg(cpu, selected_reg, false), is_64_bit, false);
        cpu.pc += 4U;
        return StepResult::Executed;
    }

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
            set_add_sub_flags(cpu, lhs, rhs, result, is_64_bit, subtract);
        }
        cpu.pc += 4U;
        return StepResult::Executed;
    }

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

    if ((instruction & 0x7C000000U) == 0x14000000U) {
        const int64_t offset = sign_extend(instruction & 0x03FFFFFFU, 26U) * 4;
        if ((instruction & 0x80000000U) != 0) cpu.x[30] = old_pc + 4U;
        cpu.pc = static_cast<uint64_t>(static_cast<int64_t>(old_pc) + offset);
        return StepResult::Executed;
    }

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

StepResult execute_one(CpuState& cpu, GuestMemory& memory, uint32_t instruction) {
    return execute_one_impl(cpu, memory, instruction);
}

StepResult execute_one(CpuState& cpu, GuestAddressSpace& memory, uint32_t instruction) {
    return execute_one_impl(cpu, memory, instruction);
}

template <typename Memory>
StepResult run_impl(CpuState& cpu, Memory& memory, uint64_t max_steps,
                    uint64_t stop_address) {
    uint64_t steps = 0;
    while (steps < max_steps) {
        if (cpu.pc == stop_address) return StepResult::StopAddressReached;
        uint32_t instruction = 0;
        if (!memory.fetch32(cpu.pc, instruction)) return StepResult::MemoryFault;
        const StepResult result = execute_one_impl(cpu, memory, instruction);
        if (result != StepResult::Executed) return result;
        ++steps;
        ++cpu.instructions_executed;
    }
    return StepResult::StepLimitReached;
}

StepResult run(CpuState& cpu, GuestMemory& memory, uint64_t max_steps,
               uint64_t stop_address) {
    return run_impl(cpu, memory, max_steps, stop_address);
}

StepResult run(CpuState& cpu, GuestAddressSpace& memory, uint64_t max_steps,
               uint64_t stop_address) {
    return run_impl(cpu, memory, max_steps, stop_address);
}

int run_self_test() {
    constexpr uint64_t kCodeBase = 0x1000U;
    constexpr uint64_t kDataBase = 0x2000U;
    GuestMemory memory;
    const MemoryPermission code_rw = MemoryPermission::Read | MemoryPermission::Write | MemoryPermission::Execute;
    const MemoryPermission data_rw = MemoryPermission::Read | MemoryPermission::Write;
    if (!memory.map(kCodeBase, GuestMemory::kPageSize, code_rw) ||
        !memory.map(kDataBase, GuestMemory::kPageSize, data_rw)) return -1;
    constexpr uint32_t program[] = {
        0xD28000E0U,
        0x91001400U,
        0xF9000020U,
        0xF9400022U,
        0x94000004U,
        0xD503201FU,
        0xD503201FU,
        0xD503201FU,
        0x91000442U,
        0xD65F03C0U,
    };
    for (std::size_t i = 0; i < sizeof(program) / sizeof(program[0]); ++i) {
        if (!memory.write32(kCodeBase + i * 4U, program[i])) return -1;
    }
    if (!memory.protect(kCodeBase, GuestMemory::kPageSize,
                        MemoryPermission::Read | MemoryPermission::Execute)) return -1;
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
    GuestMemory physical_memory;
    const MemoryPermission physical_permissions =
        MemoryPermission::Read | MemoryPermission::Write | MemoryPermission::Execute;
    if (!physical_memory.map(0x8000U, GuestMemory::kPageSize, physical_permissions))
        return -1;
    if (!physical_memory.write32(0x8000U, 0xD28000E0U)) return -1;
    GuestAddressSpace address_space(physical_memory);
    const MemoryPermission virtual_code_permissions =
        MemoryPermission::Read | MemoryPermission::Execute;
    if (!address_space.map(0x600000U, 0x8000U, GuestMemory::kPageSize,
                           virtual_code_permissions)) return -1;
    CpuState virtual_cpu{};
    virtual_cpu.pc = 0x600000U;
    if (run(virtual_cpu, address_space, 2U, 0x600004U) != StepResult::StopAddressReached ||
        virtual_cpu.x[0] != 7U) return -1;

    return 12;
}

}  // namespace astrans
