#include "smw_falcon_combat_policy.h"

SmwFalconAabb smw_falcon_attack_world_aabb(const ForeignAttackHitbox *attack,
                                           double player_x, double player_y)
{
    SmwFalconAabb out = {0, 0, 0, 0};
    if (!attack || !attack->active || attack->width <= 0 || attack->height <= 0)
        return out;
    out.left = player_x + attack->offset_x * SMW_FALCON_SOURCE_TO_WORLD;
    out.top = player_y + attack->offset_y * SMW_FALCON_SOURCE_TO_WORLD;
    out.right = out.left + attack->width * SMW_FALCON_SOURCE_TO_WORLD;
    out.bottom = out.top + attack->height * SMW_FALCON_SOURCE_TO_WORLD;
    return out;
}

int smw_falcon_aabb_overlaps(SmwFalconAabb a, SmwFalconAabb b)
{
    return a.left < b.right && a.right > b.left &&
           a.top < b.bottom && a.bottom > b.top;
}

int smw_falcon_target_is_eligible(SmwFalconTargetClass target_class)
{
    return target_class == SMW_FALCON_TARGET_ORDINARY;
}

int smw_falcon_choose_target(const ForeignAttackHitbox *attack,
                             double player_x, double player_y,
                             const SmwFalconTarget *targets, int count)
{
    SmwFalconAabb hit = smw_falcon_attack_world_aabb(attack, player_x, player_y);
    if (!attack || !attack->active || !targets || count <= 0) return -1;
    for (int i = 0; i < count; ++i)
        if (smw_falcon_target_is_eligible(targets[i].target_class) &&
            smw_falcon_aabb_overlaps(hit, targets[i].bounds)) return i;
    return -1;
}

int smw_falcon_can_break_map16(const ForeignAttackHitbox *attack,
                               SmwFalconMap16Class map16_class)
{
    if (!attack || !attack->active ||
        !(attack->flags & FOREIGN_ATTACK_BREAK_BLOCKS)) return 0;
    return map16_class == SMW_FALCON_MAP16_BRICK ||
           map16_class == SMW_FALCON_MAP16_TURN_BLOCK;
}
