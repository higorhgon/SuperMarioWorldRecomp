#include "../../src/mods/falcon/smw_falcon_combat_policy.h"
#include <stdio.h>
#include <string.h>

#define CHECK(x) do { if (!(x)) { fprintf(stderr, "FAIL %s\n", #x); return 1; } } while (0)
int main(void) {
    ForeignAttackHitbox a; SmwFalconTarget targets[3]; SmwFalconAabb box;
    memset(&a, 0, sizeof(a)); a.active=1; a.offset_x=25; a.offset_y=-10; a.width=50; a.height=40;
    box=smw_falcon_attack_world_aabb(&a, 100, 80);
    CHECK(box.left==102 && box.top==79.2 && box.right==106 && box.bottom==82.4);
    targets[0]=(SmwFalconTarget){SMW_FALCON_TARGET_BOSS,{102,79,106,83}};
    targets[1]=(SmwFalconTarget){SMW_FALCON_TARGET_ORDINARY,{103,80,105,82}};
    targets[2]=(SmwFalconTarget){SMW_FALCON_TARGET_ORDINARY,{103,80,105,82}};
    CHECK(smw_falcon_choose_target(&a,100,80,targets,3)==1);
    targets[1].target_class=SMW_FALCON_TARGET_ALREADY_DEAD;
    CHECK(smw_falcon_choose_target(&a,100,80,targets,3)==2);
    a.flags=FOREIGN_ATTACK_BREAK_BLOCKS;
    CHECK(smw_falcon_can_break_map16(&a,SMW_FALCON_MAP16_BRICK));
    CHECK(smw_falcon_can_break_map16(&a,SMW_FALCON_MAP16_TURN_BLOCK));
    CHECK(!smw_falcon_can_break_map16(&a,SMW_FALCON_MAP16_QUESTION_BLOCK));
    CHECK(!smw_falcon_can_break_map16(&a,SMW_FALCON_MAP16_PIPE));
    CHECK(!smw_falcon_can_break_map16(&a,SMW_FALCON_MAP16_SCENERY));
    a.flags=0; CHECK(!smw_falcon_can_break_map16(&a,SMW_FALCON_MAP16_BRICK));
    puts("PASS falcon combat policy"); return 0;
}
