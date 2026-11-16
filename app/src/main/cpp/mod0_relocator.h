#ifndef ASTRANS_MOD0_RELOCATOR_H
#define ASTRANS_MOD0_RELOCATOR_H
#include "arm64_interpreter.h"
#include <cstdint>
namespace astrans {
enum class Mod0Error : uint8_t {
    None, NoModuleHeader, InvalidHeader, AddressOverflow, InvalidDynamicSection,
    InvalidRelocationTable, UnsupportedRelocation, MemoryFault
};
// Applies the limited relative relocation subset used by the homebrew MOD0 runtime.
// Returns true with NoModuleHeader when the image has no MOD0 marker.
bool apply_mod0_relative_relocations(GuestMemory& memory, uint64_t image_base,
                                     uint64_t image_size, uint64_t text_size,
                                     Mod0Error& error);
}  // namespace astrans
#endif  // ASTRANS_MOD0_RELOCATOR_H
