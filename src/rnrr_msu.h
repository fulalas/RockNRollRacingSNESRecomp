#ifndef RNRR_MSU_H
#define RNRR_MSU_H

#include <stddef.h>
#include <stdint.h>

void RnrrMsuOnRomLoaded(const uint8_t *rom, size_t size);
int RnrrMsuActive(void);
const char *RnrrMsuError(void);
int RnrrMsuSelectSaveRoot(void);
const char *RnrrMsuSnapshotIdentity(void);

#endif
