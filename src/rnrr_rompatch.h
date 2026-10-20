#ifndef RNRR_ROMPATCH_H
#define RNRR_ROMPATCH_H

#include <stddef.h>
#include <stdint.h>

int RnrrRomPatchApply(uint8_t *rom, size_t size);
int RnrrRomPatchApplyFps30(uint8_t *rom, size_t size);

#endif
