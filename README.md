# AstraNS

AstraNS is an original Android-first emulator research project. The Android shell is Kotlin; the experimental guest CPU and memory core is C++20.

## Current status: 0.3.0 — guest execution foundation

Implemented:
- Native Android application shell and JNI bridge.
- A bounded guest-memory region with checked reads/writes and little-endian 32/64-bit access.
- A guest fetch/decode/execute loop with an instruction budget and stop-address support for deterministic tests.
- A small AArch64 instruction subset: NOP; MOVZ/MOVN/MOVK; ADD/SUB immediate with basic NZCV flags; unsigned-immediate integer LDR/STR for byte, halfword, word and doubleword; B, BL, B.cond and RET.
- Automated host tests for guest-memory bounds, register operations, load/store, branch/link/return, instruction budgets, and unsupported opcodes.
- GitHub Actions that run native tests, then build and upload the arm64-v8a debug APK.

## Important limitations

**AstraNS is not yet a Nintendo Switch emulator and cannot run Hollow Knight or other Switch games.** This milestone executes only a limited subset of AArch64 instructions in a test guest-memory region. It has no Switch boot process or Horizon OS compatibility.

Not implemented:
- Full ARMv8-A instruction coverage, SIMD/NEON, FP, atomics, exceptions, MMU, page tables, or JIT.
- Switch kernel and system services, IPC, filesystem interfaces or required runtime libraries.
- Tegra X1 GPU command processor, Maxwell features, shader translation or Vulkan renderer.
- Audio, controller input, save data, game loader or commercial game compatibility.

## Build APK

Every push to `main` triggers the Android APK workflow. Pull requests run the native test suite. You can also run the workflow manually from the repository's **Actions** tab.

When the workflow succeeds:
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

Run:

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

1. Expand and validate AArch64 instruction behavior, including SIMD/NEON and exceptions.
2. Add a guest virtual address space, page permissions and memory mapping abstractions.
3. Add privileged CPU state and a system exception/interrupt model.
4. Design Horizon-compatible kernel objects and IPC only from public, lawful specifications.
5. Build a GPU command translation architecture and validate Vulkan capabilities on target devices.
6. Add audio, controller, storage and save-state systems.
7. Assess title compatibility only after system boot and graphics fundamentals work.

## Legal scope

AstraNS includes no Nintendo firmware, cryptographic keys, proprietary system files, or commercial game data. Use only software and system data you are legally authorized to use. This project does not provide instructions for bypassing DRM or obtaining protected files.
