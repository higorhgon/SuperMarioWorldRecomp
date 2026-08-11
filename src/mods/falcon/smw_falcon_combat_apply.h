/* Native SMW consequences for the policy-only Falcon combat bridge. */
#ifndef SMW_FALCON_COMBAT_APPLY_H
#define SMW_FALCON_COMBAT_APPLY_H

#include "cpu_state.h"
#include "foreign_controller.h"

#include <stdint.h>

/* One sourced move may hit each sprite slot once across its linger frames,
 * while still admitting a group of distinct targets in the same frame. */
typedef struct {
    int move_state;
    int active;
    int had_sprite_contact;
    int block_applied;
    uint16_t hit_slots;
    /* Newly accepted sprite slots from the latest apply call.  The adapter
     * consumes this exact per-frame mask at ProcessNormalSprites; it is never
     * a blanket player invulnerability request. */
    uint16_t new_hit_slots;
} SmwFalconCombatLedger;

void smw_falcon_combat_ledger_update(SmwFalconCombatLedger *ledger,
                                     int move_state, int attack_active);

/*
 * Apply every previously-unhit eligible ordinary target, then (only if this
 * move has not touched a sprite) the one native collision block currently
 * under $98/$9A. This runs at M1X1 PlayerState00_00CD36, after native player
 * collision and before foreign resolve. Returns the number of sprite contacts
 * plus any native block consequence.
 */
int smw_falcon_combat_apply(CpuState *cpu, const ForeignAttackHitbox *attack,
                            float facing, SmwFalconCombatLedger *ledger,
                            ForeignCollisionResult *out_collision);

#endif
