#include "smw_falcon_combat_apply.h"
#include "falcon_locomotion.h"
#include <stdint.h>
#include <stdio.h>
#include <string.h>

static uint8_t s_ram[0x20000];
static int s_sprite_calls, s_spin_kill_calls, s_spin_star_calls;
static int s_spin_score_calls, s_dive_throw_calls, s_block_calls;

void cpu_write8(CpuState *cpu, uint8 bank, uint16 addr, uint8 value)
{
    if (cpu != NULL && cpu->ram != NULL && (bank == 0 || bank == 1))
        cpu->ram[addr] = value;
}

static int consume_native_frame(CpuState *cpu, uint8_t frame_size,
                                uint8_t expected_pb, const char *name)
{
    if (cpu->host_return_valid != frame_size || cpu->PB != expected_pb) {
        fprintf(stderr, "bad %s return frame: hrv=%u PB=%02x\n", name,
                (unsigned)cpu->host_return_valid, (unsigned)cpu->PB);
        return 0;
    }
    cpu->S = (uint16_t)(cpu->S + frame_size);
    return 1;
}

void SprStatus02_Dead_SetNorSprStatus04(CpuState *cpu)
{
    const unsigned slot = cpu->X & 0xffu;
    if (cpu->m_flag != 1 || cpu->x_flag != 1 || cpu->DB != 1 ||
        cpu->D != 0 || cpu->ram[0x15e9] != slot) {
        fprintf(stderr, "bad spin-kill contract\n"); return;
    }
    if (!consume_native_frame(cpu, 2, 1, "spin-kill")) return;
    ++s_sprite_calls; ++s_spin_kill_calls;
    /* Exact $01:9ACB native spin-jump state transition. */
    cpu->ram[0x14c8 + slot] = 4;
    cpu->ram[0x1540 + slot] = 31;
    cpu->A = 0xbeef; cpu->DB = 0xaa; cpu->ram[4] = 0xee;
}
void SpawnSpinJumpStars(CpuState *cpu)
{
    const unsigned slot = cpu->X & 0xffu;
    if (cpu->m_flag != 1 || cpu->x_flag != 1 || cpu->DB != 1 ||
        cpu->D != 0 || cpu->ram[0x15e9] != slot) {
        fprintf(stderr, "bad spin-star contract\n"); return;
    }
    if (!consume_native_frame(cpu, 3, 7, "spin-stars")) return;
    ++s_spin_star_calls;
    /* The source star spawner consumes $15E9, not a host-made particle. */
    cpu->ram[0x170b] = 16;
    cpu->ram[0x176f] = 23;
    cpu->A = 0xbeef; cpu->DB = 0xaa; cpu->ram[4] = 0xee;
}
void CheckPlayerToNormalSpriteColl_01AB46(CpuState *cpu)
{
    const unsigned slot = cpu->X & 0xffu;
    if (cpu->m_flag != 1 || cpu->x_flag != 1 || cpu->DB != 1 ||
        cpu->D != 0 || cpu->ram[0x15e9] != slot) {
        fprintf(stderr, "bad spin-score contract\n"); return;
    }
    if (!consume_native_frame(cpu, 2, 1, "spin-score")) return;
    ++s_spin_score_calls;
    ++cpu->ram[0x1697];
    ++cpu->ram[0x1dfc];
    cpu->A = 0xbeef; cpu->DB = 0xaa; cpu->ram[4] = 0xee;
}
void KillNormalSprite_AcceptedConsequence(CpuState *cpu)
{
    const unsigned slot = cpu->X & 0xffu;
    if (cpu->m_flag != 1 || cpu->x_flag != 1 || cpu->DB != 2 || cpu->D != 0) {
        fprintf(stderr, "bad Dive throw contract\n"); return;
    }
    if (!consume_native_frame(cpu, 2, 2, "Dive throw")) return;
    ++s_sprite_calls; ++s_dive_throw_calls;
    cpu->ram[0x14c8 + slot] = 2;
    ++cpu->ram[0x1dfc];
    cpu->A = 0xbeef; cpu->DB = 0xaa; cpu->ram[4] = 0xee;
}
void SpawnBounceSprite(CpuState *cpu)
{
    if (cpu->m_flag != 1 || cpu->x_flag != 1 || cpu->DB != 2 || cpu->D != 0) {
        fprintf(stderr, "bad block contract\n"); return;
    }
    if (!consume_native_frame(cpu, 3, 2, "block")) return;
    ++s_block_calls;
    cpu->ram[0x1dfc] = 7; cpu->ram[0x9c] = 2; cpu->ram[0x7d] = 0xd0;
    cpu->Y = 0xdead; cpu->ram[4] = 0xee;
}
static int failed(const char *x, int n) { fprintf(stderr,"FAIL %d: %s\n",n,x); return 1; }
#define CHECK(x) do { if (!(x)) return failed(#x, __LINE__); } while (0)
static void put16(unsigned p, uint16_t x) { s_ram[p]=(uint8_t)x; s_ram[p+1]=(uint8_t)(x>>8); }
static CpuState fresh(void) {
    CpuState c; memset(&c,0,sizeof(c)); memset(s_ram,0,sizeof(s_ram));
    c.ram=s_ram; c.m_flag=c.x_flag=1; c.P=0x30; c.S=0x01ff;
    s_sprite_calls=s_spin_kill_calls=s_spin_star_calls=s_spin_score_calls=0;
    s_dive_throw_calls=0;
    return c;
}
static ForeignAttackHitbox punch(void) {
    ForeignAttackHitbox a; memset(&a,0,sizeof(a)); a.active=1;
    a.offset_x=350; a.offset_y=100; a.width=700; a.height=650;
    a.flags=FOREIGN_ATTACK_BREAK_BLOCKS; return a;
}
static ForeignAttackHitbox kick(void) {
    ForeignAttackHitbox a; memset(&a,0,sizeof(a)); a.active=1;
    a.offset_x=336; a.offset_y=40; a.width=630; a.height=600;
    a.flags=FOREIGN_ATTACK_BREAK_BLOCKS; return a;
}
static ForeignAttackHitbox dive(void) {
    ForeignAttackHitbox a; memset(&a,0,sizeof(a)); a.active=1;
    a.offset_x=315; a.offset_y=260; a.width=470; a.height=300;
    a.flags=FOREIGN_ATTACK_CONTACT_ONLY; return a;
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
    /* `$01:80D2` reaches the adapter with bank $01 selected by its PHK/PLB
     * prologue. The native $02 route must preserve that exact mirror bank. */
    cpu.DB=1;

    /* save1's front pair is a status-$08 Koopa and a status-$09 loose shell,
     * both ID $05. Status $0A is the same admitted loose-shell lifecycle.
     * All three use the $01:9ACB -> $07:FC3B -> $01:AB46 spin-jump sequence;
     * only carried $0B is Falcon-owned and excluded. */
    install_sprite(8,8,0x05,148,126); install_sprite(9,9,0x05,156,126);
    install_sprite(10,10,0x05,164,126);
    install_sprite(7,11,0x04,188,126); begin(&ledger,FL_FALCON_PUNCH_GROUND);
    memset(s_ram,0x5a,16); memcpy(scratch,s_ram,16); memset(&hit,0,sizeof(hit));
    CHECK(smw_falcon_combat_apply(&cpu,&a,1,&ledger,&hit)==2);
    CHECK(s_sprite_calls==2 && s_spin_kill_calls==2 && s_spin_star_calls==2 &&
          s_spin_score_calls==2 && s_ram[0x14d0]==4 && s_ram[0x14d1]==4 &&
          s_ram[0x14d2]==10 && s_ram[0x1548]==31 && s_ram[0x1549]==31 &&
          s_ram[0x154a]==0 && s_ram[0x170b]==16 && s_ram[0x176f]==23 &&
          s_ram[0x14cf]==11 &&
          hit.attack_connected && ledger.hit_slots==((1u<<8)|(1u<<9)) &&
          ledger.new_hit_slots==((1u<<8)|(1u<<9)));
    CHECK(memcmp(s_ram,scratch,16)==0 && s_ram[0x15e9]==0 &&
          s_ram[0x1697]==2 && s_ram[0x1df9]==8 && cpu.DB==1 && cpu.A==0);

    /* Linger frames never replay native score/SFX/contact on the same shell. */
    calls=s_sprite_calls; memset(&hit,0,sizeof(hit));
    CHECK(smw_falcon_combat_apply(&cpu,&a,1,&ledger,&hit)==0 &&
          s_sprite_calls==calls && !hit.attack_connected &&
          ledger.new_hit_slots==0);

    /* An aerial Punch that lands during its active window keeps one source
     * move identity. The air->ground continuation must not replay the native
     * spin-kill transaction on its already-resolved slot. */
    cpu=fresh(); a=punch(); memset(&ledger,0,sizeof(ledger)); put16(0x94,100); put16(0x96,100);
    install_sprite(4,8,0x0f,150,120);
    begin(&ledger,FL_FALCON_PUNCH_AIR); CHECK(smw_falcon_combat_apply(&cpu,&a,1,&ledger,&hit)==1);
    calls=s_sprite_calls; begin(&ledger,FL_FALCON_PUNCH_GROUND);
    CHECK(smw_falcon_combat_apply(&cpu,&a,1,&ledger,&hit)==0 && s_sprite_calls==calls);

    /* Active Kick's rendered foot reaches a line of regular enemies. Its
     * status-$04 native transition proves bookkeeping—not a status-$08
     * assumption—prevents repeated contacts. The target behind Falcon is
     * untouched and therefore stays dangerous. */
    cpu=fresh(); a=kick(); memset(&ledger,0,sizeof(ledger)); put16(0x94,100); put16(0x96,100);
    install_sprite(1,8,0x0f,148,120); install_sprite(3,8,0x0f,156,120);
    install_sprite(6,8,0x0f,80,120);
    begin(&ledger,FL_FALCON_KICK_GROUND); memset(&hit,0,sizeof(hit));
    CHECK(smw_falcon_combat_apply(&cpu,&a,1,&ledger,&hit)==2 && hit.attack_connected);
    CHECK(s_ram[0x14c9]==4 && s_ram[0x14cb]==4 && s_ram[0x14ce]==8);
    calls=s_sprite_calls; CHECK(smw_falcon_combat_apply(&cpu,&a,1,&ledger,&hit)==0 &&
          s_sprite_calls==calls);

    /* Falcon Dive's hitbox is a one-target catch search.  BattleShip keeps
     * search_gobj as catch_gobj through Catch; there is no damage/status
     * transaction until FalconDiveEnd1 begins Throw.  A loose shell is not a
     * fighter capture, and a second ordinary target stays untouched. */
    cpu=fresh(); a=dive(); memset(&ledger,0,sizeof(ledger));
    put16(0x94,100); put16(0x96,100);
    install_sprite(2,9,0x05,126,104); /* rejected loose shell */
    install_sprite(4,8,0x0f,142,104); install_sprite(6,8,0x0f,146,104);
    begin(&ledger,FL_FALCON_DIVE_AIR); memset(&hit,0,sizeof(hit));
    calls=s_sprite_calls;
    CHECK(smw_falcon_combat_apply(&cpu,&a,1,&ledger,&hit)==1 &&
          hit.attack_connected && s_sprite_calls==calls &&
          ledger.dive_latched_slot==4 && ledger.dive_latched_id==0x0f &&
          ledger.new_hit_slots==(1u<<4) && s_ram[0x14cc]==8 &&
          s_ram[0x14ce]==8);
    /* Repeated catch frames cannot reselect the second enemy or invoke a
     * native impact while Falcon is holding the captured identity. */
    memset(&hit,0,sizeof(hit));
    CHECK(smw_falcon_combat_apply(&cpu,&a,1,&ledger,&hit)==0 &&
          !hit.attack_connected && s_sprite_calls==calls);
    smw_falcon_combat_ledger_update(&ledger,FL_FALCON_DIVE_CATCH,0);
    CHECK(ledger.dive_latched_slot==4 && ledger.active);
    smw_falcon_combat_ledger_update(&ledger,FL_FALCON_DIVE_THROW,0);
    memset(&hit,0,sizeof(hit));
    CHECK(smw_falcon_combat_release_dive(&cpu,&ledger,&hit)==1 &&
          hit.attack_connected && s_sprite_calls==calls+1 &&
          s_dive_throw_calls==1 && s_ram[0x14cc]==2 && s_ram[0x14ce]==8 &&
          ledger.dive_latched_slot==-1);
    CHECK(smw_falcon_combat_release_dive(&cpu,&ledger,&hit)==0 &&
          s_sprite_calls==calls+1);

    /* If the captured slot has died or been reused while Catch plays, Throw
     * releases nothing: source GObj identity must not strike a replacement. */
    cpu=fresh(); a=dive(); memset(&ledger,0,sizeof(ledger));
    put16(0x94,100); put16(0x96,100); install_sprite(5,8,0x0f,142,104);
    begin(&ledger,FL_FALCON_DIVE_GROUND); memset(&hit,0,sizeof(hit));
    CHECK(smw_falcon_combat_apply(&cpu,&a,1,&ledger,&hit)==1 &&
          ledger.dive_latched_slot==5);
    s_ram[0x9e + 5]=0x35; calls=s_sprite_calls;
    smw_falcon_combat_ledger_update(&ledger,FL_FALCON_DIVE_CATCH,0);
    smw_falcon_combat_ledger_update(&ledger,FL_FALCON_DIVE_THROW,0);
    memset(&hit,0,sizeof(hit));
    CHECK(smw_falcon_combat_release_dive(&cpu,&ledger,&hit)==0 &&
          !hit.attack_connected && s_sprite_calls==calls && s_ram[0x14cd]==8 &&
          ledger.dive_latched_slot==-1);

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
