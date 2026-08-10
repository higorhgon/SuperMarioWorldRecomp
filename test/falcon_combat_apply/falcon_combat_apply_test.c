#include "smw_falcon_combat_apply.h"
#include <stdint.h>
#include <stdio.h>
#include <string.h>

static uint8_t s_ram[0x20000];
static int s_sprite_calls, s_block_calls, s_native_contact_effects, s_expected_slot;

void CheckPlayerAttackToNormalSpriteColl_029404(CpuState *cpu)
{
    if (cpu->m_flag != 1 || cpu->x_flag != 1 || cpu->DB != 2 ||
        cpu->D != 0 || (cpu->X & 0xff) != s_expected_slot || cpu->ram[0x0e]) {
        fprintf(stderr, "bad sprite contract\n"); return;
    }
    ++s_sprite_calls;
    /* CODE_029404 calls CODE_01AB6F (native kick SFX/contact effect) and
     * GivePoints before changing the admitted target's status. */
    ++s_native_contact_effects;
    cpu->ram[0x14c8 + s_expected_slot] = 2;
    cpu->A = 0xbeef; cpu->DB = 0xaa; cpu->ram[4] = 0xee;
}
void SpawnBounceSprite(CpuState *cpu)
{
    if (cpu->m_flag != 1 || cpu->x_flag != 1 || cpu->DB != 2 || cpu->D != 0) {
        fprintf(stderr, "bad block contract\n"); return;
    }
    ++s_block_calls;
    cpu->ram[0x1dfc] = 7; cpu->ram[0x9c] = 2; cpu->ram[0x7d] = 0xd0;
    cpu->Y = 0xdead; cpu->ram[4] = 0xee;
}
static int failed(const char *x, int n) { fprintf(stderr, "FAIL %d: %s\n", n, x); return 1; }
#define CHECK(x) do { if (!(x)) return failed(#x, __LINE__); } while (0)
static void put16(unsigned p, uint16_t x) { s_ram[p]=(uint8_t)x; s_ram[p+1]=(uint8_t)(x>>8); }
static CpuState fresh(void) {
    CpuState c; memset(&c,0,sizeof(c)); memset(s_ram,0,sizeof(s_ram));
    c.ram=s_ram; c.m_flag=c.x_flag=1; c.P=0x30; return c;
}
static ForeignAttackHitbox attack(void) {
    ForeignAttackHitbox a; memset(&a,0,sizeof(a)); a.active=1;
    /* Same 80px-forward / full-torso host contact envelope as the
     * controller's lingered Falcon Punch window. */
    a.offset_x=560; a.offset_y=160; a.width=900; a.height=400;
    a.flags=FOREIGN_ATTACK_BREAK_BLOCKS; return a;
}
static void install_sprite(unsigned slot, uint8_t id, uint16_t x, uint16_t y) {
    s_ram[0x14c8 + slot]=8; s_ram[0x9e + slot]=id;
    s_ram[0xe4 + slot]=(uint8_t)x; s_ram[0x14e0 + slot]=(uint8_t)(x>>8);
    s_ram[0xd8 + slot]=(uint8_t)y; s_ram[0x14d4 + slot]=(uint8_t)(y>>8);
}
int main(void) {
    CpuState cpu=fresh(); ForeignAttackHitbox a=attack(); ForeignCollisionResult hit;
    uint8_t scratch[16], interaction[5]; int calls;
    put16(0x94,100); put16(0x96,100);
    /* A target 76px ahead is beyond the former 64px front edge but inside
     * the deliberate two-body host contact envelope. */
    install_sprite(3,0x0f,184,120); install_sprite(5,0x0f,186,110); s_expected_slot=3;
    memset(s_ram,0x5a,16); memcpy(scratch,s_ram,16); memset(&hit,0,sizeof(hit));
    smw_falcon_combat_apply(&cpu,&a,1,&hit);
    CHECK(s_sprite_calls==1 && s_native_contact_effects==1 &&
          s_ram[0x14cb]==2 && s_ram[0x14cd]==8 && hit.attack_connected);
    CHECK(memcmp(s_ram,scratch,16)==0 && cpu.DB==0 && cpu.A==0);

    /* IDs $04-$07 are upright shelled Koopas and retain the 16x24 union.
     * A loose shell (native status $09) is deliberately excluded. */
    cpu=fresh(); put16(0x94,100); put16(0x96,100); install_sprite(2,0x04,184,120); s_expected_slot=2;
    calls=s_sprite_calls; smw_falcon_combat_apply(&cpu,&a,1,&(ForeignCollisionResult){0});
    CHECK(s_sprite_calls==calls+1 && s_ram[0x14ca]==2);
    cpu=fresh(); put16(0x94,100); put16(0x96,100); install_sprite(2,0x04,184,120); s_ram[0x14ca]=9;
    calls=s_sprite_calls; smw_falcon_combat_apply(&cpu,&a,1,&(ForeignCollisionResult){0}); CHECK(s_sprite_calls==calls);

    /* Facing mirrors source X offset; the left target is selected only left. */
    cpu=fresh(); put16(0x94,100); put16(0x96,100); install_sprite(1,0x0f,80,110); s_expected_slot=1;
    smw_falcon_combat_apply(&cpu,&a,-1,&(ForeignCollisionResult){0}); CHECK(s_ram[0x14c9]==2);

    cpu=fresh(); put16(0x94,100); put16(0x96,100); put16(0x9a,128); put16(0x98,112);
    s_ram[4]=7; s_ram[0x7c]=0x33; s_ram[0x7d]=0x44; memcpy(scratch,s_ram,16); memcpy(interaction,s_ram+0x98,5);
    calls=s_block_calls; smw_falcon_combat_apply(&cpu,&a,1,&(ForeignCollisionResult){0});
    CHECK(s_block_calls==calls+1 && memcmp(s_ram,scratch,16)==0 && memcmp(s_ram+0x98,interaction,5)==0);
    CHECK(s_ram[0x7c]==0x33 && s_ram[0x7d]==0x44);

    /* A lingering host frame receives one native consequence: sprite contact
     * wins and the same frame must not also break the current collision
     * block. The adapter latches this successful return for the remainder of
     * the Punch contact window. */
    cpu=fresh(); put16(0x94,100); put16(0x96,100); put16(0x9a,128); put16(0x98,112);
    install_sprite(4,0x0f,184,120); s_expected_slot=4; s_ram[4]=7;
    calls=s_sprite_calls; int block_before=s_block_calls;
    CHECK(smw_falcon_combat_apply(&cpu,&a,1,&hit));
    CHECK(s_sprite_calls==calls+1 && s_block_calls==block_before && s_ram[0x14cc]==2);

    /* Question/pipe/scenery paths never invoke the native transaction. */
    cpu=fresh(); put16(0x94,100); put16(0x96,100); put16(0x9a,128); put16(0x98,112); s_ram[0x1693]=0x21;
    calls=s_block_calls; smw_falcon_combat_apply(&cpu,&a,1,&(ForeignCollisionResult){0}); CHECK(s_block_calls==calls);
    cpu=fresh(); cpu.DB=1; calls=s_sprite_calls; smw_falcon_combat_apply(&cpu,&a,1,&(ForeignCollisionResult){0}); CHECK(s_sprite_calls==calls);
    puts("falcon_combat_apply_test: PASS"); return 0;
}
