# Rock N' Roll Racing Recomp

A native recompilation of **Rock N' Roll Racing** (USA), built on
[snesrecomp](https://github.com/RetroPortingToolKit/snesrecomp).

Rock N' Roll Racing for SNES, recompiled to a native PC build.

> **You must legally own a copy of the game.** No ROM data is distributed with
> this project, in the repository or in any release. The recompiled C is
> generated locally from your own copy and is never committed.

## Status

Fully working.

## Features

- Save states with a 12-slot browser and thumbnails (**F7** or Select + R),
  quick slots F1-F10, rewind filmstrip (**F8** or Select + R3). Rewind depth
  and interval are set in the launcher.
- Display shaders (CRT Soft, LCD Grid, Sharp, Warm Composite, or your own
  `.glsl`/`.glslp` dropped into `assets/shaders/`) and the three SNES pixel
  aspects (4:3, 8:7, 1:1).
- **Widescreen** (Mods page): 16:9, 21:9, 32:9, or fit to window. Races render
  wider; the HUD anchors to the screen edges; menus and the title stay 4:3.
- **Motion interpolation** (Mods page, on by default): present at the display
  refresh or a fixed 90-360 Hz, blending background scrolling and sprite
  positions between the game's logic updates (every 2nd field with the 30 FPS
  feature, every 3rd without). Costs 1 frame of display delay. The F readout
  shows a second line: game updates, presented frames, interpolated frames and
  simulated fields per second.
- **30 FPS game logic** (Mods page, on by default): the race logic runs at
  30 updates per second instead of 20, with the original pacing. See "One ROM,
  two speeds" below.
- **CPU overclock** (Mods page, on by default): charges CPU instructions fewer
  master clocks during races so the game's logic never overruns its frame
  budget. Auto is 150% with the 30 FPS feature on, which otherwise drops to
  about 26-28 updates per second in busy scenes, and 100% with it off. The
  game's update rate is divider-driven, so a faster CPU does not change game
  speed.
- **MSU-1 music** (Mods page): point the feature at a folder holding a
  Rock N' Roll Racing MSU-1 pack (`.pcm` tracks plus its `.ips` or `.bps`
  patch). The patch is applied in memory, the stock ROM is never modified, and
  the patched cartridge runs through the interpreter. MSU-1 save states live
  in `saves/msu1/` and are not interchangeable with stock ones.
- Gamepads through SDL, rebindable keys and hotkeys in the launcher, and the
  in-game launcher (Ctrl+L or Select + L3).
- **NTSC shaders**: `ntsc-composite`, `ntsc-svideo`, `ntsc-rgb`, and
  `ntsc-monochrome` mimic Snes9x's Blargg NTSC filter. Hue, saturation,
  contrast, brightness, sharpness, gamma, artifacts, fringing, bleed,
  resolution, scanline intensity (12.5% default), and merge-fields are shader
  parameters (`ntsc_*`), settable per preset in the `.glslp`.

### Widescreen notes

The game only streams track tiles a few pixels past the native 256-pixel
view. The port keeps a world-keyed cache of every tile the native view has
shown, so margins are exact for any area seen before (from the second lap
on). On the first visit, the outermost part of the leading margin can show
the tilemap's wrapped content until the game streams it. Cars are emitted by
the game only within roughly 128 px of the native view, so at 32:9 they can
pop in at the far margins.

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
