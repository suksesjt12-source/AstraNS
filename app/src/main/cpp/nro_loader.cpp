#include "nro_loader.h"

#include <algorithm>
#include <array>
#include <limits>

namespace astrans {
namespace {

constexpr std::size_t kNroHeaderOffset = 0x10U;
constexpr std::size_t kNroHeaderEnd = 0x80U;
constexpr uint32_t kCompressedFlag = 1U << 1U;

uint32_t read_u32_le(const uint8_t* data) noexcept {
    return static_cast<uint32_t>(data[0]) |
           (static_cast<uint32_t>(data[1]) << 8U) |
           (static_cast<uint32_t>(data[2]) << 16U) |
           (static_cast<uint32_t>(data[3]) << 24U);
}

bool checked_add(uint64_t left, uint64_t right, uint64_t& result) noexcept {
    if (left > std::numeric_limits<uint64_t>::max() - right) return false;
    result = left + right;
    return true;
}

bool align_up_page(uint64_t value, uint64_t& aligned) noexcept {
    constexpr uint64_t mask = GuestMemory::kPageSize - 1U;
    if (value > std::numeric_limits<uint64_t>::max() - mask) return false;
    aligned = (value + mask) & ~mask;
    return true;
}

struct Segment {
    uint32_t offset;
    uint32_t size;
    uint64_t mapped_size;
    MemoryPermission final_permissions;
};

}  // namespace

bool load_nro(GuestMemory& memory, const uint8_t* file_data, std::size_t file_size,
              uint64_t guest_base, LoadedNro& loaded, NroLoadError& error) {
    loaded = {};
    error = NroLoadError::None;

    if (file_data == nullptr) {
        error = NroLoadError::NullInput;
        return false;
    }
    if (file_size < kNroHeaderEnd) {
        error = NroLoadError::TooSmall;
        return false;
    }
    if (file_data[kNroHeaderOffset] != 'N' || file_data[kNroHeaderOffset + 1U] != 'R' ||
        file_data[kNroHeaderOffset + 2U] != 'O' || file_data[kNroHeaderOffset + 3U] != '0') {
        error = NroLoadError::BadMagic;
        return false;
    }
    if ((guest_base % GuestMemory::kPageSize) != 0U) {
        error = NroLoadError::InvalidSegment;
        return false;
    }

    const uint32_t declared_size = read_u32_le(file_data + 0x18U);
    const uint32_t flags = read_u32_le(file_data + 0x1CU);
    if (declared_size < kNroHeaderEnd || declared_size > file_size) {
        error = NroLoadError::InvalidFileSize;
        return false;
    }
    if ((flags & kCompressedFlag) != 0U) {
        error = NroLoadError::CompressedNroUnsupported;
        return false;
    }

    const uint32_t text_offset = read_u32_le(file_data + 0x20U);
    const uint32_t text_size = read_u32_le(file_data + 0x24U);
    const uint32_t ro_offset = read_u32_le(file_data + 0x28U);
    const uint32_t ro_size = read_u32_le(file_data + 0x2CU);
    const uint32_t data_offset = read_u32_le(file_data + 0x30U);
    const uint32_t data_size = read_u32_le(file_data + 0x34U);
    const uint32_t bss_size = read_u32_le(file_data + 0x38U);

    if (text_offset != 0U || text_size < kNroHeaderEnd) {
        error = NroLoadError::InvalidSegment;
        return false;
    }

    const auto file_range_valid = [declared_size](uint32_t offset, uint32_t size) {
        return offset <= declared_size && size <= declared_size - offset;
    };
    if (!file_range_valid(text_offset, text_size) ||
        (ro_size != 0U && (!file_range_valid(ro_offset, ro_size) ||
                           (ro_offset % GuestMemory::kPageSize) != 0U)) ||
        (data_size != 0U && (!file_range_valid(data_offset, data_size) ||
                             (data_offset % GuestMemory::kPageSize) != 0U)) ||
        ((data_size != 0U || bss_size != 0U) &&
         (data_offset % GuestMemory::kPageSize) != 0U)) {
        error = NroLoadError::InvalidSegment;
        return false;
    }

    uint64_t data_and_bss_size = 0;
    if (!checked_add(data_size, bss_size, data_and_bss_size)) {
        error = NroLoadError::InvalidSegment;
        return false;
    }

    std::array<Segment, 3> segments{{
        {text_offset, text_size, 0U, MemoryPermission::Read | MemoryPermission::Execute},
        {ro_offset, ro_size, 0U, MemoryPermission::Read},
        {data_offset, static_cast<uint32_t>(data_and_bss_size), 0U,
         MemoryPermission::Read | MemoryPermission::Write}
    }};
    if (data_and_bss_size > UINT32_MAX) {
        error = NroLoadError::InvalidSegment;
        return false;
    }
    if (segments[2].size == 0U) segments[2].offset = 0U;

    uint64_t image_end = 0U;
    for (Segment& segment : segments) {
        if (segment.size == 0U) continue;
        if (!align_up_page(segment.size, segment.mapped_size)) {
            error = NroLoadError::AddressOverflow;
            return false;
        }
        uint64_t end = 0U;
        uint64_t guest_address = 0U;
        if (!checked_add(segment.offset, segment.mapped_size, end) ||
            !checked_add(guest_base, segment.offset, guest_address) ||
            guest_address > std::numeric_limits<uint64_t>::max() - segment.mapped_size) {
            error = NroLoadError::AddressOverflow;
            return false;
        }
        image_end = std::max(image_end, static_cast<uint64_t>(segment.offset) + segment.size);
    }

    // Validate page-rounded memory ranges before creating any mapping.
    for (std::size_t i = 0; i < segments.size(); ++i) {
        if (segments[i].size == 0U) continue;
        const uint64_t begin_i = segments[i].offset;
        const uint64_t end_i = begin_i + segments[i].mapped_size;
        for (std::size_t j = i + 1U; j < segments.size(); ++j) {
            if (segments[j].size == 0U) continue;
            const uint64_t begin_j = segments[j].offset;
            const uint64_t end_j = begin_j + segments[j].mapped_size;
            if (begin_i < end_j && begin_j < end_i) {
                error = NroLoadError::InvalidSegment;
                return false;
            }
        }
    }

    const MemoryPermission temporary_permissions =
        MemoryPermission::Read | MemoryPermission::Write | MemoryPermission::Execute;
    std::array<uint64_t, 3> mapped_bases{};
    std::size_t mapped_count = 0U;
    auto rollback = [&]() {
        while (mapped_count != 0U) {
            --mapped_count;
            memory.unmap(mapped_bases[mapped_count]);
        }
    };

    for (const Segment& segment : segments) {
        if (segment.size == 0U) continue;
        const uint64_t address = guest_base + segment.offset;
        if (!memory.map(address, static_cast<std::size_t>(segment.mapped_size),
                        temporary_permissions)) {
            rollback();
            error = NroLoadError::MappingFailed;
            return false;
        }
        mapped_bases[mapped_count++] = address;
    }

    for (const Segment& segment : segments) {
        if (segment.size == 0U) continue;
        if (!memory.write(guest_base + segment.offset,
                          file_data + segment.offset, segment.size)) {
            rollback();
            error = NroLoadError::CopyFailed;
            return false;
        }
    }

    mapped_count = 0U;
    for (const Segment& segment : segments) {
        if (segment.size == 0U) continue;
        const uint64_t address = guest_base + segment.offset;
        if (!memory.protect(address, static_cast<std::size_t>(segment.mapped_size),
                            segment.final_permissions)) {
            rollback();
            error = NroLoadError::MappingFailed;
            return false;
        }
    }

    loaded.base_address = guest_base;
    loaded.entry_point = guest_base;
    loaded.bss_start = guest_base + data_offset + data_size;
    loaded.image_size = image_end;
    loaded.text_size = text_size;
    loaded.ro_size = ro_size;
    loaded.data_size = data_size;
    loaded.bss_size = bss_size;
    return true;
}

}  // namespace astrans
