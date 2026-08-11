#include "smw_falcon_combat_apply.h"
#include "falcon_locomotion.h"
#include <stdint.h>
#include <stdio.h>
#include <string.h>

static uint8_t s_ram[0x20000];
static int s_sprite_calls, s_block_calls, s_native_contact_effects;
static uint16_t s_keep_status08;

void CheckPlayerAttackToNormalSpriteColl_029404(CpuState *cpu)
{
    const unsigned slot = cpu->X & 0xffu;
    if (cpu->m_flag != 1 || cpu->x_flag != 1 || cpu->DB != 2 ||
        cpu->D != 0 || cpu->ram[0x0e]) {
        fprintf(stderr, "bad sprite contract\n"); return;
    }
    ++s_sprite_calls;
    ++s_native_contact_effects; /* native contact SFX/score route */
    ++cpu->ram[0x1dfc]; /* persistent native SFX consequence for status-$08 */
    if ((s_keep_status08 & (uint16_t)(1u << slot)) == 0)
        cpu->ram[0x14c8 + slot] = 2;
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
static int failed(const char *x, int n) { fprintf(stderr,"FAIL %d: %s\n",n,x); return 1; }
#define CHECK(x) do { if (!(x)) return failed(#x, __LINE__); } while (0)
static void put16(unsigned p, uint16_t x) { s_ram[p]=(uint8_t)x; s_ram[p+1]=(uint8_t)(x>>8); }
static CpuState fresh(void) {
    CpuState c; memset(&c,0,sizeof(c)); memset(s_ram,0,sizeof(s_ram));
    c.ram=s_ram; c.m_flag=c.x_flag=1; c.P=0x30; s_keep_status08=0; return c;
}
static ForeignAttackHitbox punch(void) {
    ForeignAttackHitbox a; memset(&a,0,sizeof(a)); a.active=1;
    a.offset_x=700; a.offset_y=100; a.width=1400; a.height=650;
    a.flags=FOREIGN_ATTACK_BREAK_BLOCKS; return a;
}
static ForeignAttackHitbox kick(void) {
    ForeignAttackHitbox a; memset(&a,0,sizeof(a)); a.active=1;
    a.offset_x=480; a.offset_y=40; a.width=900; a.height=600;
    a.flags=FOREIGN_ATTACK_BREAK_BLOCKS; return a;
}
static void install_sprite(unsigned slot, uint8_t status, uint8_t id,
                           uint16_t x, uint16_t y) {
    s_ram[0x14c8 + slot]=status; s_ram[0x9e + slot]=id;
    s_ram[0xe4 + slot]=(uint8_t)x; s_ram[0x14e0 + slot]=(uint8_t)(x>>8);
    s_ram[0xd8 + slot]=(uint8_t)y; s_ram[0x14d4 + slot]=(uint8_t)(y>>8);
}
static void begin(SmwFalconCombatLedger *ledger, int state) {
    smw_falcon_combat_ledger_update(ledger,state,1);
}

int main(void) {
    CpuState cpu=fresh(); ForeignAttackHitbox a=punch(); ForeignCollisionResult hit;
    SmwFalconCombatLedger ledger; int calls; uint8_t scratch[16];
    memset(&ledger,0,sizeof(ledger)); put16(0x94,100); put16(0x96,100);

    /* Punch reaches two loose shells directly ahead. Their native $09/$0A
     * lifecycle is admitted, but carried $0B remains Falcon-owned. */
    install_sprite(2,9,0x04,184,126); install_sprite(5,10,0x05,202,126);
    install_sprite(7,11,0x04,188,126); begin(&ledger,FL_FALCON_PUNCH_GROUND);
    memset(s_ram,0x5a,16); memcpy(scratch,s_ram,16); memset(&hit,0,sizeof(hit));
    CHECK(smw_falcon_combat_apply(&cpu,&a,1,&ledger,&hit)==2);
    CHECK(s_sprite_calls==2 && s_native_contact_effects==2 &&
          s_ram[0x14ca]==2 && s_ram[0x14cd]==2 && s_ram[0x14cf]==11 &&
          hit.attack_connected && ledger.hit_slots==((1u<<2)|(1u<<5)));
    CHECK(memcmp(s_ram,scratch,16)==0 && cpu.DB==0 && cpu.A==0);

    /* Linger frames never replay native score/SFX/contact on the same shell. */
    calls=s_sprite_calls; memset(&hit,0,sizeof(hit));
    CHECK(smw_falcon_combat_apply(&cpu,&a,1,&ledger,&hit)==0 &&
          s_sprite_calls==calls && !hit.attack_connected);

    /* An aerial Punch that lands during its active window keeps one source
     * move identity. The air->ground continuation must not re-hit a native
     * multi-hit target that intentionally remains status $08. */
    cpu=fresh(); a=punch(); memset(&ledger,0,sizeof(ledger)); put16(0x94,100); put16(0x96,100);
    install_sprite(4,8,0x0f,184,120); s_keep_status08=(uint16_t)(1u<<4);
    begin(&ledger,FL_FALCON_PUNCH_AIR); CHECK(smw_falcon_combat_apply(&cpu,&a,1,&ledger,&hit)==1);
    calls=s_sprite_calls; begin(&ledger,FL_FALCON_PUNCH_GROUND);
    CHECK(smw_falcon_combat_apply(&cpu,&a,1,&ledger,&hit)==0 && s_sprite_calls==calls);

    /* Active Kick's rendered foot reaches a line of regular enemies. Slot 3
     * intentionally remains native status $08 (multi-hit family), proving
     * bookkeeping rather than a status mutation prevents repeated contacts.
     * The target behind Falcon is untouched and therefore stays dangerous. */
    cpu=fresh(); a=kick(); memset(&ledger,0,sizeof(ledger)); put16(0x94,100); put16(0x96,100);
    install_sprite(1,8,0x0f,170,120); install_sprite(3,8,0x0f,178,120);
    install_sprite(6,8,0x0f,80,120); s_keep_status08=(uint16_t)(1u<<3);
    begin(&ledger,FL_FALCON_KICK_GROUND); memset(&hit,0,sizeof(hit));
    CHECK(smw_falcon_combat_apply(&cpu,&a,1,&ledger,&hit)==2 && hit.attack_connected);
    CHECK(s_ram[0x14c9]==2 && s_ram[0x14cb]==8 && s_ram[0x14ce]==8);
    calls=s_sprite_calls; CHECK(smw_falcon_combat_apply(&cpu,&a,1,&ledger,&hit)==0 &&
          s_sprite_calls==calls);

    /* A new move gets a fresh ledger, but its native block transaction still
     * runs only when this move has not touched a sprite. */
    cpu=fresh(); a=punch(); memset(&ledger,0,sizeof(ledger)); put16(0x94,100); put16(0x96,100);
    put16(0x9a,128); put16(0x98,112); s_ram[4]=7; begin(&ledger,FL_FALCON_PUNCH_GROUND);
    calls=s_block_calls; CHECK(smw_falcon_combat_apply(&cpu,&a,1,&ledger,&(ForeignCollisionResult){0})==1);
    CHECK(s_block_calls==calls+1);
    smw_falcon_combat_ledger_update(&ledger,FL_FALCON_PUNCH_GROUND,0);
    CHECK(!ledger.active && ledger.hit_slots==0);
    puts("falcon_combat_apply_test: PASS"); return 0;
}
