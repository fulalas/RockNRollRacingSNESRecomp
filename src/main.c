/*
 * Rock N' Roll Racing — desktop host shim.
 *
 * Identity and hooks, nothing else. The host itself is the framework's
 * (snesrecomp/runner/src/desktop/host_main.h): the pre-boot launcher, ROM
 * resolution and digest checks, config.ini and keybinds.ini, the window and
 * the SDL / OpenGL presenters, audio, gamepads, the in-game save-state
 * browser and rewind filmstrip, the OSD, the pacing clock, crash handlers and
 * the post-mortem report, mod packages, Generate & rebuild, and the netplay
 * barrier when the project is built with it. A fix there reaches this
 * project on a submodule pull; nothing in this file needs to change for it.
 *
 * This file used to BE the host -- a ~700-line copy made at scaffold time --
 * so every project scaffolded before a fix kept the bug. Two ports of the
 * same game, a week apart, had different hosts.
 *
 * Nothing here ever ships a ROM. The player supplies one; the host makes
 * that easy (launcher, then positional argument, then a copy beside the
 * executable, then the rom.cfg cache, then a file picker) and checks what it
 * is handed against the digests in rom_identity.txt.
 *
 * Grow this file only with what is specific to THIS title: a custom
 * presenter (prepare_frame / draw_frame), an SPC player, a pacing rule
 * (keep_pacing_debt), a Mods provider. See SnesDesktopHostGame for the
 * complete list of hooks and what each is for.
 */

#include "host_main.h"
#include "game_rtl.h"
#include "rnrr_display.h"
#include "rnrr_msu.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "rnrr_rompatch.h"
#include "sha256.h"
#include "snesrecomp_rom_identity.h"
#include "cpu_state.h"
#include "snes/interp_bridge.h"

static int hex_nibble(char c)
{
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'a' && c <= 'f') return c - 'a' + 10;
    if (c >= 'A' && c <= 'F') return c - 'A' + 10;
    return -1;
}

static void rnrr_on_rom_loaded(const uint8_t *rom, size_t size)
{
    uint8_t got[32], want[32];
    sha256_compute(rom, size, got);
    const char *hex = SNESRECOMP_ROM_EXPECTED_SHA256;
    for (int i = 0; i < 32; i++) {
        int hi = hex_nibble(hex[i * 2]), lo = hex_nibble(hex[i * 2 + 1]);
        want[i] = (uint8_t)((hi << 4) | lo);
    }
    if (strlen(hex) != 64 || memcmp(got, want, 32) != 0) {
        fprintf(stderr, "[rnrr] this build accepts only the %s dump (SHA-256 %s); refusing this file\n",
                SNESRECOMP_ROM_FILE, hex);
        exit(1);
    }
    if (size % 1024 == 512) { rom += 512; size -= 512; }
    if (RnrrModsFps30Enabled() && RnrrRomPatchApplyFps30((uint8_t *)rom, size)) {
        fprintf(stderr, "[rnrr] 30 FPS patch applied in memory\n");
    } else {
        cpu_select_interpreted_program();
        interp_bridge_set_scheduler_aot_policy(0);
        fprintf(stderr, "[rnrr] stock 20 FPS game; running through the interpreter\n");
    }
    RnrrRomPatchApply((uint8_t *)rom, size);
    RnrrMsuOnRomLoaded(rom, size);
}

static const char kRnrrDefaultConfigIni[] =
  "[General]\n"
  "# Automatically save state on quit and reload on start\n"
  "Autosave = 0\n"
  "\n"
  "# Disable the SDL_Delay that happens each frame (slightly better\n"
  "# perf if your display is set to exactly 60hz)\n"
  "DisableFrameDelay = 0\n"
  "SkipLauncher = 0\n"
  "RunAhead = 0\n"
  "\n"
  "[Graphics]\n"
  "# Window size (Auto or WidthxHeight)\n"
  "WindowSize = Auto\n"
  "\n"
  "# Fullscreen mode (0=windowed, 1=desktop fullscreen, 2=fullscreen w/mode change)\n"
  "Fullscreen = 0\n"
  "\n"
  "# Window scale (1=100%, 2=200%, 3=300%, etc.)\n"
  "WindowScale = 3\n"
  "\n"
  "# Use the optimized SNES PPU implementation\n"
  "NewRenderer = 1\n"
  "\n"
  "# Don't keep the aspect ratio\n"
  "IgnoreAspectRatio = 0\n"
  "\n"
  "# Remove the sprite limits per scan line\n"
  "NoSpriteLimits = 1\n"
  "\n"
  "DisplayAspect = 4:3\n"
  "OutputMethod = OpenGL\n"
  "Renderer = opengl\n"
  "LinearFiltering = 0\n"
  "FrameBlend = 0\n"
  "Shader = assets/shaders/ntsc-svideo.glslp\n"
  "VSync = 1\n"
  "\n"
  "[Sound]\n"
  "EnableAudio = 1\n"
  "# 0..100; VolumeUp / VolumeDown move it in 5% steps and show a bar.\n"
  "Volume = 100\n"
  "AudioFreq = 32000\n"
  "AudioChannels = 2\n"
  "AudioSamples = 512\n"
  "\n"
  "[KeyMap]\n"
  "# This section is for system-level shortcuts (save/load state,\n"
  "# fullscreen, pause, etc.). The 12 SNES controller buttons live\n"
  "# in keybinds.ini next to the executable.\n"
  "Fullscreen = Alt+Return\n"
  "Reset = Ctrl+r\n"
  "Pause = Shift+p\n"
  "PauseDimmed = p\n"
  "Turbo = Tab\n"
  "WindowBigger = Ctrl+Up\n"
  "WindowSmaller = Ctrl+Down\n"
  "VolumeUp = Keypad +\n"
  "VolumeDown = Keypad -\n"
  "DisplayPerf = f\n"
  "ToggleRenderer = r\n"
  "SaveStateMenu = F11\n"
  "Rewind = F12\n"
  "Load =      F1,     F2,     F3,     F4,     F5,     F6,     F7,     F8,     F9,     F10\n"
  "Save = Shift+F1,Shift+F2,Shift+F3,Shift+F4,Shift+F5,Shift+F6,Shift+F7,Shift+F8,Shift+F9,Shift+F10\n"
  "\n"
  "[GamepadMap]\n"
  "# Enable each player's gamepad slot. SDL_GameController-compatible\n"
  "# controllers (Xbox, PlayStation, Switch Pro, etc.) auto-detect\n"
  "# when plugged in. Set to false to force keyboard-only.\n"
  "EnableGamepad1 = true\n"
  "EnableGamepad2 = true\n"
  "\n"
  "# Default Xbox-layout mapping. Order matches kKeys_Controls:\n"
  "#   Up, Down, Left, Right, Select, Start, A, B, X, Y, L, R\n"
  "# Edit + restart to rebind. Shoulder = L1/Lb (top), trigger = L2.\n"
  "Controls =   DpadUp, DpadDown, DpadLeft, DpadRight, Back, Start, B, A, Y, X, Lb, Rb\n"
  "ControlsP2 = DpadUp, DpadDown, DpadLeft, DpadRight, Back, Start, B, A, Y, X, Lb, Rb\n"
  "GamepadDeadzone = 7864\n"
  "\n"
  "[Controller]\n"
  "SourceP1 = 2\n"
  "SourceP2 = 2\n"
  "\n"
  "[Rewind]\n"
  "Enabled = 1\n"
  "Depth = 50\n"
  "Interval = 15\n";

#ifndef __ANDROID__
#define SDL_MAIN_HANDLED 1
#endif
#include "desktop/sdl_compat.h"
#ifdef __ANDROID__
/* SDLActivity loads libmain.so and dlsyms its SDL_main. */
#define main SDL_main
#endif

#ifndef SNES_GAME_VERSION
#define SNES_GAME_VERSION "dev"
#endif

static const SnesDesktopHostGame kGameHost = {
    .display_name        = "Rock N' Roll Racing",
    .window_title        = "Rock N' Roll Racing",
    .launcher_title      = "Rock N' Roll Racing - Launcher",
    .region              = SNESRECOMP_ROM_REGION,
    .rom_file            = SNESRECOMP_ROM_FILE,
    .expected_sha256_hex = SNESRECOMP_ROM_EXPECTED_SHA256,
    .expected_crc32_hex  = SNESRECOMP_ROM_EXPECTED_CRC32,
    .game_id             = SNESRECOMP_ROM_GAME_ID,
    .build_version       = SNES_GAME_VERSION,
    .game_info           = &kGameInfo,
    .num_players         = 2,
    .sram_path           = NULL,
    .native_widescreen   = 1,
    .state_menu_hotkeys  = 1,
    .state_menu_no_pad_chord = 1,
    .display_aspect_supported = 1,
    .shader_supported    = 1,
    .rewind_settings     = 1,
    .in_game_launcher    = 1,
    .rom_digest_whole_file = 1,
    .default_config_ini  = kRnrrDefaultConfigIni,
    .on_rom_loaded       = rnrr_on_rom_loaded,
    .on_reset            = RnrrOnReset,
    .prepare_frame       = RnrrPrepareFrame,
    .begin_sim_frame     = RnrrBeginSimFrame,
    .end_sim_frame       = RnrrEndSimFrame,
    .draw_frame          = RnrrDrawFrame,
    .presentation_hz     = RnrrPresentationHz,
    .window_base_width   = RnrrWindowBaseWidth,
};

#ifndef __ANDROID__
#undef main   /* desktop: keep plain main() even if SDL_main.h remapped it */
#endif
int main(int argc, char **argv)
{
    return snesrecomp_desktop_main(&kGameHost, argc, argv);
}
