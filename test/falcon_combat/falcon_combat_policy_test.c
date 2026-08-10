#include "../../src/mods/falcon/smw_falcon_combat_policy.h"
#include <stdio.h>
#include <string.h>

#define CHECK(x) do { if (!(x)) { fprintf(stderr, "FAIL %s\n", #x); return 1; } } while (0)
static int near(double a, double b) { double d = a - b; return d < 0.00001 && d > -0.00001; }
int main(void) {
    ForeignAttackHitbox a; SmwFalconTarget targets[3]; SmwFalconAabb box;
    memset(&a, 0, sizeof(a)); a.active=1; a.offset_x=25; a.offset_y=-10; a.width=50; a.height=40;
    box=smw_falcon_attack_world_aabb(&a, 100, 80, 1);
    CHECK(near(box.left,108) && near(box.top,111.2) && near(box.right,112) && near(box.bottom,114.4));
    box=smw_falcon_attack_world_aabb(&a, 100, 80, -1);
    CHECK(near(box.left,104) && near(box.right,108));
    targets[0]=(SmwFalconTarget){SMW_FALCON_TARGET_BOSS,{108,111,112,115}};
    targets[1]=(SmwFalconTarget){SMW_FALCON_TARGET_ORDINARY,{109,112,111,114}};
    targets[2]=(SmwFalconTarget){SMW_FALCON_TARGET_ORDINARY,{109,112,111,114}};
    CHECK(smw_falcon_choose_target(&a,100,80,1,targets,3)==1);
    targets[1].target_class=SMW_FALCON_TARGET_ALREADY_DEAD;
    CHECK(smw_falcon_choose_target(&a,100,80,1,targets,3)==2);
    a.flags=FOREIGN_ATTACK_BREAK_BLOCKS;
    CHECK(smw_falcon_can_break_map16(&a,SMW_FALCON_MAP16_BRICK));
    CHECK(smw_falcon_can_break_map16(&a,SMW_FALCON_MAP16_TURN_BLOCK));
    CHECK(!smw_falcon_can_break_map16(&a,SMW_FALCON_MAP16_QUESTION_BLOCK));
    CHECK(!smw_falcon_can_break_map16(&a,SMW_FALCON_MAP16_PIPE));
    CHECK(!smw_falcon_can_break_map16(&a,SMW_FALCON_MAP16_SCENERY));
    a.flags=0; CHECK(!smw_falcon_can_break_map16(&a,SMW_FALCON_MAP16_BRICK));
    puts("PASS falcon combat policy"); return 0;
}
