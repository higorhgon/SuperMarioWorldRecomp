#pragma once

#include "foreign_controller.h"

struct CpuState;

/* Injected at UpdatePlayerSpritePosition and PlayerState00_00CD36. These are deliberately
 * side-effect hooks: the generated routine still performs native SMW collision
 * and tile consequences. */
void SmwFalconBeforePhysics(struct CpuState *cpu);
void SmwFalconAfterPhysics(struct CpuState *cpu);

/* Later combat owns how this intent affects sprites/blocks. The first
 * playable milestone exposes it without inventing those consequences. */
const ForeignAttackHitbox *smw_falcon_last_attack(void);
const ForeignAudioEvents *smw_falcon_last_audio(void);
