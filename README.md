# AstraNS

AstraNS is an original Android-first Nintendo Switch emulation research project. Its Android interface is Kotlin; its experimental CPU, memory, executable loaders, IPC foundations and read-only filesystem layer are C++20.

## Current status: 1.8.0 — NRO RomFS and experimental fsp-srv

Implemented:
- Android Library and Settings interface with persistent document-picker entries.
- Detail action sends selected NRO0/NSO0 image bytes through JNI into native guest memory.
- Bounded input size (128 MiB) and BSS size (64 MiB) for the Android homebrew loading path.
- NRO/NSO segment mapping, MOD0 relative relocation stage, final memory permissions and detailed load/error report.
- NRO ASET parsing using the NRO-declared image size and ASET-relative asset offsets.
- Extraction and validation of a raw Switch RomFS asset embedded in an NRO ASET section.
- Read-only RomFS directory and file metadata traversal, path validation, bounded file reads and directory listing.
- Experimental `fsp-srv` CMIF service registration with GetStatus, OpenFile, ReadFile, CloseFile and ListDirectory commands.
- 4 KiB guest physical pages and a software virtual-address map with read/write/execute checks.
- Bounded ARM64 interpreter with a limited instruction subset: NOP, MOVZ/MOVN/MOVK, ADR/ADRP, selected ADD/SUB forms, logical forms, CSEL, MADD/MSUB/MUL, integer loads, LDP/STP and branches.
- NRO0 and NSO0 segment validation; NSO LZ4 decompression and SHA-256 validation; BSS initialization.
- Limited MOD0 R_AARCH64_RELATIVE RELA/RELR relocation handling.
- HIPC/CMIF parsers for basic messages/domains, basic service dispatch and handle table.
- TIPC request parser and a basic sm service registry with session limits.
- Vulkan capability probe. GitHub Actions native tests and arm64-v8a APK build.

## Stage 4 filesystem scope

The NRO path accepts a raw Switch RomFS image in the NRO ASET section. The current parser validates the 0x50-byte RomFS header and table ranges, follows directory/file metadata links, blocks parent-directory traversal, and caps an individual read at 1 MiB.

The current `fsp-srv` commands are an AstraNS experimental CMIF subset. They are registered in the native CMIF service manager, but the guest HIPC transport does not yet route a running Horizon process into this handler. File handles are emulator-side session identifiers, not complete Horizon kernel objects or official `fsp-srv` ABI handles.

IVFC-wrapped partitions, RomFS integrity/hash verification, writable filesystems, save data, SD-card/BIS mounts, full descriptor buffer transfers and full Horizon service semantics are not implemented.

## Important limitations

**AstraNS does not boot Nintendo Switch OS or launch commercial games.** A successful NRO/NSO load means the executable image was mapped to guest memory. A successful RomFS mount means its read-only filesystem tables were parsed; it does not mean the guest operating system or game has booted. There is no complete Horizon kernel/runtime, scheduler, SVC/syscall implementation, full CPU execution engine or Tegra X1 GPU renderer.

Not implemented:
- Full ARMv8-A, SIMD/NEON, floating point, atomics, exceptions, sysregs, hardware ARM MMU translation tables/TLB, JIT or multicore scheduling.
- Complete Switch boot chain, NPDM/NACP execution, full title/package handling, import resolution and the complete dynamic relocation set.
- Full HIPC descriptor memory transfers, kernel object semantics, SVC handle lifecycle, complete TIPC service-manager routing and actual guest-facing Horizon services.
- Tegra X1 Maxwell GPU command processor, shader translation and Vulkan rendering.
- Audio, controller input, save-data integration, timing and real-game compatibility testing.

Graphics/VSync/FPS/audio settings are preferences only until the corresponding runtime backends exist. The Vulkan module is a hardware probe, not a renderer. Commercial NSP/XCI packages are not supported by this homebrew loader.

## Build and test

Every push to main triggers the Android APK workflow. The pipeline runs native C++ tests before building the arm64-v8a debug APK. Download the AstraNS-debug-apk artifact from the Artifacts section of the latest successful Actions run.

Local requirements: JDK 17, Android SDK Platform 35, NDK 27.0.12077973, CMake 3.22.1 and Gradle 8.9. GitHub Actions runs both the interpreter/memory tests and the Stage 4 RomFS/fsp-srv tests.

## Legal scope

AstraNS contains no Nintendo firmware, cryptographic keys, proprietary system files or commercial game data. Use only software and system data you are legally authorized to use. The project does not provide instructions for obtaining protected files or bypassing DRM.
