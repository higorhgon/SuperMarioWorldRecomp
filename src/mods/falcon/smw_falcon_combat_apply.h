/* Native SMW consequences for the policy-only Falcon combat bridge. */
#ifndef SMW_FALCON_COMBAT_APPLY_H
#define SMW_FALCON_COMBAT_APPLY_H

#include "cpu_state.h"
#include "foreign_controller.h"

/*
 * Apply at most one ordinary-sprite hit and the one native collision block
 * currently under $98/$9A.  This must run at the M1X1 PlayerState00_00CD36
 * hook, immediately after native player collision and before foreign resolve.
 */
/* Returns nonzero only when the native sprite or block route actually took a
 * consequence.  Callers use this to make a lingering host attack window
 * consume one native consequence rather than one per frame. */
int smw_falcon_combat_apply(CpuState *cpu, const ForeignAttackHitbox *attack,
                            float facing, ForeignCollisionResult *out_collision);

#endif
