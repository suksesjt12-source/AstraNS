#ifndef ASTRANS_NRO_ASSETS_H
#define ASTRANS_NRO_ASSETS_H

#include <cstddef>
#include <cstdint>
#include <vector>

namespace astrans {

enum class NroAssetError : uint8_t {
    None,
    NullInput,
    TruncatedNro,
    BadNroMagic,
    InvalidNroSize,
    NoAssetHeader,
    TruncatedAssetHeader,
    BadAssetMagic,
    UnsupportedAssetVersion,
    MissingRomFs,
    InvalidRomFsRange,
    RomFsTooLarge
};

struct NroAssetInfo {
    uint64_t asset_header_offset = 0;
    uint64_t romfs_offset = 0;
    uint64_t romfs_size = 0;
};

bool extract_nro_romfs(const uint8_t* file_data, std::size_t file_size,
                       std::vector<uint8_t>& romfs, NroAssetInfo& info,
                       NroAssetError& error);

}

#endif
