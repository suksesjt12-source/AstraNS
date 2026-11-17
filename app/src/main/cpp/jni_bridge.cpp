#include <jni.h>

#include <cstdint>
#include <cstring>
#include <exception>
#include <mutex>
#include <memory>
#include <sstream>
#include <string>
#include <vector>

#include "arm64_interpreter.h"
#include "nro_loader.h"
#include "nso_loader.h"
#include "nro_assets.h"
#include "romfs.h"
#include "fsp_srv.h"
#include "hipc_cmif.h"
#include "vulkan_probe.h"

namespace {
constexpr jsize kMaximumHomebrewFileSize = 128 * 1024 * 1024;
constexpr uint32_t kMaximumBssSize = 64U * 1024U * 1024U;
constexpr uint64_t kInitialGuestBase = 0x10000000ULL;

astrans::GuestMemory g_guest_memory;
std::mutex g_guest_memory_mutex;
uint64_t g_next_guest_base = kInitialGuestBase;
std::shared_ptr<astrans::RomFsImage> g_romfs_image;
astrans::FspSrv g_fsp_srv;
astrans::CmifServiceManager g_cmif_services;
bool g_fsp_srv_registered = false;

const char* nro_error_name(astrans::NroLoadError error) noexcept {
    using astrans::NroLoadError;
    switch (error) {
        case NroLoadError::None: return "None";
        case NroLoadError::NullInput: return "NullInput";
        case NroLoadError::TooSmall: return "TooSmall";
        case NroLoadError::BadMagic: return "BadMagic";
        case NroLoadError::InvalidFileSize: return "InvalidFileSize";
        case NroLoadError::CompressedNroUnsupported: return "CompressedNroUnsupported";
        case NroLoadError::InvalidSegment: return "InvalidSegment";
        case NroLoadError::AddressOverflow: return "AddressOverflow";
        case NroLoadError::RelocationFailed: return "RelocationFailed";
        case NroLoadError::MappingFailed: return "MappingFailed";
        case NroLoadError::CopyFailed: return "CopyFailed";
    }
    return "Unknown";
}

const char* nso_error_name(astrans::NsoLoadError error) noexcept {
    using astrans::NsoLoadError;
    switch (error) {
        case NsoLoadError::None: return "None";
        case NsoLoadError::NullInput: return "NullInput";
        case NsoLoadError::TooSmall: return "TooSmall";
        case NsoLoadError::BadMagic: return "BadMagic";
        case NsoLoadError::InvalidFlags: return "InvalidFlags";
        case NsoLoadError::InvalidSegment: return "InvalidSegment";
        case NsoLoadError::UnsupportedCompression: return "UnsupportedCompression";
        case NsoLoadError::DecompressionFailed: return "DecompressionFailed";
        case NsoLoadError::HashMismatch: return "HashMismatch";
        case NsoLoadError::RelocationFailed: return "RelocationFailed";
        case NsoLoadError::AddressOverflow: return "AddressOverflow";
        case NsoLoadError::MappingFailed: return "MappingFailed";
        case NsoLoadError::CopyFailed: return "CopyFailed";
    }
    return "Unknown";
}

bool advance_guest_base(uint64_t image_size) noexcept {
    constexpr uint64_t mask = astrans::GuestMemory::kPageSize - 1U;
    constexpr uint64_t guard_size = astrans::GuestMemory::kPageSize * 4U;
    if (image_size == 0U || image_size > UINT64_MAX - mask) return false;
    const uint64_t span = (image_size + mask) & ~mask;
    if (g_next_guest_base > UINT64_MAX - span ||
        g_next_guest_base + span > UINT64_MAX - guard_size) return false;
    g_next_guest_base += span + guard_size;
    return true;
}

const char* asset_error_name(astrans::NroAssetError error) noexcept {
    using astrans::NroAssetError;
    switch (error) {
        case NroAssetError::None: return "None";
        case NroAssetError::NullInput: return "NullInput";
        case NroAssetError::TruncatedNro: return "TruncatedNro";
        case NroAssetError::BadNroMagic: return "BadNroMagic";
        case NroAssetError::InvalidNroSize: return "InvalidNroSize";
        case NroAssetError::NoAssetHeader: return "NoAssetHeader";
        case NroAssetError::TruncatedAssetHeader: return "TruncatedAssetHeader";
        case NroAssetError::BadAssetMagic: return "BadAssetMagic";
        case NroAssetError::UnsupportedAssetVersion: return "UnsupportedAssetVersion";
        case NroAssetError::MissingRomFs: return "MissingRomFs";
        case NroAssetError::InvalidRomFsRange: return "InvalidRomFsRange";
        case NroAssetError::RomFsTooLarge: return "RomFsTooLarge";
    }
    return "Unknown";
}

const char* romfs_error_name(astrans::RomFsError error) noexcept {
    using astrans::RomFsError;
    switch (error) {
        case RomFsError::None: return "None";
        case RomFsError::NullInput: return "NullInput";
        case RomFsError::TooSmall: return "TooSmall";
        case RomFsError::UnsupportedIvfc: return "UnsupportedIvfc";
        case RomFsError::InvalidHeader: return "InvalidHeader";
        case RomFsError::InvalidTableRange: return "InvalidTableRange";
        case RomFsError::InvalidEntry: return "InvalidEntry";
        case RomFsError::InvalidPath: return "InvalidPath";
        case RomFsError::NotFound: return "NotFound";
        case RomFsError::NotDirectory: return "NotDirectory";
        case RomFsError::IsDirectory: return "IsDirectory";
        case RomFsError::ReadLimitExceeded: return "ReadLimitExceeded";
        case RomFsError::InvalidOffset: return "InvalidOffset";
        case RomFsError::TooManyEntries: return "TooManyEntries";
    }
    return "Unknown";
}

std::string mount_romfs_from_nro(const uint8_t* bytes, std::size_t size) {
    g_romfs_image.reset();
    g_fsp_srv.set_image(nullptr);

    std::vector<uint8_t> romfs_bytes;
    astrans::NroAssetInfo asset_info{};
    astrans::NroAssetError asset_error = astrans::NroAssetError::None;
    if (!astrans::extract_nro_romfs(bytes, size, romfs_bytes,
                                    asset_info, asset_error)) {
        if (asset_error == astrans::NroAssetError::NoAssetHeader ||
            asset_error == astrans::NroAssetError::MissingRomFs) {
            return "RomFS asset: this NRO does not contain a RomFS partition.";
        }
        return std::string("NRO RomFS extraction failed: ") +
               asset_error_name(asset_error) + ".";
    }

    auto image = std::make_shared<astrans::RomFsImage>();
    astrans::RomFsError romfs_error = astrans::RomFsError::None;
    if (!image->mount(romfs_bytes.data(), romfs_bytes.size(), romfs_error)) {
        return std::string("NRO RomFS mount failed: ") +
               romfs_error_name(romfs_error) + ".";
    }

    g_romfs_image = std::move(image);
    g_fsp_srv.set_image(g_romfs_image);
    if (!g_fsp_srv_registered) {
        if (!g_fsp_srv.register_with(g_cmif_services)) {
            g_romfs_image.reset();
            g_fsp_srv.set_image(nullptr);
            return "RomFS mounted, but fsp-srv registration failed.";
        }
        g_fsp_srv_registered = true;
    }

    std::ostringstream status;
    status << "RomFS mounted read-only: " << asset_info.romfs_size
           << " bytes. fsp-srv registered in the experimental CMIF dispatcher.";
    return status.str();
}
}  // namespace

extern "C" JNIEXPORT jint JNICALL
Java_org_astrans_emulator_MainActivity_nativeRunSelfTest(JNIEnv*, jobject) {
    return static_cast<jint>(astrans::run_self_test());
}

extern "C" JNIEXPORT jstring JNICALL
Java_org_astrans_emulator_MainActivity_nativeVulkanInfo(JNIEnv* env, jobject) {
    const std::string info = astrans::vulkan_probe();
    return env->NewStringUTF(info.c_str());
}

extern "C" JNIEXPORT jstring JNICALL
Java_org_astrans_emulator_MainActivity_nativeLoadHomebrew(JNIEnv* env, jobject,
                                                           jbyteArray input) {
    if (input == nullptr) return env->NewStringUTF("Load failed: no file data was supplied.");
    const jsize length = env->GetArrayLength(input);
    if (length < 0x80) return env->NewStringUTF("Unsupported file: header is too small.");
    if (length > kMaximumHomebrewFileSize)
        return env->NewStringUTF("File exceeds the 128 MiB homebrew loader limit.");

    try {
        std::vector<uint8_t> bytes(static_cast<std::size_t>(length));
        env->GetByteArrayRegion(input, 0, length, reinterpret_cast<jbyte*>(bytes.data()));
        if (env->ExceptionCheck()) {
            env->ExceptionClear();
            return env->NewStringUTF("Android could not copy the selected file into the native loader.");
        }

        const bool is_nro = bytes.size() >= 0x14U &&
            std::memcmp(bytes.data() + 0x10U, "NRO0", 4U) == 0;
        const bool is_nso = std::memcmp(bytes.data(), "NSO0", 4U) == 0;
        if (!is_nro && !is_nso) {
            return env->NewStringUTF(
                "Unsupported format. This loader accepts uncompressed NRO0 and NSO0 homebrew images only; NSP/XCI packages are not supported.");
        }

        std::lock_guard<std::mutex> lock(g_guest_memory_mutex);
        const uint64_t guest_base = g_next_guest_base;
        std::ostringstream result;

        if (is_nro) {
            const uint32_t bss_size =
                static_cast<uint32_t>(bytes[0x38U]) |
                (static_cast<uint32_t>(bytes[0x39U]) << 8U) |
                (static_cast<uint32_t>(bytes[0x3AU]) << 16U) |
                (static_cast<uint32_t>(bytes[0x3BU]) << 24U);
            if (bss_size > kMaximumBssSize)
                return env->NewStringUTF("NRO rejected: BSS exceeds the 64 MiB per-image safety limit.");

            astrans::LoadedNro loaded{};
            astrans::NroLoadError error = astrans::NroLoadError::None;
            if (!astrans::load_nro(g_guest_memory, bytes.data(), bytes.size(),
                                   guest_base, loaded, error)) {
                result << "NRO load failed: " << nro_error_name(error)
                       << " (code " << static_cast<unsigned>(error) << ").";
                return env->NewStringUTF(result.str().c_str());
            }
            if (!advance_guest_base(loaded.image_size))
                return env->NewStringUTF("NRO loaded, but the guest address allocator is exhausted.");

            result << "NRO image loaded into guest memory.\n"
                   << "Guest base: 0x" << std::hex << loaded.base_address << "\n"
                   << "Entry point: 0x" << loaded.entry_point << "\n"
                   << "Image span: 0x" << loaded.image_size << "\n"
                   << std::dec << "Text: " << loaded.text_size
                   << " bytes; RO: " << loaded.ro_size
                   << " bytes; Data: " << loaded.data_size
                   << " bytes; BSS: " << loaded.bss_size << " bytes.\n"
                   << "Status: image mapped, not booted. This build has no Horizon runtime or game renderer.\n"
                   << mount_romfs_from_nro(bytes.data(), bytes.size());
        } else {
            const uint32_t bss_size =
                static_cast<uint32_t>(bytes[0x3CU]) |
                (static_cast<uint32_t>(bytes[0x3DU]) << 8U) |
                (static_cast<uint32_t>(bytes[0x3EU]) << 16U) |
                (static_cast<uint32_t>(bytes[0x3FU]) << 24U);
            if (bss_size > kMaximumBssSize)
                return env->NewStringUTF("NSO rejected: BSS exceeds the 64 MiB per-image safety limit.");

            astrans::LoadedNso loaded{};
            astrans::NsoLoadError error = astrans::NsoLoadError::None;
            if (!astrans::load_nso(g_guest_memory, bytes.data(), bytes.size(),
                                   guest_base, loaded, error)) {
                result << "NSO load failed: " << nso_error_name(error)
                       << " (code " << static_cast<unsigned>(error) << ").";
                return env->NewStringUTF(result.str().c_str());
            }
            if (!advance_guest_base(loaded.image_size))
                return env->NewStringUTF("NSO loaded, but the guest address allocator is exhausted.");

            g_romfs_image.reset();
            g_fsp_srv.set_image(nullptr);

            result << "NSO image loaded into guest memory.\n"
                   << "Guest base: 0x" << std::hex << loaded.base_address << "\n"
                   << "Entry point: 0x" << loaded.entry_point << "\n"
                   << "Image span: 0x" << loaded.image_size << "\n"
                   << std::dec << "Text: " << loaded.text_size
                   << " bytes; RO: " << loaded.ro_size
                   << " bytes; Data: " << loaded.data_size
                   << " bytes; BSS: " << loaded.bss_size << " bytes.\n"
                   << "Status: loaded, not booted. This build has no Horizon runtime or game renderer.";
        }
        return env->NewStringUTF(result.str().c_str());
    } catch (const std::bad_alloc&) {
        return env->NewStringUTF("Load failed: not enough memory to load this homebrew image.");
    } catch (const std::exception& error) {
        const std::string message = std::string("Native loader error: ") + error.what();
        return env->NewStringUTF(message.c_str());
    } catch (...) {
        return env->NewStringUTF("Native loader failed with an unknown error.");
    }
}
