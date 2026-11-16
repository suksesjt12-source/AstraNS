#ifndef ASTRANS_NRO_LOADER_H
#define ASTRANS_NRO_LOADER_H

#include "arm64_interpreter.h"

#include <cstddef>
#include <cstdint>

namespace astrans {

enum class NroLoadError : uint8_t {
    None,
    NullInput,
    TooSmall,
    BadMagic,
    InvalidFileSize,
    CompressedNroUnsupported,
    InvalidSegment,
    AddressOverflow,
    RelocationFailed,
    MappingFailed,
    CopyFailed,
};

struct LoadedNro {
    uint64_t base_address = 0;
    uint64_t entry_point = 0;
    uint64_t bss_start = 0;
    uint64_t image_size = 0;
    uint32_t text_size = 0;
    uint32_t ro_size = 0;
    uint32_t data_size = 0;
    uint32_t bss_size = 0;
};

// Loads an uncompressed NRO image into page-aligned guest memory.
// This maps segments and returns the NRO entry address; it does not provide
// the Horizon runtime, imports, services, or a game-launch environment.
bool load_nro(GuestMemory& memory, const uint8_t* file_data, std::size_t file_size,
              uint64_t guest_base, LoadedNro& loaded, NroLoadError& error);

}  // namespace astrans

#endif  // ASTRANS_NRO_LOADER_H
