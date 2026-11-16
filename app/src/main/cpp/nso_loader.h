#ifndef ASTRANS_NSO_LOADER_H
#define ASTRANS_NSO_LOADER_H
#include "arm64_interpreter.h"
#include <cstddef>
#include <cstdint>
namespace astrans {
enum class NsoLoadError : uint8_t {
    None, NullInput, TooSmall, BadMagic, InvalidFlags, InvalidSegment,
    UnsupportedCompression, DecompressionFailed, HashMismatch,
    AddressOverflow, MappingFailed, CopyFailed
};
struct LoadedNso {
    uint64_t base_address = 0;
    uint64_t entry_point = 0;
    uint64_t bss_start = 0;
    uint64_t image_size = 0;
    uint32_t text_size = 0;
    uint32_t ro_size = 0;
    uint32_t data_size = 0;
    uint32_t bss_size = 0;
};
bool load_nso(GuestMemory& memory, const uint8_t* file_data, std::size_t file_size,
              uint64_t guest_base, LoadedNso& loaded, NsoLoadError& error);
}  // namespace astrans
#endif  // ASTRANS_NSO_LOADER_H
