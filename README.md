# nts-nts-kontrol

**nts-nts-kontrol** is a native Linux librarian and editor for the **KORG NTS-1 digital kit mkII**. It is an independent reimplementation of the NTS-1 mkII librarian/editor feature set from KORG and does **not** use Wine.

![status](https://img.shields.io/badge/platform-Linux-blue) ![Qt](https://img.shields.io/badge/Qt-6-green)

## Features

- Automatic NTS-1 mkII detection over PipeWire MIDI or raw ALSA MIDI (no KORG USB driver, no Wine)
- Current program receive/write and local program library
- Import/export `.nts1mkiiprog` program files
- User oscillator, modulation FX, delay FX, and reverb FX slot management
- User unit receive/upload, clear, reorder, and import/export `.nts1mkiiunit`
- Named global settings editor (input routing, gain, MIDI channel, clock, sync, etc.)
- Official `.nts1mkii` workspace compatibility (256-byte header + ZIP)
- Receive All / Send All data
- Progress dialogs, write confirmation, and preferences

## Requirements

- Linux with ALSA (PipeWire or plain ALSA)
- Qt 6.5+ (Core, Gui, Widgets, Concurrent, Test)
- CMake 3.20+, Ninja, and a C++20 compiler
- PipeWire's JACK MIDI layer for the preferred backend, or raw ALSA for the fallback
- No `alsa-lib-devel` package is required: the raw fallback uses the kernel raw MIDI ABI (`<sound/asound.h>`) directly

On Fedora 44 / KDE Plasma, install the toolchain and Qt 6 development packages with:

```bash
sudo dnf install gcc-c++ cmake ninja-build qt6-qtbase-devel qt6-qtwayland \
    pipewire-jack-audio-connection-kit-devel
```

`qt6-qtwayland` is needed for native Wayland; omit it only if you use an X11 session.
`pipewire-jack-audio-connection-kit-devel` provides the `jack` pkg-config files used by the
PipeWire backend. `pipewire-devel` is not required.

Tested on Fedora 44 with Qt 6.11.2, GCC 16.2.1, PipeWire 1.6.10, and an NTS-1 mkII
(SDK 1.3). The same source also builds on Arch with Qt 6.12/GCC 16; use a clean build
directory per distribution.

## Build

```bash
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Release
cmake --build build -j"$(nproc)"
ctest --test-dir build --output-on-failure
```

The GUI binary is `build/nts-nts-kontrol`. A small protocol/debug CLI is built as `build/nts-nts-kontrol-cli`.
CMake enables the PipeWire backend automatically when `pkg-config jack` is available;
otherwise it builds the raw ALSA backend only.

If you configured the tree on another distribution first, delete the cached build directory before configuring again. CMake records absolute Qt and generator paths; a binary built against a newer Qt (for example Qt 6.12) will not start with Fedora's Qt 6.11 and may fail with `version 'Qt_6.12' not found`.

### Install

```bash
cmake --install build --prefix "$HOME/.local"
```

This installs the binary, desktop entry, and icon. Make sure `$HOME/.local/bin` is in your `PATH`.

## Usage

1. Connect the NTS-1 mkII with USB.
2. Start `nts-nts-kontrol`.
3. The device is detected automatically. If not, click **Refresh**.
4. Use the tabs to manage programs, user units, and global settings.
5. Workspaces are saved as `.nts1mkii` files and can be opened by the official KORG editor.

The app supports three MIDI backend modes:

- **Auto** (default): prefer PipeWire, fall back to raw ALSA.
- **PipeWire (JACK MIDI)**: uses the NTS-1 ports on PipeWire's `Midi-Bridge`.
- **ALSA raw**: opens `/dev/snd/midiC*D*` directly.

PipeWire normally owns the raw MIDI device, so raw ALSA can fail with `EBUSY` while
PipeWire is running. Auto therefore selects PipeWire when the NTS-1 is exposed there.
The backend can be selected in **Edit → Preferences → MIDI backend** or with
`nts-nts-kontrol-cli --backend auto|pipewire|alsa`.

### MIDI Route and USB MIDI

Setting **MIDI Route** to **MIDI** disables the NTS-1 mkII's USB MIDI port. The
editor warns before writing, then loses its USB connection. Change MIDI Route
back on the device and click **Refresh** to reconnect.

### Permissions

The PipeWire backend needs access to the user's PipeWire session. The raw ALSA fallback
needs access to `/dev/snd/midiC*D*` and `/dev/snd/controlC*`. On most desktop systems
PipeWire/udev already grants access. If not, add yourself to the `audio` group or install
an appropriate udev ACL rule.

## File formats

- **Program** (`.nts1mkiiprog`): raw 504-byte NTS-1 mkII program (`PROG` … `PRED`).
- **User unit** (`.nts1mkiiunit`): raw ELF with the logue SDK `.unit_header`. The app parses the header for name, developer ID, unit ID, API version, and parameter metadata.
- **Workspace** (`.nts1mkii`): official-compatible container:
  - 256-byte header (`NTS-1_MKII LIBRARY`)
  - SHA-256 of the ZIP payload stored at header offset 40
  - ZIP entries: `program` (101 × 664-byte records), `oscXX`, `modfxXX`, `delfxXX`, `revfxXX`, `global`
  - The app additionally writes `global_raw` with the 32-byte device global block so global settings survive a round-trip. This is outside the official schema; the official editor has been verified to open workspaces containing programs and user units. Keep an original workspace if you need to exchange global settings with the official editor.

## CLI

`nts-nts-kontrol-cli` is useful for scripting and protocol testing:

```bash
nts-nts-kontrol-cli list
nts-nts-kontrol-cli list --backend pipewire
nts-nts-kontrol-cli program --dump current.nts1mkiiprog
nts-nts-kontrol-cli program --send current.nts1mkiiprog
nts-nts-kontrol-cli global --dump global.bin
nts-nts-kontrol-cli status --module osc --slot 0
nts-nts-kontrol-cli download --module osc --slot 0 --file unit.nts1mkiiunit
nts-nts-kontrol-cli upload   --module osc --slot 0 --file unit.nts1mkiiunit
nts-nts-kontrol-cli clear    --module osc --slot 15
nts-nts-kontrol-cli swap     --module osc --a 0 --b 1
```

Developer helpers:

```bash
nts-nts-kontrol-cli rewrite in.nts1mkii out.nts1mkii [--global global.bin]
nts-nts-kontrol-cli mkws out.nts1mkii [program.nts1mkiiprog] [--global global.bin]
```

## Tests

```bash
ctest --test-dir build --output-on-failure
./build/test_core
```

The test suite covers the 7-bit codec, CRC32, SysEx status parsing, realtime-byte filtering, backend preference parsing, user-slot chunking, global field mapping, and official workspace round-trip including the SHA-256 header.

## Protocol notes

The implementation follows the official NTS-1 mkII MIDI implementation and the protocol notes published in the BSD-3-licensed `py-logue-tools` project. KORG SysEx messages use the header `F0 42 3g 00 01 73 … F7`; the current program and global blocks are 7-bit packed. User units are uploaded in `4A` messages as `[length][CRC32][ELF]` with 2048-byte host chunks.

The PipeWire backend uses PipeWire's JACK MIDI layer. Native `pw_stream` targeting of individual
`Midi-Bridge` ports is deprecated, while the JACK layer exposes stable named MIDI ports and
routes SysEx correctly through the PipeWire graph.

## Limitations

- Only the NTS-1 mkII is implemented. The device abstraction is ready for other logue devices.
- Program parameter values are shown as part names (oscillator/filter/etc.); there is no full per-parameter patch editor, matching the official NTS-1 mkII librarian scope.
- The local library supports import/export and slot reordering via Move Up/Down. The official workspace library format is read/written.
- Global settings are stored in the workspace via the extra `global_raw` entry; the official 48-byte `global` entry is preserved but not remapped field-by-field.

## Credits

- Protocol and file format research: KORG NTS-1 mkII MIDI implementation, logue SDK, and the BSD-3 `py-logue-tools` project.
- ZIP support: [miniz](https://github.com/richgel999/miniz) (MIT/public domain).
- This project is not affiliated with or endorsed by KORG Inc.
