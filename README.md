# AstraNS

AstraNS is an original Android-first emulator research project. The Android shell is Kotlin; the experimental guest CPU, memory and diagnostic code is C++20.

## Current status: 0.6.0 — emulator library UI and virtual memory translation

Implemented:
- Emulator-style Android UI with Library and Settings tabs.
- Android document-picker import with persistent game-library entries, file metadata and remove-from-library action.
- Saved graphics-profile, VSync, FPS-counter and audio preferences; these preferences are not yet wired to a functioning game renderer/audio backend.
- JNI bridge and Vulkan/CPU diagnostics available inside Settings.
- Page-aligned guest physical-memory regions with overlap checks and independent Read, Write and Execute permissions.
- Software guest virtual-address space with 4 KB page mappings, virtual-to-physical translation, read/write/execute permissions, map/unmap/protect operations and multi-page access checks.
- AArch64 instruction fetch, loads/stores and the CPU run loop can execute through translated virtual addresses.
- Checked guest-memory access and little-endian 32/64-bit reads and writes.
- A bounded fetch/decode/execute loop with instruction budget and deterministic stop-address support.
- Limited AArch64 support: NOP; MOVZ/MOVN/MOVK; ADR/ADRP; ADD/SUB immediate, shifted-register and extended-register forms with NZCV flags; AND/ORR/EOR/ANDS shifted-register and bitmask-immediate forms; CSEL; unsigned-immediate integer LDR/STR with signed/unsigned byte, halfword and word loads; MADD/MSUB/MUL; GPR LDP/STP/LDPSW pair operations with offset/pre/post addressing; B, BL, B.cond, CBZ/CBNZ, TBZ/TBNZ, BR, BLR and RET.
- Native Vulkan probe that creates a Vulkan instance and reports physical GPU names, device type, API version, device-local heap size, queue capabilities and swapchain-extension availability.
- Host tests for NRO validation/loading, segment permissions, BSS zero-fill, physical memory, virtual mappings and aliases, permission enforcement, cross-page access, failed boundary writes, translated instruction fetch, shifted and immediate logical operations, CSEL, MADD/MSUB/MUL, signed and unsigned loads, pair loads/stores and writeback, PC-relative address generation, extended operands, conditional/test-bit branches, indirect calls, flags, instruction budgets and memory faults.
- GitHub Actions that run native tests before building and uploading an arm64-v8a debug APK.

## Important limitations

**AstraNS cannot launch Nintendo Switch games yet.** The NRO loader currently maps uncompressed homebrew binaries only; it does not implement MOD0 relocations/import resolution, the Switch runtime, Horizon services, commercial package handling, or GPU rendering. The library can import and catalogue user-selected files, but the title loader, Horizon services, complete CPU support and graphics rendering are not implemented. The Vulkan component is only a hardware capability probe. It does not render graphics or translate NVIDIA Tegra/Maxwell GPU commands.

Not implemented:
- Full ARMv8-A coverage, SIMD/NEON, floating-point, atomics, exceptions, privileged state, hardware-format ARM translation tables/TLB and JIT. The current software page map is not an ARM hardware MMU.
- Switch boot process, kernel and system services, IPC, filesystem interfaces or required runtime libraries.
- Tegra X1 GPU command processor, Maxwell features, shader translation or Vulkan renderer.
- Audio, controller input, title loader, save data or commercial game compatibility.

## Build APK

Every push to `main` triggers the Android APK workflow. Pull requests run native tests. You can also start the workflow manually from the repository's **Actions** tab.

After a successful run:
1. Open **Actions**.
2. Select the latest successful **Android APK** run.
3. Open **Artifacts**.
4. Download `AstraNS-debug-apk` and extract `app-debug.apk`.

The debug APK is for development/testing, not a Play Store release. It targets 64-bit ARM Android devices.

## Local build requirements

- JDK 17
- Android SDK Platform 35
- Android NDK 27.0.12077973
- CMake 3.22.1
- Gradle 8.9

Build from the repository root:

```bash
gradle assembleDebug
```

Run the native tests on Linux:

```bash
g++ -std=c++20 -Wall -Wextra -Werror -pedantic \
  -I app/src/main/cpp \
  app/src/main/cpp/arm64_interpreter.cpp \
  app/src/test/cpp/arm64_interpreter_test.cpp \
  -o astrans-native-tests
./astrans-native-tests
```

## Roadmap toward compatibility

1. Expand architectural correctness and implement ARM translation-table descriptors, TLB behaviour, exception levels and interrupt state.
2. Add SIMD/NEON, floating-point, atomics and a validated CPU execution/JIT backend.
3. Implement the Switch boot path and system-service/IPC compatibility layer from public, lawful specifications.
4. Build Tegra X1 GPU command translation, shader translation and a Vulkan renderer.
5. Add filesystem, audio, controller, timing and save-state systems.
6. Validate boot and compatibility with software the user is legally authorized to use; only then assess individual game support.

## Legal scope

AstraNS includes no Nintendo firmware, cryptographic keys, proprietary system files or commercial game data. Use only software and system data you are legally authorized to use. This project does not provide instructions for bypassing DRM or obtaining protected files.
