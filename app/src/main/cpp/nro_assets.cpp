#include "nro_assets.h"

#include <algorithm>
#include <cstring>
#include <limits>

namespace astrans {
namespace {

constexpr std::size_t kNroHeaderEnd = 0x80U;
constexpr std::size_t kAssetHeaderSize = 0x38U;
constexpr uint64_t kMaximumRomFsSize = 128ULL * 1024ULL * 1024ULL;

uint32_t read_u32(const uint8_t* data) noexcept {
    return static_cast<uint32_t>(data[0]) |
           (static_cast<uint32_t>(data[1]) << 8U) |
           (static_cast<uint32_t>(data[2]) << 16U) |
           (static_cast<uint32_t>(data[3]) << 24U);
}

uint64_t read_u64(const uint8_t* data) noexcept {
    return static_cast<uint64_t>(read_u32(data)) |
           (static_cast<uint64_t>(read_u32(data + 4U)) << 32U);
}

}

bool extract_nro_romfs(const uint8_t* file_data, std::size_t file_size,
                       std::vector<uint8_t>& romfs, NroAssetInfo& info,
                       NroAssetError& error) {
    romfs.clear();
    info = {};
    error = NroAssetError::None;

    if (file_data == nullptr) {
        error = NroAssetError::NullInput;
        return false;
    }
    if (file_size < kNroHeaderEnd) {
        error = NroAssetError::TruncatedNro;
        return false;
    }
    if (std::memcmp(file_data + 0x10U, "NRO0", 4U) != 0) {
        error = NroAssetError::BadNroMagic;
        return false;
    }

    const uint64_t nro_size = read_u32(file_data + 0x18U);
    if (nro_size < kNroHeaderEnd || nro_size > file_size) {
        error = NroAssetError::InvalidNroSize;
        return false;
    }
    if (nro_size == file_size || file_size - static_cast<std::size_t>(nro_size) < 4U) {
        error = NroAssetError::NoAssetHeader;
        return false;
    }

    const uint8_t* asset = file_data + static_cast<std::size_t>(nro_size);
    const std::size_t available = file_size - static_cast<std::size_t>(nro_size);
    if (std::memcmp(asset, "ASET", 4U) != 0) {
        error = NroAssetError::NoAssetHeader;
        return false;
    }
    if (available < kAssetHeaderSize) {
        error = NroAssetError::TruncatedAssetHeader;
        return false;
    }
    if (read_u32(asset + 4U) != 0U) {
        error = NroAssetError::UnsupportedAssetVersion;
        return false;
    }

    const uint64_t relative_offset = read_u64(asset + 0x28U);
    const uint64_t section_size = read_u64(asset + 0x30U);
    if (section_size == 0U) {
        error = NroAssetError::MissingRomFs;
        return false;
    }
    if (relative_offset < kAssetHeaderSize ||
        relative_offset > available ||
        section_size > static_cast<uint64_t>(available) - relative_offset) {
        error = NroAssetError::InvalidRomFsRange;
        return false;
    }
    if (section_size > kMaximumRomFsSize ||
        section_size > static_cast<uint64_t>(std::numeric_limits<std::size_t>::max())) {
        error = NroAssetError::RomFsTooLarge;
        return false;
    }

    const std::size_t section_start =
        static_cast<std::size_t>(nro_size + relative_offset);
    const std::size_t section_length = static_cast<std::size_t>(section_size);
    romfs.assign(file_data + section_start, file_data + section_start + section_length);
    info.asset_header_offset = nro_size;
    info.romfs_offset = relative_offset;
    info.romfs_size = section_size;
    return true;
}

}
