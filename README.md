# Rock N' Roll Racing Recomp

A native recompilation of **Rock N' Roll Racing** (USA), built on
[snesrecomp](https://github.com/RetroPortingToolKit/snesrecomp).

Rock N' Roll Racing for SNES, recompiled to a native PC build.

> **You must legally own a copy of the game.** No ROM data is distributed with
> this project, in the repository or in any release. The recompiled C is
> generated locally from your own copy and is never committed.

## Status

Fully working.

<img width="3840" height="2160" alt="image" src="https://github.com/user-attachments/assets/e10f9857-418b-4636-8cf7-73f97bc9c6f1" />

<img width="3840" height="2160" alt="image" src="https://github.com/user-attachments/assets/ee8a64f2-cc3d-47cb-9531-a98ece8596ac" />

<img width="3840" height="2160" alt="image" src="https://github.com/user-attachments/assets/ca1e0459-5ae7-4539-8b76-0112bc62733e" />

## Features

- **Widescreen** (Mods): 16:9, 21:9, 32:9 or fit to window. Races only; menus
  stay 4:3.
- **30 FPS** (Mods, on by default): race logic runs at 30 updates per second
  instead of 20.
- **Motion interpolation** (Mods, on by default): smooth motion at your
  display's refresh rate (or a fixed 90-360 Hz). Adds 1 frame of delay.
- **CPU overclock** (Mods, on by default): prevents slowdown in busy races.
  Game speed does not change.
- **MSU-1 music** (Mods): pick a folder with an MSU-1 pack (`.pcm` tracks plus
  its `.ips` or `.bps` patch). Your ROM file is never modified. MSU-1 save
  states are kept apart, in `saves/msu1/`.
- **Save states**: Shift+F1-F10 to save, F1-F10 to load. F11 opens a save-state
  menu with 12 slots and screenshots; F12 or Select + R3 rewinds.
- **Shaders**: NTSC (composite, S-Video, RGB, monochrome), CRT Soft, LCD Grid,
  Sharp, Warm Composite, or your own in `assets/shaders/`. Pixel aspect 4:3,
  8:7 or 1:1.
- Gamepads, rebindable keys, and an in-game launcher (Ctrl+L or Select + L3).
  F shows the FPS counter.

### Widescreen notes

The game only draws the track a few pixels past its original 256-pixel view,
so the port fills the extra space with tiles it has already seen. From the
second lap on, the edges are complete; on the first lap the outer edge can
briefly show wrong tiles. At 32:9, cars far from the center can pop in.

## ROM identity

| | |
|---|---|
| File | `Rock N' Roll Racing (U).smc` (1,049,088 bytes: the 512-byte SMC copier header is part of the identity) |
| Publisher | Interplay |
| Developer | — |
| Year | 1993 |
| Mapping | lorom |
| Region | USA (North America) |
| Coprocessor | none |
| CRC32 | `3cf0de1f` |
| SHA-256 | `04cac1e7fed7c811fe12cde2a1ec2ac6bb9c3453e099fddcd914f2e6d76a1f50` |

`tools/regen.sh` refuses to run against anything else, so a mismatched
revision fails immediately instead of producing subtly wrong output.

## Linux build

```sh
git submodule update --init --recursive
bash tools/regen.sh --rom "/path/to/Rock N' Roll Racing (U).smc"
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build -j
bash tools/build-linux.sh   # AppImage in release-linux/
```

## Windows build

```sh
git submodule update --init --recursive
bash tools/regen.sh --rom "/c/path/to/Rock N' Roll Racing (U).smc"
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Release \
  -DCMAKE_C_COMPILER=x86_64-w64-mingw32-clang \
  -DCMAKE_CXX_COMPILER=x86_64-w64-mingw32-clang++ \
  -DSNESRECOMP_STATIC_RUNTIME=ON -DSNESRECOMP_FRAME_IMPL=LLE
cmake --build build -j
```

## Run

```sh
./build/RockNRollRacingSNESRecomp                     # launcher picks the ROM
./build/RockNRollRacingSNESRecomp "/path/to/Rock N' Roll Racing (U).smc"   # skip the launcher
```

With no ROM on the command line the build opens the recomp-ui launcher: a ROM
picker with a verification badge, plus display / audio / input settings. Your
choice is cached in `rom.cfg` beside the executable, so the next launch opens
on it and "Skip launcher on boot" makes it immediate. If the launcher is not
built in (`--no-recomp-ui`), the host falls back to a native file picker.

## License

This project's own source is under the license in `LICENSE`. The framework
carries its own terms — see `snesrecomp/LICENSE` and
`snesrecomp/THIRD_PARTY_ATTRIBUTION.md`. Neither covers the game data, which
is not distributed here.
