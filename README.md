# AstraNS

AstraNS is an original Android-first emulator research project. The repository starts with a native Kotlin + C++20 application and a deliberately tiny AArch64 interpreter test.

## Current status: 0.1.0 foundation

Implemented:
- Android application shell (Kotlin).
- Native C++20 library built with Android NDK and CMake.
- ARM64/AArch64 instruction subset: `MOVZ Xd, #imm16`, `ADD Xd, Xn, #imm12`, `NOP`, and `RET X30`.
- JNI self-test: executes a small instruction sequence and expects the result `12`.
- GitHub Actions workflow that builds an arm64-v8a debug APK and uploads it as an artifact.

Not implemented:
- Nintendo Switch/Horizon OS compatibility.
- Full ARMv8-A CPU emulation, JIT, virtual memory, GPU command processing, shader translation, audio, input or system services.
- Game loading or commercial game compatibility.

**This milestone is not a Nintendo Switch emulator yet and cannot run Hollow Knight or other Switch games.** The small interpreter exists only to establish and test the native execution pipeline.

## Build APK

Every push to `main` starts the Android APK workflow. You can also start it manually from the repository's **Actions** tab.

When the workflow succeeds:
1. Open **Actions**.
2. Select the latest **Android APK** run.
3. Open its **Artifacts** section.
4. Download `AstraNS-debug-apk` and extract `app-debug.apk`.

The debug APK is for development/testing, not a Play Store release.

## Local build requirements

- JDK 17
- Android SDK
- Android NDK 27.0.12077973
- CMake 3.22.1
- Gradle 8.9

Run:

```bash
gradle assembleDebug
```

## Planned milestones

1. Expand and unit-test AArch64 decoding/execution.
2. Add guest memory mapping, exceptions and execution state.
3. Define an emulator core architecture and deterministic test programs.
4. Investigate Vulkan device capability and a GPU translation design.
5. Add input, audio, filesystem and system-service abstractions.
6. Only then assess the feasibility of a lawful Switch compatibility layer.

## Legal and project scope

AstraNS contains no Nintendo firmware, cryptographic keys, proprietary system files or commercial game data. Use only software and system data you are legally authorized to use. This repository does not provide instructions for bypassing DRM or obtaining protected files.
