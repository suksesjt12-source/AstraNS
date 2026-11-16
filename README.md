# AstraNS

AstraNS is an original Android-first emulator research project. The Android shell is Kotlin; the experimental guest CPU, memory and diagnostic code is C++20.

## Current status: 0.5.0 — guest execution and Vulkan capability probe

Implemented:
- Android application shell and JNI bridge.
- Page-aligned guest memory regions with overlap checks and independent Read, Write and Execute permissions.
- Checked guest-memory access and little-endian 32/64-bit reads and writes.
- A bounded fetch/decode/execute loop with instruction budget and deterministic stop-address support.
- Limited AArch64 support: NOP; MOVZ/MOVN/MOVK; ADD/SUB immediate with basic NZCV flags; unsigned-immediate integer LDR/STR; B, BL, B.cond and RET.
- Native Vulkan probe that creates a Vulkan instance and reports physical GPU names, device type, API version, device-local heap size, queue capabilities and swapchain-extension availability.
- Host tests for memory mapping, access permissions, register operations, loads/stores, branches, instruction budgets and faults.
- GitHub Actions that run native tests before building and uploading an arm64-v8a debug APK.

## Important limitations

**AstraNS is not yet a Nintendo Switch emulator and cannot run Hollow Knight or other Switch games.** The Vulkan component is only a hardware capability probe. It does not render graphics or translate NVIDIA Tegra/Maxwell GPU commands.

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

1. Complete more of ARMv8-A, including SIMD/NEON, atomics, exceptions and exact architectural edge cases.
2. Build proper address translation, page tables, MMU permissions, exception levels and interrupt state.
3. Implement a Switch-like kernel/service and IPC compatibility layer from public, lawful specifications.
4. Use the Vulkan probe results to build a GPU command-translation and shader pipeline.
5. Add filesystem, audio, controller, timing and save-state systems.
6. Test boot and compatibility with software the user is legally authorized to use; only then assess individual game support.

## Legal scope

AstraNS includes no Nintendo firmware, cryptographic keys, proprietary system files or commercial game data. Use only software and system data you are legally authorized to use. This project does not provide instructions for bypassing DRM or obtaining protected files.
