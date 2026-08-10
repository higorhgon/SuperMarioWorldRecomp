/* Focused host-seam test: raw pad mapping, sign conversion, and native
 * movement/collision feedback without generated SMW sources. */
#include "../../overrides/falcon/falcon_smw_adapter.h"
#include "../../src/mods/falcon/captain_falcon_foreign.h"
#include "../../src/mods/falcon/falcon_locomotion.h"
#include "types.h"
#include "cpu_state.h"
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

/* The controller test has no owner cache.  Give TransN-bearing moves a
 * deterministic source-unit sample so the host seam can assert that it
 * applies a real root delta rather than an invented Kick velocity. */
int smw_falcon_presentation_root_delta(const char *animation, float frame,
                                       float *delta_y, float *delta_z)
{
    (void)frame;
    if (delta_y != NULL) *delta_y = 0.0f;
    if (delta_z != NULL) *delta_z = 0.0f;
    if (animation == NULL) return 0;
    if (strcmp(animation, "DownSpecial") == 0) {
        *delta_z = 64.0f;
        return 1;
    }
    if (strcmp(animation, "DownSpecialAir") == 0) {
        *delta_y = -48.0f;
        *delta_z = 40.0f;
        return 1;
    }
    return 0;
}

static int fail(const char *message)
{
    fprintf(stderr, "FAIL: %s\n", message);
    return 1;
}

static int yoshi_persistence_intact(void)
{
    return players_has_yoshi[0] == 1 &&
           yoshi_carry_over_levels_flag == 1 &&
           yoshi_yoshi_has_wings == 1 &&
           yoshi_current_yoshi_color == 2 &&
           sprites_yoshi_slot_index == 5 &&
           yoshi_stray_yoshi_flag == 5;
}

/* Relevant native path, transcribed from SMWDisX bank_01:
 * $01:ECE1/$01:ED70 accepts a falling airborne contact and writes C2, then
 * $01:ED38 is reached only after movement/clipping/real contact. PlayerDraw
 * -> $01:EA70 -> $01:EA8F/$01:EB82 then turns C2 into the rider and
 * progression/presentation side effects. This small model is deliberately
 * only the mount path; it lets the isolated host-seam harness prove that the
 * precise pre-contact block skip prevents all of those writes. */
static void model_native_yoshi_mount(unsigned slot)
{
    if (player_in_air_flag != 0 && player_riding_yoshi_flag == 0 &&
        (int8_t)player_yspeed >= 0) {
        spr_table00c2[slot] = 1;
        player_xspeed = 0;
        player_yspeed = 0;
        player_ypos = (uint16_t)(spr_ypos[slot] - 0x10);
        io_sound_ch1 = 0x2f; /* native Yoshi-drum-on cue, value immaterial */
        io_sound_ch3 = 0x29; /* native Yoshi cue */
    }
    if (spr_table00c2[slot] == 1) {
        player_riding_yoshi_flag = 1;
        yoshi_carry_over_levels_flag = 1;
        yoshi_current_yoshi_color = spr_table15f6[slot];
        player_facing_direction = (uint8_t)(spr_table157c[slot] ^ 1);
    }
}

/* Models the generated block placement: native Yoshi movement and clipping
 * already ran, CheckForContact succeeded, and the seam either takes native
 * $01:ED70 or continues into the fresh-mount eligibility/latch sequence. */
static void model_native_yoshi_contact_after_movement(unsigned slot, int skip_mount)
{
    if (!skip_mount) model_native_yoshi_mount(slot);
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
    /* $15=%byetUDLR, $17=%axlr0000. B/Y/X are Falcon-owned; A is the delayed
     * carry bridge rather than an accidental native spin-jump. */
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
    if (count != 1 || trace.frame != 1000 || trace.stick_x != 0.5f ||
        trace.stick_y != 0.0f || trace.raw_buttons != 0xC0F1)
        return fail("first directional tap is a half-stick walk trace");
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

    /* A D-pad first tap walks. Release then repeat the same direction inside
     * the documented window: only that second edge reaches the source's
     * full-stick Dash gate. */
    io_controller_hold1 = io_controller_press1 = 0;
    io_controller_hold2 = io_controller_press2 = 0;
    ++snes_frame_counter;
    SmwFalconBeforePlayerPhysics(NULL);
    SmwFalconBeforePhysics(NULL);
    SmwFalconAfterPhysics(NULL);
    io_controller_hold1 = io_controller_press1 = 0x01; /* Right */
    ++snes_frame_counter;
    SmwFalconBeforePlayerPhysics(NULL);
    SmwFalconBeforePhysics(NULL);
    if (snes_foreign_trace_last(1, &trace) != 1 || trace.stick_x != 1.0f ||
        trace.state != FL_DASH || (int8_t)player_xspeed <= 0)
        return fail("same-direction double tap enters sourced Dash with right velocity");
    SmwFalconAfterPhysics(NULL);

    /* The three Falcon actions are independent physical edges.  Square/Y is
     * special, X is normal, Cross/B is jump, and Circle/A alone remains a
     * carry-only input that cannot select a Falcon action. */
    if (!snes_foreign_select(SMW_CAPTAIN_FALCON_ID))
        return fail("reset selected controller for face-button mapping");
    io_controller_hold1 = io_controller_press1 = 0x40; /* Square / Y */
    io_controller_hold2 = io_controller_press2 = 0;
    ++snes_frame_counter;
    SmwFalconBeforePlayerPhysics(NULL);
    SmwFalconBeforePhysics(NULL);
    if (snes_foreign_trace_last(1, &trace) != 1 ||
        trace.state != FL_FALCON_PUNCH_GROUND)
        return fail("Square/Y selects Falcon special, not normal attack");
    SmwFalconAfterPhysics(NULL);

    if (!snes_foreign_select(SMW_CAPTAIN_FALCON_ID))
        return fail("reset selected controller for X normal");
    io_controller_hold1 = io_controller_press1 = 0;
    io_controller_hold2 = io_controller_press2 = 0x40; /* X */
    ++snes_frame_counter;
    SmwFalconBeforePlayerPhysics(NULL);
    SmwFalconBeforePhysics(NULL);
    if (snes_foreign_trace_last(1, &trace) != 1 || trace.state != FL_JAB)
        return fail("X selects Falcon normal, not special");
    SmwFalconAfterPhysics(NULL);

    if (!snes_foreign_select(SMW_CAPTAIN_FALCON_ID))
        return fail("reset selected controller for Cross jump");
    io_controller_hold1 = io_controller_press1 = 0x80; /* Cross / B */
    io_controller_hold2 = io_controller_press2 = 0;
    ++snes_frame_counter;
    SmwFalconBeforePlayerPhysics(NULL);
    SmwFalconBeforePhysics(NULL);
    if (snes_foreign_trace_last(1, &trace) != 1 || trace.state != FL_KNEEBEND)
        return fail("Cross/B selects only the Falcon jump path");
    SmwFalconAfterPhysics(NULL);

    if (!snes_foreign_select(SMW_CAPTAIN_FALCON_ID))
        return fail("reset selected controller for Circle carry");
    io_controller_hold1 = io_controller_press1 = 0;
    io_controller_hold2 = io_controller_press2 = 0x80; /* Circle / A */
    ++snes_frame_counter;
    SmwFalconBeforePlayerPhysics(NULL);
    SmwFalconBeforePhysics(NULL);
    if (snes_foreign_trace_last(1, &trace) != 1 || trace.state != FL_WAIT)
        return fail("Circle/A cannot select a Falcon attack or jump");
    SmwFalconAfterPhysics(NULL);

    /* Stubbed cache TransN samples prove Kick is not stationary: Down+Square
     * travels horizontally on ground, and direct aerial Kick travels down and
     * forward with facing applied once. */
    if (!snes_foreign_select(SMW_CAPTAIN_FALCON_ID))
        return fail("reset selected controller for grounded Kick");
    player_in_air_flag = 0;
    io_controller_hold1 = io_controller_press1 = 0x44; /* Down + Square/Y */
    io_controller_hold2 = io_controller_press2 = 0;
    ++snes_frame_counter;
    SmwFalconBeforePlayerPhysics(NULL);
    SmwFalconBeforePhysics(NULL);
    if (snes_foreign_trace_last(1, &trace) != 1 ||
        trace.state != FL_FALCON_KICK_GROUND || (int8_t)player_xspeed != 82 ||
        player_yspeed != 0)
        return fail("grounded Kick consumes horizontal DownSpecial TransN");
    SmwFalconAfterPhysics(NULL);

    if (!snes_foreign_select(SMW_CAPTAIN_FALCON_ID))
        return fail("reset selected controller for aerial Kick");
    player_in_air_flag = 1;
    player_yspeed = 0;
    io_controller_hold1 = io_controller_press1 = 0x44; /* Down + Square/Y */
    io_controller_hold2 = io_controller_press2 = 0;
    ++snes_frame_counter;
    SmwFalconBeforePlayerPhysics(NULL);
    SmwFalconBeforePhysics(NULL);
    if (snes_foreign_trace_last(1, &trace) != 1 ||
        trace.state != FL_FALCON_KICK_AIR || (int8_t)player_xspeed != 51 ||
        (int8_t)player_yspeed != 61)
        return fail("aerial Kick consumes down-forward DownSpecialAir TransN");
    SmwFalconAfterPhysics(NULL);
    player_in_air_flag = 0;

    /* Neutral Square/Y turns into an opposite-facing Falcon Punch in the
     * source.  Let that finish, then prove the next Down+Square Kick mirrors
     * the same TransN delta rather than assigning a right-only host speed. */
    if (!snes_foreign_select(SMW_CAPTAIN_FALCON_ID))
        return fail("reset selected controller for left-facing Kick");
    io_controller_hold1 = io_controller_press1 = 0x42; /* Left + Square/Y */
    io_controller_hold2 = io_controller_press2 = 0;
    ++snes_frame_counter;
    SmwFalconBeforePlayerPhysics(NULL);
    SmwFalconBeforePhysics(NULL);
    SmwFalconAfterPhysics(NULL);
    for (int frame = 0; frame < 90; ++frame) {
        io_controller_hold1 = io_controller_press1 = 0;
        ++snes_frame_counter;
        SmwFalconBeforePlayerPhysics(NULL);
        SmwFalconBeforePhysics(NULL);
        SmwFalconAfterPhysics(NULL);
    }
    io_controller_hold1 = io_controller_press1 = 0x46; /* Left, Down, Y */
    ++snes_frame_counter;
    SmwFalconBeforePlayerPhysics(NULL);
    SmwFalconBeforePhysics(NULL);
    if (snes_foreign_trace_last(1, &trace) != 1 ||
        trace.state != FL_FALCON_KICK_GROUND ||
        snes_foreign_state()->facing != -1.0f ||
        (int8_t)player_xspeed != -82)
        return fail("left-facing Kick mirrors grounded TransN velocity once");
    SmwFalconAfterPhysics(NULL);

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
    io_controller_hold1 = io_controller_press1 = 0x48; /* Up + Square/Y */
    io_controller_hold2 = io_controller_press2 = 0;
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

    /* Water remains Falcon-controlled: an aerial X attack still selects the
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
    yoshi_current_yoshi_color = 2;
    sprites_yoshi_slot_index = 5;
    yoshi_stray_yoshi_flag = 5;
    spr_spriteid[5] = 0x35;
    spr_table00c2[5] = 1;
    timer_yoshi_tongue_is_out = 7;
    timer_yoshi_tongue_init = 7;
    io_controller_hold1 = 0;
    io_controller_press1 = 0;
    io_controller_hold2 = io_controller_press2 = 0x40; /* X: normal attack. */
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
        spr_table00c2[5] != 0 ||
        timer_yoshi_tongue_is_out || timer_yoshi_tongue_init)
        return fail("early seam masks native extensions before SMW action input");
    if (!yoshi_persistence_intact())
        return fail("Yoshi ownership and level-entity persistence survive dismount");

    /* Model the real $01:ECE1 mount candidate, rather than merely setting
     * $187A after the fact. Native code would otherwise set C2=1, rider,
     * colour/facing/carry-over, SFX, smoke, bounce, and player Y position
     * before PlayerDraw later reasserts $187A at $01:EB82. */
    player_in_air_flag = 1;
    player_yspeed = 0x20;
    player_riding_yoshi_flag = 0;
    spr_table00c2[5] = 0;
    spr_table15f6[5] = 7;
    spr_table157c[5] = 0;
    player_facing_direction = 0;
    player_ypos = 0x1234;
    spr_ypos[5] = 0x0200;
    yoshi_current_yoshi_color = 2;
    io_sound_ch1 = io_sound_ch3 = 0;
    SmwFalconBeforeYoshi(NULL);
    if (player_yspeed != 0x20)
        return fail("Yoshi block seam leaves Falcon velocity unchanged");
    const int skip_mount = SmwFalconSkipYoshiMount(NULL);
    if (!skip_mount)
        return fail("Yoshi block seam takes the native contact-return path");
    /* The generated $01:ED38 seam takes its jump instead of executing this
     * native mount model. Keep it in the test to make the excluded writes
     * concrete and detectable. */
    model_native_yoshi_contact_after_movement(5, skip_mount);
    /* These are the actual $01:ED70/$01:EB82 side-effect fields. The model
     * stays on its no-contact path because the hook jumps after native contact
     * confirmation but before the mount eligibility/latch writes. */
    if (spr_table00c2[5] != 0 || player_riding_yoshi_flag ||
        yoshi_current_yoshi_color != 2 || player_facing_direction != 0 ||
        player_ypos != 0x1234 || io_sound_ch1 || io_sound_ch3)
        return fail("Yoshi mount contact reaches none of its native side effects");
    if (!yoshi_persistence_intact())
        return fail("mount guard preserves Yoshi entity and progression");
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
    if (player_riding_yoshi_flag || timer_yoshi_tongue_is_out ||
        timer_yoshi_tongue_init)
        return fail("unsupported Yoshi mount is cleanly dismounted");
    if (!yoshi_persistence_intact())
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
    io_controller_hold1 = 0x40; /* Physical Square/Y: Falcon special only. */
    io_controller_press1 = 0;
    io_controller_hold2 = io_controller_press2 = 0;
    ++snes_frame_counter;
    SmwFalconBeforePlayerPhysics(NULL);
    SmwFalconBeforePhysics(NULL);
    SmwFalconAfterPhysics(NULL);
    SmwFalconBeforeNormalSprites(NULL);
    if ((io_controller_hold1 & 0x44) != 0 || g_ram[0x14C8] != 0x08)
        return fail("physical Square/Y and unsupported sprites never enter native carry");

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

    /* A Falcon-enabled level never lets native transition/load states retain
     * mount ownership.  The persistent Yoshi records and selected entity stay
     * untouched through pipe, goal, death, and an already-mounted save. */
    player_riding_yoshi_flag = 1;
    spr_table00c2[5] = 1;
    timer_yoshi_tongue_is_out = timer_yoshi_tongue_init = 7;
    player_timer_pipe_warping = 1;
    SmwFalconBeforeYoshi(NULL);
    if (player_riding_yoshi_flag || timer_yoshi_tongue_is_out ||
        timer_yoshi_tongue_init || spr_table00c2[5] != 0 ||
        !yoshi_persistence_intact())
        return fail("pipe handoff dismounts without corrupting Yoshi persistence");
    player_timer_pipe_warping = 0;

    player_riding_yoshi_flag = 1;
    spr_table00c2[5] = 1;
    timer_yoshi_tongue_is_out = timer_yoshi_tongue_init = 7;
    timer_end_level = 1;
    SmwFalconBeforeYoshi(NULL);
    if (player_riding_yoshi_flag || timer_yoshi_tongue_is_out ||
        timer_yoshi_tongue_init || spr_table00c2[5] != 0 ||
        !yoshi_persistence_intact())
        return fail("goal handoff dismounts without corrupting Yoshi persistence");
    timer_end_level = 0;

    player_riding_yoshi_flag = 1;
    spr_table00c2[5] = 1;
    timer_yoshi_tongue_is_out = timer_yoshi_tongue_init = 7;
    player_current_state = 9;
    SmwFalconBeforeYoshi(NULL);
    if (player_riding_yoshi_flag || timer_yoshi_tongue_is_out ||
        timer_yoshi_tongue_init || spr_table00c2[5] != 0 ||
        !yoshi_persistence_intact())
        return fail("death handoff dismounts without corrupting Yoshi persistence");
    player_current_state = 0;

    player_riding_yoshi_flag = 1;
    spr_table00c2[5] = 1;
    timer_yoshi_tongue_is_out = timer_yoshi_tongue_init = 7;
    snes_foreign_set_ownership(FOREIGN_OWNERSHIP_SCRIPTED);
    SmwFalconOnStateLoaded();
    if (player_riding_yoshi_flag || timer_yoshi_tongue_is_out ||
        timer_yoshi_tongue_init || spr_table00c2[5] != 0 ||
        !yoshi_persistence_intact())
        return fail("mounted Falcon save loads as a clean native Yoshi dismount");

    /* The same hooks must be inert after the trusted Falcon controller is
     * reset/unselected, preserving exact native/mod-off behaviour. */
    player_riding_yoshi_flag = 1;
    spr_table00c2[5] = 1;
    timer_yoshi_tongue_is_out = timer_yoshi_tongue_init = 7;
    snes_foreign_select(NULL);
    SmwFalconBeforeYoshi(NULL);
    SmwFalconOnStateLoaded();
    if (player_riding_yoshi_flag != 1 || spr_table00c2[5] != 1 ||
        timer_yoshi_tongue_is_out != 7 ||
        timer_yoshi_tongue_init != 7 || !yoshi_persistence_intact())
        return fail("mod-off Yoshi state is exact and untouched");

    puts("falcon_smw_adapter: pad, collision, carry, and Yoshi seams PASS");
    return 0;
}
