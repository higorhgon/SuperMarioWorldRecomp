/* Native SMW host boundary for the portable Captain Falcon controller. */
#include "falcon_smw_adapter.h"

#include "types.h"
#include "variables.h"
#include "common_rtl.h"
#include "src/mods/falcon/falcon_locomotion.h"
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
/* Approved owner cache FalconDive TransN is subpixel through source frame 13
 * and first produces an upward SMW speed at frame 14.  The source resolver
 * itself preserves grounded-Dive air kinetics through frame 15. */
#define SMW_FALCON_DEPARTURE_MAX_FRAMES 16u

static int s_pending;
static int s_force_airborne_pending;
static unsigned s_force_airborne_frames;
static int s_dash_first_dir;
static int s_dash_prev_dir;
static int s_dash_full_hold;
static int s_special_grace_pending;
static int s_dash_ignore_until_release;
static unsigned s_dash_tap_age;
static int s_stomp_bounce_armed;
static int s_stomp_contact_guard;
static int s_attack_committed;
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
    s_special_grace_pending = 0;
    s_dash_ignore_until_release = 0;
    s_dash_tap_age = 0;
}

static void smw_falcon_clear_stomp_contact_guard(void)
{
    /* $1497 is SMW's IFrameTimer, checked by the later native/custom-sprite
     * side-damage path. We write only its one-frame value and remove exactly
     * that value at the next early player seam. A real native timer update is
     * never overwritten. */
    if (s_stomp_contact_guard && timer_player_hurt == 1)
        timer_player_hurt = 0;
    s_stomp_contact_guard = 0;
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

/* A source controller's departure request is an edge, but SMW's collision
 * probe can still find the floor while the source animation has not yet
 * accumulated a representable upward displacement.  Keep the established
 * SMW airborne value and a one-pixel upward opportunity live only until the
 * native collision result accepts the lift.  This is the same bounded
 * quantization bridge used by the mature NES host seam, not a repeated input
 * or a controller-owned position write. */
static void smw_falcon_hold_departure_edge(int advance_timeout)
{
    if (!s_force_airborne_pending) return;
    if (advance_timeout &&
        ++s_force_airborne_frames > SMW_FALCON_DEPARTURE_MAX_FRAMES) {
        /* A malformed controller or a true obstruction cannot pin SMW in an
         * artificial airborne state. Native collision regains authority. */
        s_force_airborne_pending = 0;
        s_force_airborne_frames = 0;
        return;
    }
    player_in_air_flag = 1;
    if (signed8(player_yspeed) >= 0)
        player_yspeed = (uint8_t)(int8_t)-16;
}

static int smw_falcon_playable(void)
{
    return misc_game_mode == 0x14 && player_current_state == 0 &&
           player_timer_pipe_warping == 0 && player_pipe_action == 0 &&
           flag_about_to_warp_in_pipe == 0 && timer_end_level == 0 &&
           timer_end_level_via_keyhole == 0;
}

static int smw_falcon_yoshi_lock_active(void)
{
    /* Keep the title/attract demo byte-exact.  In an ordinary level this is
     * deliberately broader than FOREIGN ownership: activation starts
     * SCRIPTED, and a restored Falcon save may reach the Yoshi code before
     * the first controller tick reclaims movement. */
    return snes_foreign_active() != NULL && misc_game_mode == 0x14;
}

static void smw_falcon_dismount_yoshi(void)
{
    unsigned i;

    /* A live rider is encoded by both $187A and the mounted Yoshi's $00C2=1
     * state. Drop both latches, but do not alter sprite status/position or
     * the selected slot ($18DF/$18E2), owned-Yoshi flags ($0DBA/$0DC1),
     * colour/wings, or level-transition metadata. Native SMW therefore keeps
     * the actual Yoshi entity and progression in its off-Yoshi state. */
    player_riding_yoshi_flag = 0;
    for (i = 0; i != 12; ++i) {
        if (spr_spriteid[i] == 0x35 && spr_table00c2[i] == 1)
            spr_table00c2[i] = 0;
    }

    /* These are player-owned tongue startup/visibility timers. */
    timer_yoshi_tongue_is_out = 0;
    timer_yoshi_tongue_init = 0;
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
     * Preserve the persistent owned-Yoshi flags and the level entity. */
    smw_falcon_dismount_yoshi();

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
    const int special_press = (press1 & 0x40) != 0;

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
    if (s_dash_ignore_until_release) {
        /* A turn-start press is the first edge of a deliberate double tap.
         * It remains held through authored Turn frames, so wait for neutral
         * before accepting the second edge, but retain its tap buffer. */
        s_dash_full_hold = 0;
        if (direction == 0) s_dash_ignore_until_release = 0;
    } else {
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
        } else {
            /* Still holding the same source magnitude. */
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
    /* Port the mature NES bridge's one-frame directional-special grace.
     * A directionless Square/Y edge waits one frame so Y then Up still
     * selects Falcon Dive rather than committing Falcon Punch.  Directional
     * edges remain immediate and source-state priority remains authoritative. */
    if (special_press) {
        if (direction != 0 || input.stick_y != 0.0f) {
            input.special_pressed = 1;
            s_special_grace_pending = 0;
        } else {
            s_special_grace_pending = 1;
        }
    } else if (s_special_grace_pending) {
        input.special_pressed = 1;
        s_special_grace_pending = 0;
    }
    input.raw_buttons = (int)hold1 | ((int)hold2 << 8);
    s_foreign_pad.valid = 0;
    return input;
}

void SmwFalconBeforePlayerPhysics(struct CpuState *cpu)
{
    (void)cpu;
    smw_falcon_clear_stomp_contact_guard();
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
    int departure_started = 0;
    (void)cpu;

    /* BoostMarioSpeed runs later in ProcessNormalSprites. Never let its
     * previous-frame observation cross a reset, handoff, or next tick. */
    s_stomp_bounce_armed = 0;
    s_stomp_contact_guard = 0;
    if (!snes_foreign_active()) return;
    if (!smw_falcon_playable()) {
        if (snes_foreign_ownership() == FOREIGN_OWNERSHIP_FOREIGN)
            snes_foreign_set_ownership(FOREIGN_OWNERSHIP_SCRIPTED);
        s_pending = 0;
        s_force_airborne_pending = 0;
        s_force_airborne_frames = 0;
        s_attack_committed = 0;
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

    /* This must precede state->grounded.  A floor rediscovered after the
     * previous tick is a host quantization artefact until native collision
     * has accepted a whole upward lift; feeding it back now would turn a
     * grounded Falcon Dive into a perpetual floor-bound pose. */
    if (s_force_airborne_pending) {
        smw_falcon_hold_departure_edge(1);
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
    if (!s_last_move.attack.active)
        s_attack_committed = 0;
    /* An opposite-facing first press starts a source Turn, not a completed
     * SMW D-pad tap. Suppress it until its eventual neutral release. */
    if (state->state == FL_TURN || state->state == FL_TURN_RUN)
        s_dash_ignore_until_release = 1;
    smw_falcon_audio_play_events(&s_last_move.audio);
    smw_falcon_adapt_water_motion(&s_last_move);

    s_x_before = player_xpos;
    s_y_before = player_ypos;
    s_pending = 1;
    player_xspeed = clamp_speed(s_last_move.requested_dx, 0);
    player_yspeed = clamp_speed(s_last_move.requested_dy, 1);
    player_sub_xspeed = player_sub_yspeed = 0;
    player_facing_direction = state->facing >= 0.0f;
    if ((s_last_move.force_airborne ||
         state->jump_phase == FOREIGN_JUMP_LAUNCH) &&
        !s_force_airborne_pending) {
        s_force_airborne_pending = 1;
        s_force_airborne_frames = 0;
        departure_started = 1;
    }
    /* The controller's source delta was clamped just above. Reassert the
     * already-authorized native departure after that clamp; timeout advances
     * only at the pre-tick presentation, never twice in one guest frame. */
    if (s_force_airborne_pending)
        smw_falcon_hold_departure_edge(departure_started);

    /* $00:DC2D is intentionally velocity/collision ownership only. */
}

void SmwFalconBeforeCrushCheck(struct CpuState *cpu)
{
    const ForeignState *state;
    (void)cpu;

    /* The BLOCK_PATCH in HandlePlayerLevelCollision_M1X1 reaches the inlined
     * SMWDisX $00:E9FB block before it sends $77&$1C==$1C to $00:EA08, which calls
     * DamagePlayer_KillAndDisableButtons.  That exact combination means the
     * movement reached the vertical face of a one-block step while grounded;
     * it is not ordinary head contact.  Falcon's high-speed Dash/Run can
     * reach that branch before the later CD36 seam. Restore the DC2D snapshot
     * and let the original routine take its normal non-crush path, so the
     * step behaves as a solid wall rather than leaving Falcon embedded or
     * granting broad damage immunity. */
    if (!s_pending || !snes_foreign_active() ||
        snes_foreign_ownership() != FOREIGN_OWNERSHIP_FOREIGN ||
        !smw_falcon_playable() ||
        /* Slot 0's step path is wall bit $01 plus exact crush bits $1C;
         * do not turn an airborne/moving-ceiling crush into immunity. */
        (player_blocked_flags & 0x1Du) != 0x1Du ||
        player_ypos != s_y_before) return;
    state = snes_foreign_state();
    if (state == NULL || !state->grounded ||
        (state->state != FL_DASH && state->state != FL_RUN))
        return;

    player_xpos = s_x_before;
    player_ypos = s_y_before;
    player_sub_xspeed = player_sub_yspeed = 0;
    player_xspeed = player_yspeed = 0;
    /* $77 bit $04 is the native floor contact (the valid pre-step value was
     * $04). Clear only incompatible ceiling/crush bits $18: $1D becomes $05,
     * retaining both the wall and floor result for the next frame. */
    player_blocked_flags &= 0xE7u;
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
    /* SMW has accepted this departure only when its own collision result is
     * airborne after a real upward whole-pixel integration.  A one-pixel
     * request that collision immediately re-grounds deliberately remains
     * pending; the next DC2D seam presents the same native airborne state. */
    if (s_force_airborne_pending && dy < 0 && player_in_air_flag != 0 &&
        !hit.hit_ceiling) {
        s_force_airborne_pending = 0;
        s_force_airborne_frames = 0;
    }
    if (cpu != NULL && s_last_move.attack.active && !s_attack_committed) {
        const ForeignState *state = snes_foreign_state();
        if (smw_falcon_combat_apply(cpu, &s_last_move.attack,
                                    state != NULL ? state->facing : 1.0f,
                                    &hit))
            s_attack_committed = 1;
    }
    /* Native normal-sprite collision has not run at $00:CD36 yet. Arm the
     * post-write observer for this one frame so an accepted native stomp can
     * hand its exact $D0/$A8 bounce back to the controller without changing
     * any native contact, damage, score, or sound decision. */
    s_stomp_bounce_armed = 1;
    snes_foreign_resolve(&hit);
    snes_foreign_trace_note_native(player_xpos, player_ypos);
    s_pending = 0;
}

void SmwFalconOnNativeStompBounce(struct CpuState *cpu)
{
    ForeignCollisionResult bounce;
    const uint8_t native_speed = player_yspeed;
    (void)cpu;

    /* SMWDisX $01:AA33 BoostMarioSpeed returns here after a successful native
     * stomp. It writes precisely $D0 (or $A8 while B is held); reject every
     * other call path, including climbing's no-write return. The earlier
     * pad seam normally masks B, but both documented native values remain
     * valid for exact mod-off-compatible semantics. */
    if (!s_stomp_bounce_armed || !snes_foreign_active() ||
        snes_foreign_ownership() != FOREIGN_OWNERSHIP_FOREIGN ||
        (native_speed != 0xD0 && native_speed != 0xA8)) return;

    s_stomp_bounce_armed = 0;
    /* The current normal-sprite pass can dispatch later custom/multi-hit
     * interaction bodies after the native stomp path returns. $1497 is their
     * own established no-hurt guard. Arm exactly one frame only when it was
     * clear, preserving any pre-existing native invulnerability untouched. */
    if (timer_player_hurt == 0) {
        timer_player_hurt = 1;
        s_stomp_contact_guard = 1;
    }
    memset(&bounce, 0, sizeof(bounce));
    bounce.grounded = 0;
    bounce.has_imposed_vy = 1;
    /* SMW stores downward-positive sixteenth-pixel speed; Falcon uses
     * upward-positive source units. This is the inverse of clamp_speed(). */
    bounce.imposed_vy = -(double)signed8(native_speed) /
                        (FALCON_TO_SMW_PX * SMW_SPEED_PER_PX);
    snes_foreign_resolve(&bounce);
}

void SmwFalconBeforeNormalSprites(struct CpuState *cpu)
{
    (void)cpu;
    /* ProcessNormalSprites begins at $01:808C. Its first generated entry is
     * $01:80D2, which follows all player input/physics and enters
     * CheckPlayerToNormalSpriteColl ($01:AA42) plus status-$0B carry handlers.
     * It is the first safe bridge point: $00:CD36 is earlier than native
     * climb/door/player interactions, so Down must not be emitted there. */
    if (snes_foreign_active() != NULL && misc_game_mode == 0x07) {
        /* GameMode07 retains its own mode value while it JMPs into the title
         * demo's level handler.  The demo's recorded side-hit otherwise
         * enters native death and can strand the title-to-start handoff.
         * SMWDisX's collision check treats any nonzero $1497 as invulnerable;
         * PlayerDraw uses a one-frame value without the flicker branch. */
        timer_player_hurt = 1;
        /* Drop only host bridge state.  Do not call clear_carry_bridge here:
         * GM07's native script deliberately uses Y/Down bits (for example
         * $41 = Y+Right), and its controller bytes must remain untouched. */
        s_foreign_pad.carry_a = 0;
        s_foreign_pad.carry_down = 0;
        s_foreign_pad.carry_valid = 0;
        return;
    }
    if (!snes_foreign_active() || !smw_falcon_playable() ||
        snes_foreign_ownership() != FOREIGN_OWNERSHIP_FOREIGN) {
        smw_falcon_clear_carry_bridge();
        return;
    }
    smw_falcon_emit_carry_input();
}

void SmwFalconBeforeYoshi(struct CpuState *cpu)
{
    (void)cpu;
    /* Clear restored C2=1 before Yoshi's earlier mounted fast path. Fresh
     * mount contact is skipped at its precise $01:ECE1 block seam below. */
    if (smw_falcon_yoshi_lock_active()) smw_falcon_dismount_yoshi();
}

int SmwFalconSkipYoshiMount(struct CpuState *cpu)
{
    (void)cpu;
    /* $01:ED38 is after native Yoshi movement and a successful contact test,
     * but before the fresh-mount eligibility/latch path. The generated block
     * patch jumps to $01:ED70, whose C2 check returns because
     * SmwFalconBeforeYoshi cleared C2=1. No player velocity/register/scratch
     * state is changed, so later sprite slots retain exact Falcon movement. */
    return smw_falcon_yoshi_lock_active();
}

void SmwFalconOnStateLoaded(void)
{
    /* Native WRAM carries sprite status $0B and the player carry flags in the
     * outer savestate. The bridge is only a one-frame input translation, so
     * never revive a pre-save A/Down decision or deferred Square/Y edge from
     * static host memory.  The grace latch is intentionally not schema state. */
    s_pending = 0;
    s_force_airborne_pending = 0;
    s_force_airborne_frames = 0;
    s_stomp_bounce_armed = 0;
    s_stomp_contact_guard = 0;
    s_attack_committed = 0;
    smw_falcon_reset_dash_taps();
    s_foreign_pad.valid = 0;
    smw_falcon_clear_carry_bridge();
    memset(&s_last_move, 0, sizeof(s_last_move));
    /* A save can resume a transition before GM14 is reinstated. */
    if (snes_foreign_active() != NULL)
        smw_falcon_dismount_yoshi();
}

const ForeignAttackHitbox *smw_falcon_last_attack(void)
{
    return &s_last_move.attack;
}

const ForeignAudioEvents *smw_falcon_last_audio(void)
{
    return &s_last_move.audio;
}
