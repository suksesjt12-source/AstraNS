# AstraNS

AstraNS is an original Android-first emulator research project. The Android shell is Kotlin; the experimental guest CPU and memory core is C++20.

## Current status: 0.4.0 — mapped guest memory foundation

Implemented:
- Android application shell and JNI bridge.
- Page-aligned guest memory regions with overlap checks and independent Read, Write and Execute permissions.
- Permission changes for a mapped region, including changing a code page from writable during loading to read/execute for guest instruction fetch.
- Checked guest-memory access and little-endian 32/64-bit reads and writes.
- A bounded fetch/decode/execute loop with instruction budget and deterministic stop-address support.
- Limited AArch64 support: NOP; MOVZ/MOVN/MOVK; ADD/SUB immediate with basic NZCV flags; unsigned-immediate integer LDR/STR for byte, halfword, word and doubleword; B, BL, B.cond and RET.
- Host tests covering memory mapping, overlap and alignment rejection, access permissions, register operations, loads/stores, branches, instruction budgets and faults.
- GitHub Actions that run native tests before building an arm64-v8a debug APK.

## Important limitations

**AstraNS is not yet a Nintendo Switch emulator and cannot run Hollow Knight or other Switch games.** The current memory regions are a basic guest address-space abstraction, not a complete ARM MMU or Switch memory manager.

Not implemented:
- Full ARMv8-A instruction coverage, SIMD/NEON, floating-point, atomics, exceptions, privileged state, page tables or JIT.
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

1. Expand and validate AArch64 behavior, including SIMD/NEON, atomics, exceptions and exact architectural edge cases.
2. Build proper address translation, page tables, MMU permissions, exception levels and interrupt state.
3. Implement a Switch-like kernel/service and IPC compatibility layer from public, lawful specifications.
4. Build a GPU command-translation architecture and shader pipeline; validate Vulkan support on Mali-class devices.
5. Add filesystem, audio, controller, timing and save-state systems.
6. Test boot and compatibility with software the user is legally authorized to use; only then assess individual game support.

## Legal scope

AstraNS includes no Nintendo firmware, cryptographic keys, proprietary system files or commercial game data. Use only software and system data you are legally authorized to use. This project does not provide instructions for bypassing DRM or obtaining protected files.
