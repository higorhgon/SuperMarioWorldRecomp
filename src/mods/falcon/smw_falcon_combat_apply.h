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
    /* Falcon Dive is a source capture, not an impact.  Keep exactly one
     * host sprite identity through Catch until Throw releases it. */
    int dive_latched_slot;
    uint8_t dive_latched_id;
} SmwFalconCombatLedger;

void smw_falcon_combat_ledger_update(SmwFalconCombatLedger *ledger,
                                     int move_state, int attack_active);

/*
 * Apply every previously-unhit eligible ordinary target, then (only if this
 * move has not touched a sprite) the Falcon-authored destructible block
 * volume. This runs at M1X1 PlayerState00_00CD36, after native player
 * collision and before foreign resolve. Returns the number of sprite contacts
 * plus any native block consequence.
 */
int smw_falcon_combat_apply(CpuState *cpu, const ForeignAttackHitbox *attack,
                            float facing, SmwFalconCombatLedger *ledger,
                            ForeignCollisionResult *out_collision);

/* Release the one ordinary target accepted by a Falcon Dive catch.  This is
 * deliberately separate from the contact-only search: the native defeat is
 * authored by FalconDiveEnd1's Throw transition, never by contact. */
int smw_falcon_combat_release_dive(CpuState *cpu,
                                   SmwFalconCombatLedger *ledger,
                                   ForeignCollisionResult *out_collision);

/* Source CaptureCaptain physics draws both fighters together.  SMW has no
 * corresponding captured-fighter state, so return only the conservative
 * player-side portion for the host's pre-physics seam.  The delta is bounded
 * to four whole pixels and is zero when the original captured slot changed
 * lifecycle or identity. */
int smw_falcon_combat_dive_snap_delta(const CpuState *cpu,
                                      const SmwFalconCombatLedger *ledger,
                                      int *out_dx, int *out_dy);

#endif
