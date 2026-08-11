#include "captain_falcon_foreign.h"

#include "falcon_locomotion.h"
#include "foreign_controller.h"
#include "smw_falcon_presentation_runtime.h"

#include <limits.h>
#include <string.h>

static FalconFighter s_fighter;

/* These are the exact owner-cache motion names used by the mature NES bridge.
 * The associated TransN track is the source of Falcon Kick's horizontal and
 * air-diagonal travel; no velocity is guessed when the cache is absent. */
static const char *cf_root_motion_animation(void)
{
    switch (s_fighter.state) {
    case FL_FALCON_KICK_GROUND:
        return "DownSpecial";
    case FL_FALCON_KICK_GROUND_AIR:
        return (s_fighter.grounded || s_fighter.state_frame < 16.0)
                   ? "VelocityXDownSpecialAir" : NULL;
    case FL_FALCON_KICK_LANDING:
        return "LandingDownSpecial";
    case FL_FALCON_KICK_AIR:
        return "DownSpecialAir";
    case FL_FALCON_KICK_BOUND:
        /* BattleShip ftdata.c's fifth Captain SpecialL motion is named
         * FalconDiveEnd1 in the asset table, but ftcaptain.h identifies that
         * positional slot as SpecialLwBound. It is the authored rebound
         * TransN, not a borrowed Dive-release trajectory. */
        return "FalconDiveEnd1";
    case FL_FALCON_DIVE_GROUND:
        return "FalconDive";
    case FL_FALCON_DIVE_AIR:
        return "FalconDiveEnd2";
    case FL_FALCON_DIVE_THROW:
        return "FalconDiveEnd1";
    default:
        return NULL;
    }
}

static void cf_reset(ForeignState *state)
{
    falcon_reset(&s_fighter);
    memset(state, 0, sizeof(*state));
    state->state = s_fighter.state;
    state->facing = 1.0f;
    state->grounded = 1;
}

static void cf_tick(ForeignState *state, const ForeignInput *input,
                    ForeignMoveResult *out)
{
    FalconInputRaw raw;
    FalconMotion motion;
    const int was_kneebend = s_fighter.state == FL_KNEEBEND;

    memset(&raw, 0, sizeof(raw));
    raw.stick_x = (int)(input->stick_x * 80.0f);
    raw.stick_y = (int)(input->stick_y * 80.0f);
    raw.jump_held = input->jump_held;
    raw.jump_pressed = input->jump_pressed;
    raw.attack_pressed = input->attack_pressed;
    raw.special_pressed = input->special_pressed;
    s_fighter.grounded = state->grounded;
    s_fighter.host_air_cause = (int)state->air_cause;

    falcon_tick(&s_fighter, &raw, &motion);
    {
        const char *animation = cf_root_motion_animation();
        float delta_y, delta_z;

        if (animation != NULL && smw_falcon_presentation_root_delta(
                                     animation, (float)s_fighter.state_frame,
                                     &delta_y, &delta_z)) {
            if (!s_fighter.grounded) {
                const int dive_launch =
                    s_fighter.state == FL_FALCON_DIVE_GROUND ||
                    s_fighter.state == FL_FALCON_DIVE_AIR;
                s_fighter.vel_air_x = (double)delta_z * (double)s_fighter.lr +
                    (dive_launch ? s_fighter.specialhi_vel_x : 0.0);
                s_fighter.vel_air_y = (double)delta_y +
                    (dive_launch ? s_fighter.specialhi_vel_y : 0.0);
                motion.requested_dx = s_fighter.vel_air_x;
                motion.requested_dy = s_fighter.vel_air_y;
            } else {
                s_fighter.vel_ground_x = (double)delta_z;
                motion.requested_dx = (double)delta_z * (double)s_fighter.lr;
                motion.requested_dy = 0.0;
            }
        }
    }
    memset(out, 0, sizeof(*out));
    out->requested_dx = motion.requested_dx;
    out->requested_dy = motion.requested_dy;
    out->vx = s_fighter.grounded ? s_fighter.vel_ground_x * s_fighter.lr
                                 : s_fighter.vel_air_x;
    out->vy = s_fighter.vel_air_y;
    out->state = s_fighter.state;
    out->force_airborne =
        (s_fighter.state == FL_FALCON_DIVE_GROUND &&
         s_fighter.state_frame == 0.0) ||
        (s_fighter.state == FL_FALCON_KICK_BOUND &&
         s_fighter.state_frame == 1.0);
    out->attack.offset_x = motion.attack.offset_x;
    out->attack.offset_y = motion.attack.offset_y;
    out->attack.width = motion.attack.width;
    out->attack.height = motion.attack.height;
    out->attack.knockback_x = motion.attack.knockback_x;
    out->attack.knockback_y = motion.attack.knockback_y;
    out->attack.damage = motion.attack.damage;
    out->attack.flags = (motion.attack.break_blocks ? FOREIGN_ATTACK_BREAK_BLOCKS : 0) |
                        (motion.attack.contact_only ? FOREIGN_ATTACK_CONTACT_ONLY : 0);
    out->attack.active = motion.attack.active;
    for (unsigned cue = 1; cue < FALCON_AUDIO_CUE_COUNT &&
                           out->audio.count < FOREIGN_AUDIO_EVENT_CAPACITY; ++cue) {
        if (motion.audio_cues & FALCON_AUDIO_CUE_BIT(cue)) {
            out->audio.events[out->audio.count].cue = cue;
            out->audio.events[out->audio.count].gain_percent = 100;
            ++out->audio.count;
        }
    }

    state->state = s_fighter.state;
    state->state_frame = (unsigned)s_fighter.state_frame;
    state->facing = (float)s_fighter.lr;
    state->grounded = s_fighter.grounded;
    state->fast_fall = s_fighter.is_fastfall;
    state->vx = out->vx;
    state->vy = out->vy;
    state->jump_phase = s_fighter.state == FL_KNEEBEND ? FOREIGN_JUMP_CHARGING
                      : (was_kneebend && !s_fighter.grounded) ? FOREIGN_JUMP_LAUNCH
                      : FOREIGN_JUMP_NONE;
}

static void cf_resolve(ForeignState *state, const ForeignCollisionResult *hit)
{
    FalconCollision collision;
    memset(&collision, 0, sizeof(collision));
    collision.actual_dx = hit->actual_dx;
    collision.actual_dy = hit->actual_dy;
    collision.grounded = hit->grounded;
    collision.hit_ceiling = hit->hit_ceiling;
    collision.hit_floor = hit->hit_floor;
    collision.hit_wall = hit->hit_wall;
    collision.attack_connected = hit->attack_connected;
    collision.has_imposed_vy = hit->has_imposed_vy;
    collision.imposed_vy = hit->imposed_vy;
    falcon_resolve(&s_fighter, &collision);

    state->x = s_fighter.pos_x;
    state->y = s_fighter.pos_y;
    state->vx = s_fighter.grounded ? s_fighter.vel_ground_x * s_fighter.lr
                                   : s_fighter.vel_air_x;
    state->vy = s_fighter.vel_air_y;
    state->state = s_fighter.state;
    state->state_frame = (unsigned)s_fighter.state_frame;
    state->facing = (float)s_fighter.lr;
    state->grounded = s_fighter.grounded;
    state->fast_fall = s_fighter.is_fastfall;
}

static const char *cf_state_name(ForeignMoveState state)
{
    return falcon_state_name(state);
}

/* The foreign core owns its generic state record.  Falcon's source-accurate
 * locomotion state is controller-private and therefore travels as its bounded
 * optional payload.  Deserialize into a local first so a bad blob cannot
 * partially overwrite the live fighter. */
static int cf_serialize(const ForeignState *state, uint8_t *dst,
                        uint32_t capacity, uint32_t *out_size)
{
    int size;
    (void)state;
    if (!dst || !out_size || capacity > INT_MAX) return 0;
    size = falcon_serialize(&s_fighter, dst, (int)capacity);
    if (size < 0) return 0;
    *out_size = (uint32_t)size;
    return 1;
}

static int cf_deserialize(ForeignState *state, const uint8_t *src,
                          uint32_t size, uint32_t version)
{
    FalconFighter candidate;
    (void)state;
    if (!src || version != 1 || size > INT_MAX) return 0;
    candidate = s_fighter;
    if (!falcon_deserialize(&candidate, src, (int)size)) return 0;
    s_fighter = candidate;
    return 1;
}

static const ForeignController k_captain_falcon = {
    .id = SMW_CAPTAIN_FALCON_ID,
    .name = "Captain Falcon",
    .reset = cf_reset,
    .tick = cf_tick,
    .resolve = cf_resolve,
    .state_name = cf_state_name,
    .save_version = 1,
    .serialize = cf_serialize,
    .deserialize = cf_deserialize,
};

int smw_captain_falcon_register(void)
{
    return snes_foreign_register(&k_captain_falcon);
}
