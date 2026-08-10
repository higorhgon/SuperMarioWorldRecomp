/* Pure, host-contract-free Falcon combat policy. */
#ifndef SMW_FALCON_COMBAT_POLICY_H
#define SMW_FALCON_COMBAT_POLICY_H

#include "foreign_controller.h"

typedef struct { double left, top, right, bottom; } SmwFalconAabb;
typedef enum {
    SMW_FALCON_TARGET_ORDINARY,
    SMW_FALCON_TARGET_BOSS,
    SMW_FALCON_TARGET_HAZARD,
    SMW_FALCON_TARGET_ALREADY_DEAD,
    SMW_FALCON_TARGET_UNSUPPORTED
} SmwFalconTargetClass;
typedef struct {
    SmwFalconTargetClass target_class;
    SmwFalconAabb bounds;
} SmwFalconTarget;
typedef enum {
    SMW_FALCON_MAP16_BRICK,
    SMW_FALCON_MAP16_TURN_BLOCK,
    SMW_FALCON_MAP16_QUESTION_BLOCK,
    SMW_FALCON_MAP16_PIPE,
    SMW_FALCON_MAP16_SCENERY,
    SMW_FALCON_MAP16_UNKNOWN
} SmwFalconMap16Class;

#define SMW_FALCON_SOURCE_TO_WORLD 0.08

SmwFalconAabb smw_falcon_attack_world_aabb(const ForeignAttackHitbox *attack,
                                           double player_x, double player_y);
int smw_falcon_aabb_overlaps(SmwFalconAabb a, SmwFalconAabb b);
int smw_falcon_target_is_eligible(SmwFalconTargetClass target_class);
/* Returns one eligible contact at most. -1 means no host consequence. */
int smw_falcon_choose_target(const ForeignAttackHitbox *attack,
                             double player_x, double player_y,
                             const SmwFalconTarget *targets, int count);
int smw_falcon_can_break_map16(const ForeignAttackHitbox *attack,
                               SmwFalconMap16Class map16_class);

#endif
