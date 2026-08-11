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

/* The adapter harness deliberately has no generated game bodies.  The attack
 * stub models only the persistent native contact consequence used by the
 * bridge's accepted-transaction contract; detailed status routing remains in
 * falcon_combat_apply_test. */
static int s_native_attack_contacts;
void CheckPlayerAttackToNormalSpriteColl_029404(CpuState *cpu)
{
    if (cpu == NULL) return;
    ++s_native_attack_contacts;
    ++cpu->ram[0x1DFC]; /* native contact/SFX-side effect, outside scratch */
}
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

static uint16_t test_sprite_ypos(unsigned slot)
{
    return (uint16_t)((uint16_t)spr_ypos_lo[slot] |
                      ((uint16_t)spr_ypos_hi[slot] << 8));
}

static void test_set_sprite_ypos(unsigned slot, uint16_t value)
{
    spr_ypos_lo[slot] = (uint8_t)value;
    spr_ypos_hi[slot] = (uint8_t)(value >> 8);
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
        player_ypos = (uint16_t)(test_sprite_ypos(slot) - 0x10);
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

/* The later ordinary/custom side-damage path at $01:A8E6 first returns when
 * the native IFrameTimer ($1497) is nonzero, otherwise it can reach
 * HurtMario. Model only that reviewed guard decision here. */
static void model_native_later_side_damage(void)
{
    if (timer_player_hurt == 0) player_current_state = 9;
}

/* Drive the two ordinary player seams in their guest execution order.  The
 * harness has no native position integrator, intentionally: these directional
 * checks assert the host velocity that would be consumed by $00:DC2D. */
static void adapter_frame(uint8_t hold1, uint8_t press1,
                          uint8_t hold2, uint8_t press2)
{
    io_controller_hold1 = hold1;
    io_controller_press1 = press1;
    io_controller_hold2 = hold2;
    io_controller_press2 = press2;
    ++snes_frame_counter;
    SmwFalconBeforePlayerPhysics(NULL);
    SmwFalconBeforePhysics(NULL);
    SmwFalconAfterPhysics(NULL);
}

static void adapter_frame_cpu(uint8_t hold1, uint8_t press1,
                              uint8_t hold2, uint8_t press2, CpuState *cpu)
{
    io_controller_hold1 = hold1;
    io_controller_press1 = press1;
    io_controller_hold2 = hold2;
    io_controller_press2 = press2;
    ++snes_frame_counter;
    SmwFalconBeforePlayerPhysics(NULL);
    SmwFalconBeforePhysics(NULL);
    SmwFalconAfterPhysics(cpu);
}

int main(void)
{
    ForeignTraceEntry trace;
    CpuState attack_cpu;
    int count;

    /* The title attract handler retains GM=$07 while it jumps into its level
     * code. With no selected foreign controller this seam must be a no-op. */
    memset(g_ram, 0, sizeof(g_ram));
    misc_game_mode = 0x07;
    timer_player_hurt = 0x5A;
    SmwFalconBeforeNormalSprites(NULL);
    if (timer_player_hurt != 0x5A)
        return fail("mod-off title attract leaves native invulnerability untouched");

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
    io_controller_hold1 = 0x01; /* live slot-0 signature is held right */
    io_controller_press1 = 0;
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

    /* SMWDisX's GameMode07 title attract routine JMPs into level handling
     * without changing $0100. Its recorded Koopa side-hit must remain native
     * except for one-frame $1497 invulnerability: collision treats any
     * nonzero value as protected and PlayerDraw's value 1 remains visible.
     * Only private deferred-carry state is discarded; the recorded input
     * bytes themselves remain exactly native. */
    misc_game_mode = 0x07;
    timer_player_hurt = 0;
    io_controller_hold1 = 0x54;  /* Start plus translated Y/Down carry bits */
    io_controller_press1 = 0x40;
    SmwFalconBeforeNormalSprites(NULL);
    if (timer_player_hurt != 1 || io_controller_hold1 != 0x54 ||
        io_controller_press1 != 0x40)
        return fail("active title attract receives visible native damage immunity");

    misc_game_mode = 0x14;
    timer_player_hurt = 0x5A;
    io_controller_hold1 = 0x54;
    io_controller_press1 = 0x40;
    SmwFalconBeforeNormalSprites(NULL);
    if (timer_player_hurt != 0x5A || io_controller_hold1 != 0x54 ||
        io_controller_press1 != 0x40)
        return fail("ordinary GM14 never receives title-attract immunity");

    /* A left press from a right-facing idle starts source Turn, whose authored
     * facing flip is at frame 4.  Do not mistake the first three stationary
     * Turn frames for an X-sign bug.  Once the flip is complete, require the
     * release then repeat that same left D-pad edge inside the documented
     * window.  The turn-initiating edge is tap one, so the repeated edge must
     * produce the sourced full-stick Dash->Run path with negative velocity. */
    player_timer_pipe_warping = 1;
    SmwFalconBeforePhysics(NULL); /* also clears the adapter tap bridge */
    player_timer_pipe_warping = 0;
    player_facing_direction = 1;
    player_in_air_flag = 0;
    if (!snes_foreign_select(SMW_CAPTAIN_FALCON_ID))
        return fail("reset selected controller for left Dash turn");
    adapter_frame(0x02, 0x02, 0, 0); /* Left: enter FL_TURN. */
    for (int frame = 0; frame < 4; ++frame)
        adapter_frame(0x02, 0, 0, 0);
    if (snes_foreign_state() == NULL || snes_foreign_state()->facing != -1.0f)
        return fail("left turn reaches the source frame-4 facing flip");

    adapter_frame(0, 0, 0, 0);
    adapter_frame(0x02, 0x02, 0, 0); /* repeat left: full source stick */
    if (snes_foreign_trace_last(1, &trace) != 1 || trace.stick_x != -1.0f ||
        trace.state != FL_DASH || (int8_t)player_xspeed >= 0)
        return fail("left double tap enters sourced Dash with negative velocity");
    for (int frame = 0; frame < 16; ++frame)
        adapter_frame(0x02, 0, 0, 0);
    if (snes_foreign_trace_last(1, &trace) != 1 || trace.state != FL_RUN ||
        (int8_t)player_xspeed >= 0)
        return fail("left Dash advances to Run with negative velocity");

    /* The three Falcon actions are independent physical edges.  Square/Y is
     * special, X is normal, Cross/B is jump, and Circle/A alone remains a
     * carry-only input that cannot select a Falcon action.  A neutral Y edge
     * has the source-proven one-frame directional grace before Falcon Punch. */
    if (!snes_foreign_select(SMW_CAPTAIN_FALCON_ID))
        return fail("reset selected controller for face-button mapping");
    io_controller_hold1 = io_controller_press1 = 0x40; /* Square / Y */
    io_controller_hold2 = io_controller_press2 = 0;
    ++snes_frame_counter;
    SmwFalconBeforePlayerPhysics(NULL);
    SmwFalconBeforePhysics(NULL);
    if (snes_foreign_trace_last(1, &trace) != 1 ||
        trace.state != FL_WAIT)
        return fail("neutral Square/Y defers one frame for directional special");
    SmwFalconAfterPhysics(NULL);
    io_controller_hold1 = io_controller_press1 = 0;
    ++snes_frame_counter;
    SmwFalconBeforePlayerPhysics(NULL);
    SmwFalconBeforePhysics(NULL);
    if (snes_foreign_trace_last(1, &trace) != 1 ||
        trace.state != FL_FALCON_PUNCH_GROUND)
        return fail("deferred Square/Y selects Falcon special, not normal attack");
    SmwFalconAfterPhysics(NULL);

    /* The grace edge is intentionally transient host state.  A save/load
     * cannot revive a pre-save Square press into a later Up-special. */
    if (!snes_foreign_select(SMW_CAPTAIN_FALCON_ID))
        return fail("reset selected controller for grace savestate boundary");
    io_controller_hold1 = io_controller_press1 = 0x40;
    ++snes_frame_counter;
    SmwFalconBeforePlayerPhysics(NULL);
    SmwFalconBeforePhysics(NULL);
    SmwFalconAfterPhysics(NULL);
    SmwFalconOnStateLoaded();
    adapter_frame(0x08, 0, 0, 0); /* Up, but no restored Square edge. */
    if (snes_foreign_trace_last(1, &trace) != 1 ||
        trace.state == FL_FALCON_DIVE_GROUND ||
        trace.state == FL_FALCON_DIVE_AIR)
        return fail("state load clears deferred Square/Y edge rather than reviving it");

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
    player_in_air_flag = 0; /* emulate native floor rediscovery this frame */
    SmwFalconAfterPhysics(NULL);
    if (snes_foreign_trace_last(1, &trace) != 1 ||
        trace.state != FL_FALCON_DIVE_GROUND || trace.grounded != 0)
        return fail("Up-B startup resolves as airborne after native collision");
    /* No test-only position nudge: the real SMW collision can report the
     * same floor after the first subpixel/one-pixel request. The following
     * DC2D seam must preserve the bounded departure edge and give native
     * integration another upward opportunity before source state is sampled. */
    io_controller_hold1 = io_controller_press1 = 0;
    ++snes_frame_counter;
    SmwFalconBeforePlayerPhysics(NULL);
    SmwFalconBeforePhysics(NULL);
    if (player_in_air_flag == 0 || (int8_t)player_yspeed > -16 ||
        snes_foreign_state()->grounded != 0)
        return fail("Up-B departure latch survives native floor rediscovery");
    SmwFalconAfterPhysics(NULL);

    /* The mature NES input seam gives a directionless special edge one frame
     * of grace.  This is particularly important on a D-pad: Y then Up must
     * enter the same grounded Dive as a simultaneous Up+Y edge, rather than
     * being consumed by neutral Falcon Punch. */
    if (!snes_foreign_select(SMW_CAPTAIN_FALCON_ID))
        return fail("reset selected controller for deferred grounded Up-B");
    player_in_air_flag = 0;
    io_controller_hold1 = io_controller_press1 = 0x40; /* Square/Y first */
    io_controller_hold2 = io_controller_press2 = 0;
    ++snes_frame_counter;
    SmwFalconBeforePlayerPhysics(NULL);
    SmwFalconBeforePhysics(NULL);
    if (snes_foreign_trace_last(1, &trace) != 1 || trace.state != FL_WAIT)
        return fail("deferred Up-B holds a neutral special edge for one frame");
    SmwFalconAfterPhysics(NULL);
    io_controller_hold1 = 0x08; /* Up arrives on the grace frame */
    io_controller_press1 = 0;
    ++snes_frame_counter;
    SmwFalconBeforePlayerPhysics(NULL);
    SmwFalconBeforePhysics(NULL);
    if (snes_foreign_trace_last(1, &trace) != 1 ||
        trace.state != FL_FALCON_DIVE_GROUND || player_in_air_flag == 0)
        return fail("Y then Up enters grounded Falcon Dive through the normal edge");
    player_in_air_flag = 0; /* native may rediscover the starting floor */
    SmwFalconAfterPhysics(NULL);
    io_controller_hold1 = io_controller_press1 = 0;
    ++snes_frame_counter;
    SmwFalconBeforePlayerPhysics(NULL);
    SmwFalconBeforePhysics(NULL);
    if (player_in_air_flag == 0 || (int8_t)player_yspeed > -16 ||
        snes_foreign_state()->grounded != 0)
        return fail("Y then Up keeps the same departure latch through floor rediscovery");
    SmwFalconAfterPhysics(NULL);
    /* Direct controller selection is the harness's fresh-match boundary; use
     * the real savestate/reset hook so no host latch leaks into the following
     * independently seeded aerial parity case. */
    SmwFalconOnStateLoaded();

    /* Ground and air use the same immediate Up+Y input priority; only the
     * source-selected state differs.  This keeps the floor handoff from
     * becoming a ground-only host bypass. */
    if (!snes_foreign_select(SMW_CAPTAIN_FALCON_ID))
        return fail("reset selected controller for aerial Up-B parity");
    player_in_air_flag = 1;
    io_controller_hold1 = io_controller_press1 = 0x48; /* Up + Square/Y */
    io_controller_hold2 = io_controller_press2 = 0;
    ++snes_frame_counter;
    SmwFalconBeforePlayerPhysics(NULL);
    SmwFalconBeforePhysics(NULL);
    if (snes_foreign_trace_last(1, &trace) != 1 ||
        trace.state != FL_FALCON_DIVE_AIR || player_in_air_flag == 0)
        return fail("aerial Up-B shares immediate Square/Y priority");
    SmwFalconAfterPhysics(NULL);

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
    test_set_sprite_ypos(5, 0x0200);
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

    /* Confirmed SMW order: a successful ordinary stomp reaches native
     * BoostMarioSpeed ($01:AA33), which writes D0 before returning through
     * $01:AA41. The block seam observes that exact native impulse and arms
     * only the remainder of this normal-sprite pass against later multi-hit
     * or custom side damage; it never changes the native stomp decision. */
    if (!snes_foreign_select(SMW_CAPTAIN_FALCON_ID))
        return fail("reset selected controller for stomp seam");
    snes_foreign_set_ownership(FOREIGN_OWNERSHIP_FOREIGN);
    misc_game_mode = 0x14;
    player_current_state = 0;
    player_in_air_flag = 0;
    player_yspeed = 0;
    io_controller_hold1 = io_controller_press1 = 0;
    io_controller_hold2 = io_controller_press2 = 0;
    ++snes_frame_counter;
    SmwFalconBeforePhysics(NULL);
    SmwFalconAfterPhysics(NULL); /* arms the future normal-sprite observer */
    timer_player_hurt = 0;
    player_in_air_flag = 1;
    player_yspeed = 0xD0;
    SmwFalconOnNativeStompBounce(NULL);
    if (snes_foreign_state() == NULL ||
        fabs(snes_foreign_state()->vy - 37.5) > 0.001 ||
        snes_foreign_state()->grounded || timer_player_hurt != 1 ||
        player_current_state != 0)
        return fail("native stomp bounce and one-contact immunity are adopted exactly");
    model_native_later_side_damage();
    if (player_current_state != 0)
        return fail("same-pass multi-hit side damage is blocked after native stomp");
    /* The latch is consumed at the next D5F2, before the next normal-sprite
     * pass. Do not leave broad native invulnerability behind. */
    SmwFalconBeforePlayerPhysics(NULL);
    if (timer_player_hurt != 0 || player_current_state != 0)
        return fail("stomp immunity latch clears before the next frame");

    /* Kick's native $02:9404 consequence can leave a multi-hit target in
     * status $08, after which the later normal-sprite side-damage route would
     * ordinarily hurt Falcon.  Exercise the real adapter sequence through
     * active frame 12 and prove its per-slot $1497 guard protects only the
     * connected slot, then clears before the next slot. */
    if (!snes_foreign_select(SMW_CAPTAIN_FALCON_ID))
        return fail("reset selected controller for Kick contact guard");
    snes_foreign_set_ownership(FOREIGN_OWNERSHIP_FOREIGN);
    misc_game_mode = 0x14;
    player_current_state = 0;
    player_in_air_flag = 0;
    player_xpos = 100;
    player_ypos = 200;
    timer_player_hurt = 0;
    spr_current_status[0] = 8;
    spr_spriteid[0] = 0x0f;
    spr_xpos_lo[0] = 170;
    spr_xpos_hi[0] = 0;
    spr_ypos_lo[0] = 220;
    spr_ypos_hi[0] = 0;
    memset(&attack_cpu, 0, sizeof(attack_cpu));
    attack_cpu.ram = g_ram;
    attack_cpu.m_flag = attack_cpu.x_flag = 1;
    attack_cpu.P = 0x30;
    s_native_attack_contacts = 0;
    for (int frame = 0; frame != 13; ++frame) {
        const uint8_t special = frame == 0 ? 0x44 : 0;
        adapter_frame_cpu(special, special, 0, 0, &attack_cpu);
    }
    if (!smw_falcon_last_attack()->active || s_native_attack_contacts != 1 ||
        timer_player_hurt != 0 || player_current_state != 0)
        return fail("active Kick contact records only its connected slot");
    attack_cpu.X = 0;
    SmwFalconBeforeNormalSprites(&attack_cpu);
    if (timer_player_hurt != 1)
        return fail("connected Kick slot arms its native side-damage guard");
    model_native_later_side_damage();
    if (player_current_state != 0)
        return fail("connected Kick slot blocks same-pass side damage only");
    attack_cpu.X = 1;
    SmwFalconBeforeNormalSprites(&attack_cpu);
    if (timer_player_hurt != 0)
        return fail("Kick contact guard clears before the next unhit slot");

    /* Live slot 0 reproduced this exact native condition while a small
     * Falcon (powerup $00) ran into a one-block step: after collision, $77
     * changed from $04 to $1D, and SMWDisX $00:E9FB would immediately
     * branch to $00:EA08/DamagePlayer_KillAndDisableButtons.  This hook runs
     * before that branch, restores the last DC2D position, and makes the
     * opening a wall instead of granting invulnerability or leaving an
     * embedded player. */
    if (!snes_foreign_select(SMW_CAPTAIN_FALCON_ID))
        return fail("reset selected controller for one-block-step guard");
    snes_foreign_set_ownership(FOREIGN_OWNERSHIP_FOREIGN);
    misc_game_mode = 0x14;
    player_current_state = 0;
    player_current_power_up = 0;
    player_xpos = 0x070F;
    player_ypos = 0x0160;
    player_xspeed = 0x65;
    player_yspeed = 0;
    player_in_air_flag = 0;
    io_controller_hold1 = io_controller_press1 = 0;
    io_controller_hold2 = io_controller_press2 = 0;
    ++snes_frame_counter;
    /* This direct seam model supplies the immediately preceding valid
     * coordinate. The live f310/f324 log establishes the signature, not a
     * single 59px integration step. */
    SmwFalconBeforePhysics(NULL);
    if (snes_foreign_state() == NULL)
        return fail("one-block-step guard has a Falcon state");
    snes_foreign_state()->state = FL_RUN;
    player_xpos = 0x074A;
    player_ypos = 0x0160;
    player_xspeed = 0;
    player_yspeed = 0x96;
    player_blocked_flags = 0x1D;
    SmwFalconBeforeCrushCheck(NULL);
    if (player_xpos != 0x070F || player_ypos != 0x0160 ||
        player_xspeed != 0 || player_yspeed != 0 ||
        player_blocked_flags != 0x05)
        return fail("step guard restores position and preserves wall/floor contact");
    /* Do not broadly immunize genuine moving-ceiling / airborne crushes. */
    player_xpos = 0x074A;
    player_ypos = 0x015F;
    player_xspeed = 0x65;
    player_blocked_flags = 0x1D;
    snes_foreign_state()->grounded = 0;
    SmwFalconBeforeCrushCheck(NULL);
    if (player_xpos != 0x074A || player_ypos != 0x015F ||
        player_xspeed != 0x65 || player_blocked_flags != 0x1D)
        return fail("airborne or vertically displaced crush remains native-owned");
    snes_foreign_state()->grounded = 1;
    SmwFalconAfterPhysics(NULL);

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
