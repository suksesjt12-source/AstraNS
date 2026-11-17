# AstraNS

AstraNS is an original Android-first Nintendo Switch emulation research project. Its Android interface is Kotlin; its experimental CPU, memory, executable loaders and IPC foundations are C++20.

## Current status: 1.8.0 — NSP directory inspection and homebrew image loading

Implemented:
- Android Library and Settings interface with persistent document-picker entries.
- Read-only NSP PFS0 directory inspection with bounded entry/string-table sizes, filename validation, file-boundary checks and NCA/TIK/CERT inventory. Only the directory is read; encrypted payloads are not opened or decrypted.
- Kotlin unit tests for valid PFS0 directories, invalid signatures, truncated headers and out-of-range entries.
- Detail action sends selected NRO0/NSO0 image bytes through JNI into native guest memory.
- Bounded input size (128 MiB) and BSS size (64 MiB) for the Android homebrew loading path.
- NRO/NSO segment mapping, MOD0 relative relocation stage, final memory permissions and detailed load/error report.
- 4 KiB guest physical pages and a software virtual-address map with read/write/execute checks.
- Bounded ARM64 interpreter with a limited instruction subset: NOP, MOVZ/MOVN/MOVK, ADR/ADRP, selected ADD/SUB forms, logical forms, CSEL, MADD/MSUB/MUL, integer loads, LDP/STP and branches.
- NRO0 and NSO0 segment validation; NSO LZ4 decompression and SHA-256 validation; BSS initialization.
- Limited MOD0 R_AARCH64_RELATIVE RELA/RELR relocation handling.
- HIPC/CMIF parsers for basic messages/domains, basic service dispatch and handle table.
- TIPC request parser and a basic sm service registry with session limits.
- Vulkan capability probe. GitHub Actions native tests and arm64-v8a APK build.

## Important limitations

**AstraNS does not boot Nintendo Switch OS or launch Hollow Knight.** A successful NRO/NSO load means the executable image was mapped to guest memory; it does not mean the guest operating system or game has booted. There is no complete Horizon kernel/runtime, scheduler, SVC/syscall implementation, full CPU execution engine or Tegra X1 GPU renderer.

Not implemented:
- Full ARMv8-A, SIMD/NEON, floating point, atomics, exceptions, sysregs, hardware ARM MMU translation tables/TLB, JIT or multicore scheduling.
- Complete Switch boot chain, NPDM/NACP parsing, title metadata extraction from encrypted NCA content, title installation, import resolution and the complete dynamic relocation set.
- Full HIPC descriptor memory transfers, kernel object semantics, SVC handle lifecycle, the complete TIPC service manager and actual Horizon services.
- Tegra X1 Maxwell GPU command processor, shader translation and Vulkan rendering.
- Audio, controller input, filesystem save-data integration, timing and real-game compatibility testing.

Graphics/VSync/FPS/audio settings are preferences only until the corresponding runtime backends exist. The Vulkan module is a hardware probe, not a renderer. NSP support is directory-inspection-only: NCA content decryption, title metadata extraction, XCI parsing and commercial title launch are not implemented.

## Build and test

Every push to main triggers the Android APK workflow. The pipeline runs native C++ tests and Kotlin PFS0-inspector unit tests before building the arm64-v8a debug APK. Download the AstraNS-debug-apk artifact from the Artifacts section of the latest successful Actions run.

Local requirements: JDK 17, Android SDK Platform 35, NDK 27.0.12077973, CMake 3.22.1 and Gradle 8.9. The GitHub Actions workflow runs both native and Kotlin unit tests automatically.

## Legal scope

AstraNS contains no Nintendo firmware, cryptographic keys, proprietary system files or commercial game data. Use only software and system data you are legally authorized to use. The project does not provide instructions for obtaining protected files or bypassing DRM.
