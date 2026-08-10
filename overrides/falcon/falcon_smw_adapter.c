/* Native SMW host boundary for the portable Captain Falcon controller. */
#include "falcon_smw_adapter.h"

#include "types.h"
#include "variables.h"
#include "common_rtl.h"
#include "src/mods/falcon/smw_falcon_audio.h"
#include "src/mods/falcon/smw_falcon_combat_apply.h"

#include <stdint.h>
#include <string.h>

#define FALCON_TO_SMW_PX 0.08
#define SMW_SPEED_PER_PX 16.0
#define SMW_TO_FALCON (1.0 / FALCON_TO_SMW_PX)

/* ADAPTATION: keep the source controller in water, but map its vertical
 * output through a deliberately floaty SMW host envelope. This avoids handing
 * the pad back to native swim while keeping Falcon attacks and air states. */
#define SMW_FALCON_WATER_VERTICAL_SCALE 0.45
#define SMW_FALCON_WATER_TERMINAL_FALL 42.0
#define SMW_FALCON_DASH_DOUBLE_TAP_FRAMES 15

static int s_pending;
static int s_force_airborne_pending;
static int s_dash_first_dir;
static int s_dash_prev_dir;
static int s_dash_full_hold;
static unsigned s_dash_tap_age;
static uint16_t s_x_before;
static uint16_t s_y_before;
static ForeignMoveResult s_last_move;
static struct {
    uint8_t hold1;
    uint8_t press1;
    uint8_t hold2;
    uint8_t press2;
    uint8_t carry_a;
    uint8_t carry_down;
    int valid;
    int carry_valid;
} s_foreign_pad;

static int signed8(uint8_t value) { return (int)(int8_t)value; }

static void smw_falcon_reset_dash_taps(void)
{
    s_dash_first_dir = 0;
    s_dash_prev_dir = 0;
    s_dash_full_hold = 0;
    s_dash_tap_age = 0;
}

static uint8_t clamp_speed(double source_delta, int y_axis)
{
    /* SMW stores a signed 8-bit speed in sixteenth-pixel units. Falcon's
     * +Y-up request is inverted exactly once here because SMW's +Y is down. */
    double value = source_delta * FALCON_TO_SMW_PX * SMW_SPEED_PER_PX;
    int rounded;
    if (y_axis) value = -value;
    rounded = value >= 0.0 ? (int)(value + 0.5) : (int)(value - 0.5);
    if (rounded > 127) rounded = 127;
    if (rounded < -127) rounded = -127;
    return (uint8_t)(int8_t)rounded;
}

static int smw_falcon_playable(void)
{
    return misc_game_mode == 0x14 && player_current_state == 0 &&
           player_timer_pipe_warping == 0 && player_pipe_action == 0 &&
           flag_about_to_warp_in_pipe == 0 && timer_end_level == 0 &&
           timer_end_level_via_keyhole == 0;
}

static void smw_falcon_disable_native_extensions(void)
{
    /* Falcon's health/progression remains SMW-owned: do not change $19 or the
     * reserve item box. Only suppress native actions that compete with the
     * foreign controller while it owns the playable frame. */
    player_spin_jump_flag = 0;
    player_spinjump_fireball_timer = 0;
    timer_display_player_shoot_fireball_pose = 0;
    player_cape_image = 0;
    flag_cape_to_sprite_interaction = 0;
    timer_active_cape_spin = 0;
    timer_cape_flap_animation = 0;
    timer_wait_before_cape_flight_begins = 0;
    timer_time_to_float_after_cape_flight = 0;
    player_cape_flying_phase = 0;
    player_cape_glide_index = 0;
    player_furthest_cape_dive_stage = 0;

    /* Yoshi's active rider/tongue state cannot share Falcon's host boundary.
     * Dismount without touching the persistent owned-Yoshi flags or the level
     * entity; both remain native SMW progression. */
    player_riding_yoshi_flag = 0;
    timer_yoshi_tongue_is_out = 0;

    if (flag_underwater_level)
        player_can_jump_out_of_water = 0;
}

static void smw_falcon_capture_and_mask_input(void)
{
    /* This runs at HandlePlayerPhysics ($00:D5F2), before SMW reads the
     * controller for movement, spin, fire, cape, or swimming. Preserve the
     * physical pad for Falcon's downstream tick, then remove only native
     * gameplay buttons. Start/Select retain their normal system behaviour. */
    s_foreign_pad.hold1 = io_controller_hold1;
    s_foreign_pad.press1 = io_controller_press1;
    s_foreign_pad.hold2 = io_controller_hold2;
    s_foreign_pad.press2 = io_controller_press2;
    s_foreign_pad.carry_a = (io_controller_hold2 & 0x80) != 0;
    s_foreign_pad.carry_down = (io_controller_hold1 & 0x04) != 0;
    s_foreign_pad.valid = 1;
    s_foreign_pad.carry_valid = 1;

    io_controller_hold1 &= (uint8_t)~0xCF;  /* B,Y,U,D,L,R */
    io_controller_press1 &= (uint8_t)~0xCF;
    io_controller_hold2 &= (uint8_t)~0xC0;  /* A carry, X normal */
    io_controller_press2 &= (uint8_t)~0xC0;
}

static void smw_falcon_clear_carry_bridge(void)
{
    s_foreign_pad.carry_a = 0;
    s_foreign_pad.carry_down = 0;
    s_foreign_pad.carry_valid = 0;

    /* These bits were emitted only after native player physics. Remove them
     * when a scripted handoff preempts the normal next-frame input refresh. */
    io_controller_hold1 &= (uint8_t)~0x44;  /* translated Y and Down */
    io_controller_press1 &= (uint8_t)~0x40;
}

static void smw_falcon_emit_carry_input(void)
{
    if (!s_foreign_pad.carry_valid) return;

    /* SMWDisX $01:AA42 owns pickup eligibility and changes a valid sprite to
     * native status $0B. Its $01:9F9B carried lifecycle subsequently reads
     * Y/Down: A held means Y held; A released means native throw, with Down
     * retained only for native set-down. Physical Y never enters this bridge. */
    io_controller_hold1 &= (uint8_t)~0x44;
    io_controller_press1 &= (uint8_t)~0x40;
    if (s_foreign_pad.carry_a) {
        io_controller_hold1 |= 0x40;
    } else if (player_carrying_something_flag1 != 0 &&
               s_foreign_pad.carry_down) {
        io_controller_hold1 |= 0x04;
    }
}

static void smw_falcon_adapt_water_motion(ForeignMoveResult *move)
{
    double vertical;
    if (!flag_underwater_level) return;

    /* Source +Y is up. Clamp only falling speed, then scale every vertical
     * velocity so its gravity also feels buoyant at the SMW boundary. */
    vertical = move->requested_dy;
    if (vertical < -SMW_FALCON_WATER_TERMINAL_FALL)
        vertical = -SMW_FALCON_WATER_TERMINAL_FALL;
    move->requested_dy = vertical * SMW_FALCON_WATER_VERTICAL_SCALE;
    move->vy = move->requested_dy;

    /* Never let SMW's swim-button branch create a separate movement model. */
    player_can_jump_out_of_water = 0;
}

static void smw_falcon_reseed(ForeignState *state)
{
    state->x = (double)player_xpos * SMW_TO_FALCON;
    state->y = -(double)player_ypos * SMW_TO_FALCON;
    state->vx = (double)signed8(player_xspeed) /
                (FALCON_TO_SMW_PX * SMW_SPEED_PER_PX);
    state->vy = -(double)signed8(player_yspeed) /
                (FALCON_TO_SMW_PX * SMW_SPEED_PER_PX);
    state->grounded = player_in_air_flag == 0;
    state->air_cause = FOREIGN_AIR_NONE;
    state->facing = player_facing_direction ? 1.0f : -1.0f;
    snes_foreign_trace_note_reseed();
}

static ForeignInput smw_falcon_input(void)
{
    ForeignInput input;
    const uint8_t hold1 = s_foreign_pad.valid ? s_foreign_pad.hold1 :
                                                io_controller_hold1;
    const uint8_t press1 = s_foreign_pad.valid ? s_foreign_pad.press1 :
                                                 io_controller_press1;
    const uint8_t hold2 = s_foreign_pad.valid ? s_foreign_pad.hold2 :
                                                io_controller_hold2;
    const uint8_t press2 = s_foreign_pad.valid ? s_foreign_pad.press2 :
                                                 io_controller_press2;

    memset(&input, 0, sizeof(input));
    int direction = (hold1 & 0x01) ? 1 : (hold1 & 0x02) ? -1 : 0;

    /* $15 is %byetUDLR; $17 is %axlr0000.
     *
     * Smash's analogue tap buffer treats one 0->full stick edge as dash. A
     * D-pad has no walk magnitude, so expose a 0.5 walk on the first tap and
     * a full source stick only for the second same-direction tap within 15
     * frames.  The source Dash->Run transition is otherwise untouched. */
    if (s_dash_first_dir != 0) {
        if (s_dash_tap_age < SMW_FALCON_DASH_DOUBLE_TAP_FRAMES)
            ++s_dash_tap_age;
        else
            s_dash_first_dir = 0;
    }
    if (direction == 0) {
        s_dash_full_hold = 0;
    } else if (direction != s_dash_prev_dir) {
        if (direction == s_dash_first_dir &&
            s_dash_tap_age <= SMW_FALCON_DASH_DOUBLE_TAP_FRAMES) {
            s_dash_full_hold = 1;
            s_dash_first_dir = 0;
            s_dash_tap_age = 0;
        } else {
            s_dash_full_hold = 0;
            s_dash_first_dir = direction;
            s_dash_tap_age = 0;
        }
    }
    s_dash_prev_dir = direction;
    input.stick_x = direction == 0 ? 0.0f :
                    direction * (s_dash_full_hold ? 1.0f : 0.5f);
    input.stick_y = (hold1 & 0x08) ? 1.0f : (hold1 & 0x04) ? -1.0f : 0.0f;
    input.jump_pressed = (press1 & 0x80) != 0; /* PlayStation Cross / SNES B */
    input.jump_held = (hold1 & 0x80) != 0;
    input.down_pressed = (press1 & 0x04) != 0;
    input.attack_pressed = (press2 & 0x40) != 0; /* SNES X: normal */
    input.special_pressed = (press1 & 0x40) != 0; /* PlayStation Square / SNES Y */
    input.raw_buttons = (int)hold1 | ((int)hold2 << 8);
    s_foreign_pad.valid = 0;
    return input;
}

void SmwFalconBeforePlayerPhysics(struct CpuState *cpu)
{
    (void)cpu;
    s_foreign_pad.valid = 0;
    s_foreign_pad.carry_valid = 0;

    /* $00:D5F2 is the action-input seam. Do not move this work to the later
     * $00:DC2D velocity seam: native spin/cape/fire decisions have already
     * happened there. */
    /* Activation enters SCRIPTED and is reclaimed at $00:DC2D. Mask here in
     * either ownership state so that handoff's first playable frame cannot
     * leak B/Y/X/A into native SMW before the later controller tick. */
    if (!snes_foreign_active() || !smw_falcon_playable())
    {
        smw_falcon_reset_dash_taps();
        return;
    }
    smw_falcon_capture_and_mask_input();
    smw_falcon_disable_native_extensions();
}

void SmwFalconBeforePhysics(struct CpuState *cpu)
{
    ForeignState *state;
    ForeignInput input;
    (void)cpu;

    if (!snes_foreign_active()) return;
    if (!smw_falcon_playable()) {
        if (snes_foreign_ownership() == FOREIGN_OWNERSHIP_FOREIGN)
            snes_foreign_set_ownership(FOREIGN_OWNERSHIP_SCRIPTED);
        s_pending = 0;
        s_force_airborne_pending = 0;
        smw_falcon_reset_dash_taps();
        smw_falcon_clear_carry_bridge();
        return;
    }

    state = snes_foreign_state();
    if (!state) return;
    if (snes_foreign_ownership() != FOREIGN_OWNERSHIP_FOREIGN) {
        /* Script/death/pipe/goal handoffs leave native WRAM authoritative.
         * Re-select resets transient move state before controllable play. */
        const char *id = snes_foreign_active()->id;
        if (!snes_foreign_select(id)) return;
        state = snes_foreign_state();
        smw_falcon_reseed(state);
        snes_foreign_set_ownership(FOREIGN_OWNERSHIP_FOREIGN);
    }

    if (state->grounded && player_in_air_flag != 0)
        state->air_cause = signed8(player_yspeed) < 0 ? FOREIGN_AIR_LAUNCHED
                                                       : FOREIGN_AIR_FELL;
    else
        state->air_cause = FOREIGN_AIR_NONE;
    state->x = (double)player_xpos * SMW_TO_FALCON;
    state->y = -(double)player_ypos * SMW_TO_FALCON;
    state->grounded = player_in_air_flag == 0;

    input = smw_falcon_input();
    memset(&s_last_move, 0, sizeof(s_last_move));
    if (!snes_foreign_tick(snes_frame_counter, &input, &s_last_move))
        return;
    smw_falcon_audio_play_events(&s_last_move.audio);
    smw_falcon_adapt_water_motion(&s_last_move);

    s_x_before = player_xpos;
    s_y_before = player_ypos;
    s_pending = 1;
    player_xspeed = clamp_speed(s_last_move.requested_dx, 0);
    player_yspeed = clamp_speed(s_last_move.requested_dy, 1);
    player_sub_xspeed = player_sub_yspeed = 0;
    player_facing_direction = state->facing >= 0.0f;
    s_force_airborne_pending = s_last_move.force_airborne ||
                              state->jump_phase == FOREIGN_JUMP_LAUNCH;
    if (s_force_airborne_pending) {
        /* Ground Falcon Dive begins with near-zero root motion. Give its
         * departure edge one whole native upward pixel so SMW does not keep
         * rediscovering the same floor before the source launch progresses. */
        player_in_air_flag = 1;
        if (signed8(player_yspeed) >= 0)
            player_yspeed = (uint8_t)(int8_t)-16;
    }

    /* $00:DC2D is intentionally velocity/collision ownership only. */
}

void SmwFalconAfterPhysics(struct CpuState *cpu)
{
    ForeignCollisionResult hit;
    const int dx = (int)(int16_t)(player_xpos - s_x_before);
    const int dy = (int)(int16_t)(player_ypos - s_y_before);
    if (!s_pending || snes_foreign_ownership() != FOREIGN_OWNERSHIP_FOREIGN)
    {
        smw_falcon_clear_carry_bridge();
        return;
    }
    memset(&hit, 0, sizeof(hit));
    hit.actual_dx = (double)dx * SMW_TO_FALCON;
    hit.actual_dy = -(double)dy * SMW_TO_FALCON; /* SMW down -> Falcon up */
    hit.grounded = s_force_airborne_pending ? 0 : player_in_air_flag == 0;
    hit.hit_floor = hit.grounded && dy >= 0;
    hit.hit_ceiling = (player_blocked_flags & 0x08) != 0;
    hit.hit_wall = (player_blocked_flags & 0x03) != 0;
    if (cpu != NULL) {
        const ForeignState *state = snes_foreign_state();
        smw_falcon_combat_apply(cpu, &s_last_move.attack,
                                state != NULL ? state->facing : 1.0f, &hit);
    }
    snes_foreign_resolve(&hit);
    snes_foreign_trace_note_native(player_xpos, player_ypos);
    s_pending = 0;
    s_force_airborne_pending = 0;
}

void SmwFalconBeforeNormalSprites(struct CpuState *cpu)
{
    (void)cpu;
    /* ProcessNormalSprites begins at $01:808C. Its first generated entry is
     * $01:80D2, which follows all player input/physics and enters
     * CheckPlayerToNormalSpriteColl ($01:AA42) plus status-$0B carry handlers.
     * It is the first safe bridge point: $00:CD36 is earlier than native
     * climb/door/player interactions, so Down must not be emitted there. */
    if (!snes_foreign_active() || !smw_falcon_playable() ||
        snes_foreign_ownership() != FOREIGN_OWNERSHIP_FOREIGN) {
        smw_falcon_clear_carry_bridge();
        return;
    }
    smw_falcon_emit_carry_input();
}

void SmwFalconOnStateLoaded(void)
{
    /* Native WRAM carries sprite status $0B and the player carry flags in the
     * outer savestate. The bridge is only a one-frame input translation, so
     * never revive a pre-save A/Down decision from static host memory. */
    s_pending = 0;
    s_force_airborne_pending = 0;
    smw_falcon_reset_dash_taps();
    s_foreign_pad.valid = 0;
    smw_falcon_clear_carry_bridge();
    memset(&s_last_move, 0, sizeof(s_last_move));
}

const ForeignAttackHitbox *smw_falcon_last_attack(void)
{
    return &s_last_move.attack;
}

const ForeignAudioEvents *smw_falcon_last_audio(void)
{
    return &s_last_move.audio;
}
