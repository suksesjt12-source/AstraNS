# AstraNS

AstraNS is an original Android-first emulator research project. The app is written in Kotlin and the experimental CPU core is C++20.

## Current status: 0.2.0 foundation

Implemented:
- Android application shell (Kotlin).
- Native C++20 library built with Android NDK and CMake.
- A deliberately limited AArch64 instruction interpreter:
  - `NOP`
  - `MOVZ Wd/Xd, #imm16` with supported immediate shifts
  - `ADD Wd/Xd, Wn/Xn, #imm12` (without flags; SP forms are not modelled)
  - `RET Xn`
- JNI self-test that executes a small instruction sequence and expects the result `12`.
- Host-side native tests for decoding, register results, PC updates, branches, and unsupported opcodes.
- GitHub Actions that run native tests first, then build an `arm64-v8a` debug APK and upload it as an artifact.

The current interpreter intentionally rejects instructions it does not implement. It is a learning/test foundation, not a complete ARMv8-A CPU.

## Important limitations

**This milestone is not a Nintendo Switch emulator and cannot run Hollow Knight or other Switch games.**

Not implemented:
- Full ARMv8-A CPU emulation or JIT compilation.
- Guest virtual memory, MMU, exceptions, or privileged execution.
- Nintendo Switch / Horizon OS system services.
- GPU command processing, shader translation, audio, or controller input.
- Loading commercial Switch games.

The next milestone is to implement guest memory and execution tests without claiming game compatibility.

## Build APK

Every push to `main` starts the Android APK workflow. A pull request also runs the native test suite. You can manually start the APK workflow from the repository's **Actions** tab.

When the workflow succeeds:
1. Open **Actions**.
2. Select the latest **Android APK** run.
3. Open its **Artifacts** section.
4. Download `AstraNS-debug-apk` and extract `app-debug.apk`.

The debug APK is for development/testing, not a Play Store release. The APK is built for 64-bit ARM Android devices.

## Build requirements

- JDK 17
- Android SDK Platform 35
- Android NDK 27.0.12077973
- CMake 3.22.1
- Gradle 8.9

Run from the repository root:

```bash
gradle assembleDebug
```

Run the native interpreter tests on Linux with a C++20 compiler:

```bash
g++ -std=c++20 -Wall -Wextra -Werror -pedantic \
  app/src/main/cpp/arm64_interpreter.cpp \
  app/src/test/cpp/arm64_interpreter_test.cpp \
  -o astrans-native-tests
./astrans-native-tests
```

## Planned milestones

1. Expand AArch64 decoding/execution and add reference-based instruction tests.
2. Add guest memory mapping, exceptions, and execution-state management.
3. Add safe instruction fetch/decode boundaries and deterministic test programs.
4. Investigate Vulkan device capabilities and a GPU translation architecture.
5. Add input, audio, filesystem, and system-service abstractions.
6. Assess lawful Switch compatibility only after the underlying architecture is mature.

## Legal and project scope

AstraNS contains no Nintendo firmware, cryptographic keys, proprietary system files, or commercial game data. Use only software and system data you are legally authorized to use. This repository does not provide instructions for bypassing DRM or obtaining protected files.
