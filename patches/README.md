Local changes to the two submodules, as `git diff` output against the pinned
commits. CMake configure and `tools/regen.sh` apply them automatically
(`tools/apply_patches.sh`; already-applied patches are skipped). After moving
a submodule pin, regenerate the file with `git -C <submodule> diff`.

snesrecomp.patch
- beam_frame_driver.c: pay beam debt before a slice and cap the slice at the
  next IRQ match (hero-select split timing).
- interp_bridge.c/.h: `g_interp_bridge_master_scale_pct` (CPU overclock for
  interpreted code).
- rom_image_verify.c/.h, mod_runtime.cpp, desktop/host_main.c/.h:
  `rom_digest_whole_file` verifies the ROM file as stored, header included.
- snes_savestate_menu.c/.h, desktop/host_main.c/.h: `state_menu_no_pad_chord`
  disables the Select+R controller chord for the save-state browser.
- snes/ppu.c: BG modes 4-6 use the per-pixel renderer; the fast one only
  covers 0-3 and 7 (fixes the equipment shop, mode 4).
- snes/dsp.c: a voice is silent on its key-on sample; it played one stale
  sample at the new gain, a click on every direct-gain note start.
- snes/dsp.c: the Gaussian sum clamps instead of wrapping at 16 bits; loud
  samples (the engine) flipped to the opposite peak, a click on wall hits.
- snes/dsp.c/.h: a voice cut off by key-on or sample end fades its last
  output (after volume) to zero over 32 samples instead of dropping it
  (voice-steal clicks). The fade only feeds the dry mix, never the echo
  buffer, and its state sits before `ram`, outside save states and digests.
- snes/dsp.c: a voice fading out (key-off release) keeps its pitch and sample
  until it is silent or keyed on again; the RNRR driver loads the next sound's
  pitch during the release and the explosion tail turned into a noise burst.

recomp-ui.patch
- `digest_whole_file` on the launcher game info: hash the picked ROM whole.
