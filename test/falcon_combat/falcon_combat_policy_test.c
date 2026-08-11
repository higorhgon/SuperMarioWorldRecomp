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
    /* Production Punch is a 56px no-rear union after the playtest range
     * reduction; its vertical torso/shell band remains unchanged. */
    a.offset_x=350; a.offset_y=100; a.width=700; a.height=650;
    box=smw_falcon_attack_world_aabb(&a,100,80,1);
    CHECK(near(box.left,108.0) && near(box.right,164.0));
    CHECK(near(box.top,78.0) && near(box.bottom,130.0));
    targets[0]=(SmwFalconTarget){SMW_FALCON_TARGET_ORDINARY,{156,90,172,114}};
    targets[1]=(SmwFalconTarget){SMW_FALCON_TARGET_ORDINARY,{165,100,181,124}};
    CHECK(smw_falcon_choose_target(&a,100,80,1,targets,2)==0);
    targets[0].bounds=(SmwFalconAabb){165,90,181,114};
    CHECK(smw_falcon_choose_target(&a,100,80,1,targets,2)==-1);
    /* Production Kick is 70% of its former horizontal reach: 52.08px. */
    a.offset_x=336; a.offset_y=40; a.width=630; a.height=600;
    box=smw_falcon_attack_world_aabb(&a,100,80,1);
    CHECK(near(box.left,109.68) && near(box.right,160.08));
    CHECK(near(box.top,84.8) && near(box.bottom,132.8));
    /* Restore the compact generic hitbox for target-priority policy checks. */
    a.offset_x=25; a.offset_y=-10; a.width=50; a.height=40;
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
