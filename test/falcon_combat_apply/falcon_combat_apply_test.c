#include "smw_falcon_combat_apply.h"
#include "falcon_locomotion.h"
#include <stdint.h>
#include <stdio.h>
#include <string.h>

static uint8_t s_ram[0x20000];
static int s_sprite_calls, s_spin_kill_calls, s_spin_star_calls;
static int s_spin_score_calls, s_star_kill_calls, s_block_calls;
static int s_bounce_block_calls, s_brick_piece_calls, s_map16_lookup_calls;

typedef struct MockMap16Tile {
    uint16_t x;
    uint16_t y;
    uint8_t low;
} MockMap16Tile;

static MockMap16Tile s_map16_tiles[64];
static int s_map16_tile_count;

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
        fprintf(stderr, "bad star-kill contract\n"); return;
    }
    if (!consume_native_frame(cpu, 2, 2, "star kill")) return;
    ++s_sprite_calls; ++s_star_kill_calls;
    cpu->ram[0x14c8 + slot] = 2;
    ++cpu->ram[0x1dfc];
    cpu->A = 0xbeef; cpu->DB = 0xaa; cpu->ram[4] = 0xee;
}
void SpawnBounceSprite(CpuState *cpu)
{
    (void)cpu;
    ++s_bounce_block_calls;
    fprintf(stderr, "unexpected bounce block activation\n");
}
void SpawnBrickPieces(CpuState *cpu)
{
    if (cpu->m_flag != 1 || cpu->x_flag != 1 || cpu->DB != 2 ||
        cpu->D != 0 || (cpu->A & 0xffu) != 0) {
        fprintf(stderr, "bad brick piece contract\n"); return;
    }
    if (!consume_native_frame(cpu, 3, 2, "brick pieces")) return;
    ++s_brick_piece_calls;
    /* Native debris consumes $98/$9A block coordinates and writes minor
     * extended sprites/SFX only; it must not activate block contents. */
    cpu->ram[0x17f0 + (s_brick_piece_calls & 7)] = 1;
    cpu->ram[0x1dfc] = 7;
    cpu->A = 0xbeef; cpu->DB = 0xaa; cpu->ram[4] = 0xee;
}
static uint16_t read16(unsigned p)
{
    return (uint16_t)(s_ram[p] | ((uint16_t)s_ram[p + 1] << 8));
}
static void mock_map16_clear(void)
{
    memset(s_map16_tiles, 0, sizeof(s_map16_tiles));
    s_map16_tile_count = 0;
}
static void mock_map16_set(uint16_t x, uint16_t y, uint8_t low)
{
    if (s_map16_tile_count >=
        (int)(sizeof(s_map16_tiles) / sizeof(s_map16_tiles[0]))) {
        fprintf(stderr, "mock map16 overflow\n");
        return;
    }
    s_map16_tiles[s_map16_tile_count].x = x;
    s_map16_tiles[s_map16_tile_count].y = y;
    s_map16_tiles[s_map16_tile_count].low = low;
    ++s_map16_tile_count;
}
static uint8_t mock_map16_get(uint16_t x, uint16_t y)
{
    int i;
    for (i = 0; i < s_map16_tile_count; ++i)
        if (s_map16_tiles[i].x == x && s_map16_tiles[i].y == y)
            return s_map16_tiles[i].low;
    return 0;
}
static void mock_map16_delete(uint16_t x, uint16_t y)
{
    int i;
    for (i = 0; i < s_map16_tile_count; ++i) {
        if (s_map16_tiles[i].x == x && s_map16_tiles[i].y == y) {
            s_map16_tiles[i] = s_map16_tiles[s_map16_tile_count - 1];
            --s_map16_tile_count;
            return;
        }
    }
}
void GetPlayerLevelCollisionMap16ID_Entry2(CpuState *cpu)
{
    if (cpu->m_flag != 1 || cpu->x_flag != 1 || cpu->DB != 0 ||
        cpu->D != 0) {
        fprintf(stderr, "bad map16 lookup contract\n"); return;
    }
    if (!consume_native_frame(cpu, 2, 0, "map16 lookup")) return;
    ++s_map16_lookup_calls;
    cpu->ram[0x1693] = mock_map16_get(read16(0x9a), read16(0x98));
    cpu->A = 0xbeef; cpu->DB = 0xaa; cpu->ram[4] = 0xee;
}
void GenerateTile(CpuState *cpu)
{
    if (cpu->m_flag != 1 || cpu->x_flag != 1 || cpu->DB != 0 ||
        cpu->D != 0 || cpu->ram[0x9c] != 1) {
        fprintf(stderr, "bad clean block contract\n"); return;
    }
    if (!consume_native_frame(cpu, 3, 0, "clean block")) return;
    ++s_block_calls;
    mock_map16_delete(read16(0x9a), read16(0x98));
    /* Command 1 is the blank-tile path; it should not run content/bounce
     * behavior or alter Mario's Y speed. */
    cpu->ram[0x1693] = 0;
    cpu->ram[0x1dfc] = 7;
    cpu->ram[0x7d] = 0x33;
    cpu->ram[4] = 0xee;
}
static int failed(const char *x, int n) { fprintf(stderr,"FAIL %d: %s\n",n,x); return 1; }
#define CHECK(x) do { if (!(x)) return failed(#x, __LINE__); } while (0)
static void put16(unsigned p, uint16_t x) { s_ram[p]=(uint8_t)x; s_ram[p+1]=(uint8_t)(x>>8); }
static CpuState fresh(void) {
    CpuState c; memset(&c,0,sizeof(c)); memset(s_ram,0,sizeof(s_ram));
    c.ram=s_ram; c.m_flag=c.x_flag=1; c.P=0x30; c.S=0x01ff;
    s_sprite_calls=s_spin_kill_calls=s_spin_star_calls=s_spin_score_calls=0;
    s_star_kill_calls=s_block_calls=s_bounce_block_calls=0;
    s_brick_piece_calls=0;
    s_map16_lookup_calls=0; mock_map16_clear();
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
    a.offset_x=350; a.offset_y=100; a.width=700; a.height=650;
    a.flags=FOREIGN_ATTACK_CONTACT_ONLY; return a;
}
static void install_sprite(unsigned slot, uint8_t status, uint8_t id,
                           uint16_t x, uint16_t y) {
    s_ram[0x14c8 + slot]=status; s_ram[0x9e + slot]=id;
    s_ram[0xe4 + slot]=(uint8_t)x; s_ram[0x14e0 + slot]=(uint8_t)(x>>8);
    s_ram[0xd8 + slot]=(uint8_t)y; s_ram[0x14d4 + slot]=(uint8_t)(y>>8);
}
static void install_big_target(unsigned slot, uint8_t id, uint16_t x,
                               uint16_t y, uint8_t clip, uint8_t tweaker_c,
                               uint8_t tweaker_d) {
    install_sprite(slot, 8, id, x, y);
    s_ram[0x1662 + slot] = clip;
    s_ram[0x166e + slot] = tweaker_c;
    s_ram[0x167a + slot] = tweaker_d;
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

    /* Oracle vanilla source row $9F is $1662/$166E/$167A = $B6/$31/$01.
     * $03:B69F masks B with $3F, and $03:B56C/B5A8/B5E4/B620 index $36 is
     * [x+8,x+60) x [y+8,y+54). At x=156 the Punch right edge is exactly 164
     * and must not touch; moving one pixel left must use the framed spin
     * transaction. This is the $02:D587 -> $01:A7DC interaction body, not
     * Banzai's larger drawn OAM body. */
    cpu=fresh(); a=punch(); memset(&ledger,0,sizeof(ledger));
    put16(0x94,100); put16(0x96,100);
    install_big_target(5,0x9f,156,120,0xb6,0x31,0x01);
    begin(&ledger,FL_FALCON_PUNCH_GROUND); memset(&hit,0,sizeof(hit));
    CHECK(smw_falcon_combat_apply(&cpu,&a,1,&ledger,&hit)==0 &&
          s_sprite_calls==0 && s_ram[0x14cd]==8);
    s_ram[0x00e4 + 5]=155;
    CHECK(smw_falcon_combat_apply(&cpu,&a,1,&ledger,&hit)==1 &&
          s_spin_kill_calls==1 && s_spin_star_calls==1 &&
          s_spin_score_calls==1 && s_star_kill_calls==0 &&
          s_ram[0x14cd]==4 && ledger.new_hit_slots==(1u<<5));

    /* Kick shares the same oracle `$B6 & $3F == $36` Banzai interaction
     * body. Its narrower source hitbox reaches the 52x46 native body at
     * x=151; it does not substitute a guessed drawn-tile union. */
    cpu=fresh(); a=kick(); memset(&ledger,0,sizeof(ledger));
    put16(0x94,100); put16(0x96,100);
    install_big_target(3,0x9f,151,120,0xb6,0x31,0x01);
    begin(&ledger,FL_FALCON_KICK_GROUND); memset(&hit,0,sizeof(hit));
    CHECK(smw_falcon_combat_apply(&cpu,&a,1,&ledger,&hit)==1 &&
          s_spin_kill_calls==1 && s_spin_star_calls==1 &&
          s_spin_score_calls==1 && s_ram[0x14cb]==4 &&
          ledger.new_hit_slots==(1u<<3));

    /* The high bits in vanilla Banzai's `$1662 == $B6` are signature data,
     * not disposable geometry noise. `$36/$31/$01` has the same masked clip
     * index but is a non-vanilla property row and must not enter the special
     * native route. */
    cpu=fresh(); a=punch(); memset(&ledger,0,sizeof(ledger));
    put16(0x94,100); put16(0x96,100);
    install_big_target(3,0x9f,155,120,0x36,0x31,0x01);
    begin(&ledger,FL_FALCON_PUNCH_GROUND); memset(&hit,0,sizeof(hit));
    CHECK(smw_falcon_combat_apply(&cpu,&a,1,&ledger,&hit)==0 &&
          s_sprite_calls==0 && s_ram[0x14cb]==8 && ledger.new_hit_slots==0);

    /* Oracle vanilla source row $91 is $1662/$166E/$167A = $0D/$0B/$F9.
     * Chargin' Chuck's source clip index $0D is [x,x+15) x [y-4,y+12).
     * Its custom $02:C79D interaction is never re-entered after host
     * geometry; user-selected one-hit behavior is the framed $02:C7B1
     * post-star defeat, preserving the live $15E9 selector. */
    cpu=fresh(); a=punch(); memset(&ledger,0,sizeof(ledger));
    put16(0x94,100); put16(0x96,100); s_ram[0x15e9]=0xa5;
    install_big_target(6,0x91,148,124,0x0d,0x0b,0xf9);
    begin(&ledger,FL_FALCON_PUNCH_GROUND); memset(&hit,0,sizeof(hit));
    CHECK(smw_falcon_combat_apply(&cpu,&a,1,&ledger,&hit)==1 &&
          s_spin_kill_calls==0 && s_spin_star_calls==0 &&
          s_spin_score_calls==0 && s_star_kill_calls==1 &&
          s_ram[0x14ce]==2 && s_ram[0x15e9]==0xa5 &&
          ledger.new_hit_slots==(1u<<6));

    /* A ROM-hack variant with one mismatched source tweaker remains outside
     * this deliberately narrow combat tranche. */
    cpu=fresh(); a=punch(); memset(&ledger,0,sizeof(ledger));
    put16(0x94,100); put16(0x96,100);
    install_big_target(6,0x91,148,124,0x0d,0x0a,0xf9);
    begin(&ledger,FL_FALCON_PUNCH_GROUND); memset(&hit,0,sizeof(hit));
    CHECK(smw_falcon_combat_apply(&cpu,&a,1,&ledger,&hit)==0 &&
          s_sprite_calls==0 && s_ram[0x14ce]==8 && ledger.new_hit_slots==0);

    /* Rex ($1F) is a special case for user-facing combat: native stomp-style
     * interaction can leave it squished/compressed, but Falcon Punch/Kick are
     * meant to be decisive Smash hits.  Admit only the vanilla oracle row
     * $1662/$166E/$167A = $81/$4F/$02, use source clip index $01
     * [x+2,x+14) x [y+3,y+24), and route to the guaranteed star-kill
     * endpoint rather than Rex's ordinary squish lifecycle. */
    cpu=fresh(); a=punch(); memset(&ledger,0,sizeof(ledger));
    put16(0x94,100); put16(0x96,100);
    install_big_target(7,0x1f,162,120,0x81,0x4f,0x02);
    begin(&ledger,FL_FALCON_PUNCH_GROUND); memset(&hit,0,sizeof(hit));
    CHECK(smw_falcon_combat_apply(&cpu,&a,1,&ledger,&hit)==0 &&
          s_sprite_calls==0 && s_ram[0x14cf]==8);
    s_ram[0x00e4 + 7]=150;
    CHECK(smw_falcon_combat_apply(&cpu,&a,1,&ledger,&hit)==1 &&
          s_star_kill_calls==1 && s_spin_kill_calls==0 &&
          s_ram[0x14cf]==2 && ledger.new_hit_slots==(1u<<7));

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
    {
        int dx = 0, dy = 0;
        /* Source CaptureCaptain permits 180 source units (14.4 host px),
         * while SMW takes only a 4px pre-physics convergence step. */
        CHECK(smw_falcon_combat_dive_snap_delta(&cpu,&ledger,&dx,&dy) &&
              dx == 4 && dy == 0);
        s_ram[0x9e + 4] = 0x35;
        CHECK(!smw_falcon_combat_dive_snap_delta(&cpu,&ledger,&dx,&dy) &&
              dx == 0 && dy == 0);
        s_ram[0x9e + 4] = 0x0f;
    }
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
          s_star_kill_calls==1 && s_ram[0x14cc]==2 && s_ram[0x14ce]==8 &&
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
    s_ram[0x7c]=0xaa; s_ram[0x7d]=0xbb;
    calls=s_block_calls; CHECK(smw_falcon_combat_apply(&cpu,&a,1,&ledger,&(ForeignCollisionResult){0})==1);
    CHECK(s_block_calls==calls+1 && s_bounce_block_calls==0 &&
          s_brick_piece_calls==1 &&
          s_ram[0x1693]==0 && s_ram[0x9c]==0 &&
          s_ram[0x7c]==0xaa && s_ram[0x7d]==0xbb);
    /* Content-like turn blocks must use the same clean blank-tile path, not
     * the native bounce/content activation route that can spawn items/enemies. */
    cpu=fresh(); a=kick(); memset(&ledger,0,sizeof(ledger)); put16(0x94,100); put16(0x96,100);
    put16(0x9a,128); put16(0x98,112); s_ram[0x1693]=0x1e;
    begin(&ledger,FL_FALCON_KICK_GROUND);
    CHECK(smw_falcon_combat_apply(&cpu,&a,1,&ledger,&(ForeignCollisionResult){0})==1 &&
          s_block_calls==1 && s_brick_piece_calls==1 &&
          s_bounce_block_calls==0 &&
          s_ram[0x1693]==0 && s_ram[0x9c]==0);

    /* Falcon specials break the whole authored volume, not only SMW's single
     * touched block.  A grounded Kick carves a horizontal/vertical contact
     * strip across every overlapped turn block and leaves non-turn Map16
     * tiles alone. */
    cpu=fresh(); a=kick(); memset(&ledger,0,sizeof(ledger)); put16(0x94,100); put16(0x96,100);
    mock_map16_set(112,112,0x1e); mock_map16_set(128,112,0x1e);
    mock_map16_set(144,112,0x1e); mock_map16_set(160,112,0x1e);
    mock_map16_set(128,128,0x30); /* not a Falcon-breakable block class */
    begin(&ledger,FL_FALCON_KICK_GROUND);
    CHECK(smw_falcon_combat_apply(&cpu,&a,1,&ledger,&(ForeignCollisionResult){0})==4 &&
          s_block_calls==4 && s_brick_piece_calls==4 &&
          s_bounce_block_calls==0 &&
          mock_map16_get(112,112)==0 && mock_map16_get(128,112)==0 &&
          mock_map16_get(144,112)==0 && mock_map16_get(160,112)==0 &&
          mock_map16_get(128,128)==0x30 && s_map16_lookup_calls > 4);

    /* Because a Falcon Punch/Kick lingers, each active frame may destroy a
     * newly-overlapped set of blocks.  The ledger records that a block hit
     * occurred but must not suppress later block-volume scans. */
    mock_map16_set(112,128,0x1e); mock_map16_set(128,128,0x1e);
    CHECK(smw_falcon_combat_apply(&cpu,&a,1,&ledger,&(ForeignCollisionResult){0})==2 &&
          s_block_calls==6 && s_brick_piece_calls==6 && ledger.block_applied &&
          mock_map16_get(112,128)==0 && mock_map16_get(128,128)==0);

    /* Falcon Punch has a wider authored range and should blank several blocks
     * in front of him in one frame. */
    cpu=fresh(); a=punch(); memset(&ledger,0,sizeof(ledger)); put16(0x94,100); put16(0x96,100);
    mock_map16_set(112,96,0x1e); mock_map16_set(128,96,0x1e);
    mock_map16_set(144,96,0x1e); mock_map16_set(112,112,0x1e);
    mock_map16_set(128,112,0x1e); mock_map16_set(144,112,0x1e);
    begin(&ledger,FL_FALCON_PUNCH_GROUND);
    CHECK(smw_falcon_combat_apply(&cpu,&a,1,&ledger,&(ForeignCollisionResult){0})==6 &&
          s_block_calls==6 && s_brick_piece_calls==6 && s_bounce_block_calls==0);

    /* Standing on a destructible block must not collapse the whole special
     * into only the native foot collision.  Falcon may break the block below
     * him, but Punch also needs to sweep the lower forward row. */
    cpu=fresh(); a=punch(); memset(&ledger,0,sizeof(ledger)); put16(0x94,100); put16(0x96,96);
    mock_map16_set(96,160,0x1e);   /* under/near Falcon's feet */
    mock_map16_set(112,160,0x1e);  /* forward floor row */
    mock_map16_set(128,160,0x1e);
    mock_map16_set(144,160,0x1e);
    mock_map16_set(160,160,0x1e);
    mock_map16_set(176,160,0x1e);
    begin(&ledger,FL_FALCON_PUNCH_GROUND);
    CHECK(smw_falcon_combat_apply(&cpu,&a,1,&ledger,&(ForeignCollisionResult){0})==6 &&
          s_block_calls==6 && s_brick_piece_calls==6 &&
          mock_map16_get(96,160)==0 && mock_map16_get(112,160)==0 &&
          mock_map16_get(128,160)==0 && mock_map16_get(144,160)==0 &&
          mock_map16_get(160,160)==0 && mock_map16_get(176,160)==0);

    /* Grounded Falcon Kick gets the same clean platformer row treatment with
     * a longer block-only sweep so he does not get caught after one tile. */
    cpu=fresh(); a=kick(); memset(&ledger,0,sizeof(ledger)); put16(0x94,100); put16(0x96,96);
    mock_map16_set(96,160,0x1e); mock_map16_set(112,160,0x1e);
    mock_map16_set(128,160,0x1e); mock_map16_set(144,160,0x1e);
    mock_map16_set(160,160,0x1e); mock_map16_set(176,160,0x1e);
    mock_map16_set(192,160,0x1e); mock_map16_set(208,160,0x1e);
    begin(&ledger,FL_FALCON_KICK_GROUND);
    CHECK(smw_falcon_combat_apply(&cpu,&a,1,&ledger,&(ForeignCollisionResult){0})==8 &&
          s_block_calls==8 && s_brick_piece_calls==8 &&
          mock_map16_get(96,160)==0 && mock_map16_get(112,160)==0 &&
          mock_map16_get(128,160)==0 && mock_map16_get(144,160)==0 &&
          mock_map16_get(160,160)==0 && mock_map16_get(176,160)==0 &&
          mock_map16_get(192,160)==0 && mock_map16_get(208,160)==0);

    /* Direct aerial Falcon Kick gets a block-only down-forward crater volume.
     * The compact sprite hitbox remains unchanged, but yellow blocks beneath
     * a short hop are still destroyed. */
    cpu=fresh(); a=kick(); memset(&ledger,0,sizeof(ledger)); put16(0x94,100); put16(0x96,100);
    mock_map16_set(128,144,0x1e); mock_map16_set(144,160,0x1e);
    mock_map16_set(160,176,0x1e);
    begin(&ledger,FL_FALCON_KICK_AIR);
    CHECK(smw_falcon_combat_apply(&cpu,&a,1,&ledger,&(ForeignCollisionResult){0})==3 &&
          s_block_calls==3 && s_brick_piece_calls==3 &&
          mock_map16_get(128,144)==0 && mock_map16_get(144,160)==0 &&
          mock_map16_get(160,176)==0);

    smw_falcon_combat_ledger_update(&ledger,FL_FALCON_PUNCH_GROUND,0);
    CHECK(!ledger.active && ledger.hit_slots==0);
    puts("falcon_combat_apply_test: PASS"); return 0;
}
