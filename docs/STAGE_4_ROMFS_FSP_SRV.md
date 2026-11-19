# AstraNS Stage 4: NRO RomFS and fsp-srv

## What is added

The native NRO asset reader locates the ASET header at the NRO-declared image size. The RomFS asset offset is interpreted relative to the start of ASET, checked against the selected file length, then copied into an owned buffer.

The raw Switch RomFS parser validates the ten 64-bit header fields, table boundaries, root directory entry, linked metadata references, file-data ranges and names. Directory traversal uses sibling links with cycle detection. Paths cannot contain parent traversal components. File reads are read-only and limited to 1 MiB per request.

The experimental `fsp-srv` CMIF handler is registered in AstraNS's native `CmifServiceManager`. Its current command identifiers are AstraNS-internal and intentionally do not claim to be the complete Nintendo ABI:

| Command | ID | Input | Output |
| --- | ---: | --- | --- |
| GetStatus | 0 | none | protocol version, mount state, image size |
| OpenFile | 1 | u32 path length + UTF-8 path | internal handle + file size |
| ReadFile | 2 | handle, u64 offset, u32 maximum length | bytes read + data |
| CloseFile | 3 | handle | empty payload |
| ListDirectory | 4 | u32 path length + UTF-8 path | typed entry list |

The Android JNI NRO loader invokes extraction and mount after the NRO image has loaded successfully. The status report distinguishes a mapped executable from a booted guest.

## Validation

The Stage 4 native test creates an in-memory NRO with an ASET section and a minimal valid RomFS partition. It checks extraction, mount, root directory listing, file reads, path traversal rejection, the per-read limit, CMIF service registration, OpenFile/ReadFile/CloseFile and malformed table rejection.

The GitHub Actions workflow compiles and runs this test separately from the existing CPU and guest-memory test suite.

## Deliberate boundaries

This stage supports raw RomFS data in NRO assets. IVFC-wrapped images and integrity verification are not implemented. The handler is registered in the host-side CMIF service manager but is not yet connected to a running Horizon guest process through a complete HIPC transport. The filesystem is read-only. This does not make AstraNS able to boot Horizon OS or launch commercial Switch games.
