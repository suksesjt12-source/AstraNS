# AstraNS PC Game Launcher (experimental flavor)

This source set adds a separate Android application variant for importing user-owned Windows game ZIP archives. It is isolated from the Nintendo Switch emulator flavor.

## Build variants
- Switch application: `switchDebug`
- PC launcher application: `pcLauncherDebug`
- PC launcher application ID: `org.astrans.emulator.pc`

## Implemented in this milestone
- Android Storage Access Framework ZIP selection.
- Streaming ZIP extraction to app-specific external storage, with internal-storage fallback.
- ZIP path traversal checks, entry-count cap, and 25 GiB expanded-data cap.
- Detection of `NSUNSR.exe`.
- Persistent list of imported game folders.

## Not implemented
This milestone does not execute Windows binaries. Wine, Box64, a compatible root filesystem, DXVK/Vulkan, audio, input mapping, and process lifecycle integration remain future work. The app must not claim that a game can launch until those components are integrated and device-tested. No copyrighted game files are bundled in the APK.
