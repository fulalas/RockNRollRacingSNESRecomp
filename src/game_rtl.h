#ifndef GAME_RTL_H
#define GAME_RTL_H

#include "common_cpu_infra.h"

/* One frame of Rock N' Roll Racing. See game_rtl.c — this is the port. */
void GameInitialize(void);
void GameRunOneFrame(void);
void GameDrawPpuFrame(void);
void GameSessionReset(void);

extern const RtlGameInfo kGameInfo;

#endif /* GAME_RTL_H */
