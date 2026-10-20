#ifndef RNRR_DISPLAY_H
#define RNRR_DISPLAY_H

#include <stddef.h>
#include <stdint.h>

enum {
    RNRR_WS_OFF = 0,
    RNRR_WS_16_9,
    RNRR_WS_21_9,
    RNRR_WS_32_9,
    RNRR_WS_FIT,
};

enum {
    RNRR_MAX_FRAME_WIDTH = 682,
};

void RnrrDisplaySetWidescreen(int mode);
int RnrrDisplayWidescreenMode(void);
void RnrrDisplaySetPresentationFps(int enabled, unsigned fps);
int RnrrDisplayPresentationEnabled(void);

void RnrrPrepareFrame(int drawable_w, int drawable_h, int *frame_w, int *frame_h);
void RnrrBeginSimFrame(unsigned number);
void RnrrEndSimFrame(const uint8_t *field, unsigned number);
int RnrrDrawFrame(uint8_t *dst, size_t pitch, const uint8_t *field,
                  int frame_w, int frame_h, double alpha);
double RnrrPresentationHz(double display_refresh);
int RnrrWindowBaseWidth(int frame_w);
int RnrrModsWidescreenMode(void);
int RnrrModsFps30Enabled(void);
void RnrrDisplayApplyEnv(void);
void RnrrSetOverclock(unsigned percent);
unsigned RnrrOverclock(void);
void RnrrOnReset(void);

struct Ppu;
void RnrrRasterObserve(const struct Ppu *ppu, unsigned line, void *context);

int RnrrInRace(void);

#endif
