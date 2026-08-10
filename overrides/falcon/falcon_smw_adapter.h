#pragma once

#include "foreign_controller.h"

struct CpuState;

/* $00:D5F2 is before HandlePlayerPhysics reads gameplay input; it snapshots
 * Falcon's pad and suppresses competing native abilities. $00:DC2D and
 * PlayerState00_00CD36 remain side-effect seams for native collision/tile
 * consequences and foreign movement resolution. */
void SmwFalconBeforePlayerPhysics(struct CpuState *cpu);
void SmwFalconBeforePhysics(struct CpuState *cpu);
void SmwFalconAfterPhysics(struct CpuState *cpu);
void SmwFalconBeforeNormalSprites(struct CpuState *cpu);
void SmwFalconBeforeYoshi(struct CpuState *cpu);
void SmwFalconBeforePlayerDraw(struct CpuState *cpu);
void SmwFalconOnStateLoaded(void);

/* Later combat owns how this intent affects sprites/blocks. The first
 * playable milestone exposes it without inventing those consequences. */
const ForeignAttackHitbox *smw_falcon_last_attack(void);
const ForeignAudioEvents *smw_falcon_last_audio(void);
