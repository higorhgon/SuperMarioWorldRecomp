/* Focused host-seam test: raw pad mapping, sign conversion, and native
 * movement/collision feedback without generated SMW sources. */
#include "../../overrides/falcon/falcon_smw_adapter.h"
#include "../../src/mods/falcon/captain_falcon_foreign.h"
#include "../../src/mods/falcon/falcon_locomotion.h"
#include "types.h"
#include "../../src/variables.h"

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

uint8 g_ram[0x20000];
int snes_frame_counter;
static int s_audio_dispatches;

/* The adapter harness deliberately has no generated game bodies. These are
 * the two validated native aliases exercised in detail by falcon_combat_apply
 * unit tests; adapter calls here use a null CpuState until a full CPU fixture. */
void CheckPlayerAttackToNormalSpriteColl_029404(CpuState *cpu) { (void)cpu; }
void SpawnBounceSprite(CpuState *cpu) { (void)cpu; }

void smw_falcon_audio_play_events(const ForeignAudioEvents *events)
{
    (void)events;
    ++s_audio_dispatches;
}

static int fail(const char *message)
{
    fprintf(stderr, "FAIL: %s\n", message);
    return 1;
}

int main(void)
{
    ForeignTraceEntry trace;
    int count;

    if (!smw_captain_falcon_register() ||
        !snes_foreign_select(SMW_CAPTAIN_FALCON_ID))
        return fail("register/select Captain Falcon");
    memset(g_ram, 0, sizeof(g_ram));
    misc_game_mode = 0x14;
    player_current_state = 0;
    player_xpos = 100;
    player_ypos = 200;
    player_in_air_flag = 0;
    /* $15=%byetUDLR, $17=%axlr0000. B/Y/X are Falcon-owned; A is reserved
     * rather than accidentally becoming SMW's native spin-jump. */
    io_controller_hold1 = 0xF1;  /* B,Y,Select,Start,Right */
    io_controller_press1 = 0x01; /* Right */
    io_controller_hold2 = 0xC0;  /* A,X */
    io_controller_press2 = 0x80; /* A */
    snes_frame_counter = 1000;

    snes_foreign_set_ownership(FOREIGN_OWNERSHIP_SCRIPTED);
    /* Model the actual hook order: $D5F2 captures/masks before native action
     * branches, and only then $DC2D accepts Falcon's velocity. */
    SmwFalconBeforePlayerPhysics(NULL);
    if (snes_foreign_ownership() != FOREIGN_OWNERSHIP_SCRIPTED)
        return fail("early hook masks the initial scripted handoff frame");
    if (io_controller_hold1 != 0x30 || io_controller_hold2 != 0 ||
        io_controller_press1 != 0 || io_controller_press2 != 0)
        return fail("B/Y/directions/A/X cleared but Start/Select preserved");
    SmwFalconBeforePhysics(NULL);
    if (snes_foreign_ownership() != FOREIGN_OWNERSHIP_FOREIGN)
        return fail("ordinary level grants foreign ownership after the early mask");
    if (s_audio_dispatches != 1)
        return fail("controller audio is dispatched exactly once after its tick");
    count = snes_foreign_trace_last(1, &trace);
    if (count != 1 || trace.frame != 1000 || trace.stick_x != 1.0f ||
        trace.stick_y != 0.0f || trace.raw_buttons != 0xC0F1)
        return fail("raw WRAM pad mapping and monotonic frame trace");
    if (smw_falcon_last_attack()->active)
        return fail("reserved A does not become a Falcon jump or attack");

    /* Emulate native position integration and level collision between seams. */
    player_xpos = (uint16)(player_xpos + 3);
    player_ypos = (uint16)(player_ypos + 2);
    player_blocked_flags = 0x0B; /* wall plus ceiling; grounded yields floor */
    SmwFalconAfterPhysics(NULL);
    if (snes_foreign_trace_last(1, &trace) != 1 ||
        fabs(trace.resolved_dx - 37.5) > 0.001 ||
        fabs(trace.resolved_dy + 25.0) > 0.001 ||
        !trace.hit_wall || !trace.hit_ceiling || !trace.hit_floor)
        return fail("native delta and collision flags resolve with Y inversion");

    player_timer_pipe_warping = 1;
    SmwFalconBeforePhysics(NULL);
    if (snes_foreign_ownership() != FOREIGN_OWNERSHIP_SCRIPTED)
        return fail("pipe handoff is scripted, not foreign");

    /* Grounded Up-B publishes force_airborne. The boundary must consume it
     * instead of leaving a dead result that SMW immediately grounds again. */
    player_timer_pipe_warping = 0;
    player_in_air_flag = 0;
    player_blocked_flags = 0;
    io_controller_hold1 = io_controller_press1 = 0;
    io_controller_hold2 = io_controller_press2 = 0;
    ++snes_frame_counter;
    SmwFalconBeforePhysics(NULL); /* reclaim after the scripted pipe handoff */
    io_controller_hold1 = io_controller_press1 = 0x08; /* Up */
    io_controller_hold2 = io_controller_press2 = 0x40; /* X */
    ++snes_frame_counter;
    SmwFalconBeforePlayerPhysics(NULL);
    SmwFalconBeforePhysics(NULL);
    if (player_in_air_flag == 0 || (int8_t)player_yspeed > -16)
        return fail("grounded Falcon Dive consumes force_airborne upward");
    player_ypos = (uint16)(player_ypos - 1);
    player_in_air_flag = 0; /* emulate native floor rediscovery this frame */
    SmwFalconAfterPhysics(NULL);
    if (snes_foreign_trace_last(1, &trace) != 1 ||
        trace.state != FL_FALCON_DIVE_GROUND || trace.grounded != 0)
        return fail("Up-B startup resolves as airborne after native collision");

    /* Water remains Falcon-controlled: an aerial Y attack still selects the
     * source state, but vertical output is buoyant and capped before SMW sees
     * it. Native cape/fire/spin/Yoshi mechanics are suppressed without
     * consuming the SMW power-up or reserve item. */
    if (!snes_foreign_select(SMW_CAPTAIN_FALCON_ID))
        return fail("reset selected controller for water seam");
    snes_foreign_set_ownership(FOREIGN_OWNERSHIP_FOREIGN);
    assert(snes_foreign_state()->state == FL_WAIT);
    misc_game_mode = 0x14;
    player_current_state = 0;
    player_timer_pipe_warping = 0;
    player_pipe_action = 0;
    flag_about_to_warp_in_pipe = 0;
    timer_end_level = 0;
    timer_end_level_via_keyhole = 0;
    flag_underwater_level = 1;
    player_in_air_flag = 1;
    player_yspeed = 0;
    player_current_power_up = 3;
    player_current_item_box = 3;
    player_spin_jump_flag = 1;
    player_spinjump_fireball_timer = 7;
    timer_display_player_shoot_fireball_pose = 7;
    player_cape_image = 1;
    flag_cape_to_sprite_interaction = 1;
    timer_active_cape_spin = 7;
    player_cape_flying_phase = 1;
    player_riding_yoshi_flag = 1;
    players_has_yoshi[0] = 1;
    yoshi_carry_over_levels_flag = 1;
    yoshi_yoshi_has_wings = 1;
    timer_yoshi_tongue_is_out = 7;
    io_controller_hold1 = 0;
    io_controller_press1 = 0x40; /* Y: Falcon aerial attack, never fireball. */
    io_controller_hold2 = io_controller_press2 = 0;
    ++snes_frame_counter;
    SmwFalconBeforePlayerPhysics(NULL);
    /* Native action code would run here. It sees the masked pad/state, while
     * the downstream Falcon tick consumes the preserved raw Y press. */
    if (io_controller_hold1 != 0 || io_controller_press1 != 0 ||
        io_controller_hold2 != 0 || io_controller_press2 != 0 ||
        player_spin_jump_flag || player_spinjump_fireball_timer ||
        timer_display_player_shoot_fireball_pose || player_cape_image ||
        flag_cape_to_sprite_interaction || timer_active_cape_spin ||
        player_cape_flying_phase || player_riding_yoshi_flag ||
        timer_yoshi_tongue_is_out)
        return fail("early seam masks native extensions before SMW action input");
    if (players_has_yoshi[0] != 1 || yoshi_carry_over_levels_flag != 1 ||
        yoshi_yoshi_has_wings != 1)
        return fail("Yoshi ownership and level-entity persistence survive dismount");
    SmwFalconBeforePhysics(NULL);
    if (snes_foreign_ownership() != FOREIGN_OWNERSHIP_FOREIGN ||
        snes_foreign_trace_last(1, &trace) != 1 ||
        trace.state != FL_ATTACK_AIR_N)
        return fail("underwater frame remains Falcon-owned and accepts attack");
    if (abs((int)(int8_t)player_yspeed) > 25 ||
        player_can_jump_out_of_water != 0)
        return fail("water vertical output is floaty and native swim jump is disabled");
    if (player_spin_jump_flag || player_spinjump_fireball_timer ||
        timer_display_player_shoot_fireball_pose || player_cape_image ||
        flag_cape_to_sprite_interaction || timer_active_cape_spin ||
        player_cape_flying_phase)
        return fail("native spin fire and cape actions are suppressed");
    if (player_riding_yoshi_flag || timer_yoshi_tongue_is_out)
        return fail("unsupported Yoshi mount is cleanly dismounted");
    if (players_has_yoshi[0] != 1 || yoshi_carry_over_levels_flag != 1 ||
        yoshi_yoshi_has_wings != 1)
        return fail("dismount preserves owned Yoshi and level entity state");
    if (player_current_power_up != 3 || player_current_item_box != 3)
        return fail("powerup and reserve progression remain SMW-owned");

    /* Fall long enough to reach the water terminal cap. The source reaches
     * -66, but the adapter exposes no more than 42 * 0.45 source units. */
    SmwFalconAfterPhysics(NULL);
    for (int i = 0; i < 24; ++i) {
        io_controller_hold1 = io_controller_press1 = 0;
        ++snes_frame_counter;
        SmwFalconBeforePlayerPhysics(NULL);
        SmwFalconBeforePhysics(NULL);
        SmwFalconAfterPhysics(NULL);
    }
    if (abs((int)(int8_t)player_yspeed) > 25)
        return fail("water terminal speed remains capped after sustained fall");
    flag_underwater_level = 0;

    /* A is the only carry bridge input. $01:AA42 still decides whether the
     * nearby native sprite is eligible and creates status $0B; this adapter
     * only exposes a post-physics Y bit for that lifecycle to consume. */
    if (!snes_foreign_select(SMW_CAPTAIN_FALCON_ID))
        return fail("reset selected controller for carry seam");
    snes_foreign_set_ownership(FOREIGN_OWNERSHIP_FOREIGN);
    player_in_air_flag = 0;
    player_carrying_something_flag1 = 0;
    g_ram[0x14C8] = 0x08; /* Unsupported/native-owned status sentinel. */
    io_controller_hold1 = 0x40; /* Physical Y: Falcon attack only. */
    io_controller_press1 = 0;
    io_controller_hold2 = io_controller_press2 = 0;
    ++snes_frame_counter;
    SmwFalconBeforePlayerPhysics(NULL);
    SmwFalconBeforePhysics(NULL);
    SmwFalconAfterPhysics(NULL);
    SmwFalconBeforeNormalSprites(NULL);
    if ((io_controller_hold1 & 0x44) != 0 || g_ram[0x14C8] != 0x08)
        return fail("physical Y and unsupported sprites never enter native carry");

    io_controller_hold1 = io_controller_press1 = 0;
    io_controller_hold2 = 0x80; /* Physical A: translated after player physics. */
    io_controller_press2 = 0;
    ++snes_frame_counter;
    SmwFalconBeforePlayerPhysics(NULL);
    SmwFalconBeforePhysics(NULL);
    SmwFalconAfterPhysics(NULL);
    /* CD36 collision resolution must not leak synthetic Down to player logic. */
    if ((io_controller_hold1 & 0x44) != 0)
        return fail("carry input remains absent through player interaction seam");
    SmwFalconBeforeNormalSprites(NULL);
    if (io_controller_hold1 != 0x40 || g_ram[0x14C8] != 0x08)
        return fail("A offers native Y without host-owned pickup or relocation");

    /* Model a successful native $01:AA42 pickup: status $0B and the carry
     * flag are native savestate RAM, not controller-private state. */
    g_ram[0x14C8] = 0x0B;
    player_carrying_something_flag1 = 1;
    io_controller_hold1 = io_controller_press1 = 0;
    io_controller_hold2 = 0x80;
    ++snes_frame_counter;
    SmwFalconBeforePlayerPhysics(NULL);
    SmwFalconBeforePhysics(NULL);
    SmwFalconAfterPhysics(NULL);
    SmwFalconBeforeNormalSprites(NULL);
    if (io_controller_hold1 != 0x40)
        return fail("held A keeps native status-0B item carried");

    io_controller_hold1 = io_controller_press1 = 0;
    io_controller_hold2 = io_controller_press2 = 0;
    ++snes_frame_counter;
    SmwFalconBeforePlayerPhysics(NULL);
    SmwFalconBeforePhysics(NULL);
    SmwFalconAfterPhysics(NULL);
    SmwFalconBeforeNormalSprites(NULL);
    if ((io_controller_hold1 & 0x44) != 0)
        return fail("A release exposes native throw without carry bits");

    io_controller_hold1 = 0x04; /* Down + released A means native set-down. */
    io_controller_press1 = 0;
    io_controller_hold2 = io_controller_press2 = 0;
    ++snes_frame_counter;
    SmwFalconBeforePlayerPhysics(NULL);
    SmwFalconBeforePhysics(NULL);
    SmwFalconAfterPhysics(NULL);
    if ((io_controller_hold1 & 0x44) != 0)
        return fail("Down cannot leak into native player interaction logic");
    SmwFalconBeforeNormalSprites(NULL);
    if (io_controller_hold1 != 0x04)
        return fail("Down survives only for native status-0B set-down");

    /* A bridged bit is never retained through a scripted handoff or a loaded
     * state. Native carry RAM itself belongs to the complete SMW savestate. */
    io_controller_hold1 = 0;
    io_controller_hold2 = 0x80;
    ++snes_frame_counter;
    SmwFalconBeforePlayerPhysics(NULL);
    SmwFalconBeforePhysics(NULL);
    SmwFalconAfterPhysics(NULL);
    SmwFalconBeforeNormalSprites(NULL);
    if (io_controller_hold1 != 0x40)
        return fail("carry bridge prepared handoff cleanup case");
    player_timer_pipe_warping = 1;
    SmwFalconBeforePhysics(NULL);
    if ((io_controller_hold1 & 0x44) != 0 ||
        snes_foreign_ownership() != FOREIGN_OWNERSHIP_SCRIPTED)
        return fail("pipe handoff clears translated carry input");
    player_timer_pipe_warping = 0;
    snes_foreign_set_ownership(FOREIGN_OWNERSHIP_FOREIGN);
    io_controller_hold1 = 0x44;
    timer_end_level = 1;
    SmwFalconBeforePhysics(NULL);
    if ((io_controller_hold1 & 0x44) != 0)
        return fail("goal handoff clears translated carry input");
    timer_end_level = 0;
    snes_foreign_set_ownership(FOREIGN_OWNERSHIP_FOREIGN);
    io_controller_hold1 = 0x44;
    player_current_state = 9;
    SmwFalconBeforePhysics(NULL);
    if ((io_controller_hold1 & 0x44) != 0)
        return fail("death handoff clears translated carry input");
    player_current_state = 0;
    io_controller_hold1 = 0x40;
    SmwFalconOnStateLoaded();
    if ((io_controller_hold1 & 0x44) != 0)
        return fail("state load clears transient carry translation");

    puts("falcon_smw_adapter: pad mapping, seam order, collision feedback PASS");
    return 0;
}
