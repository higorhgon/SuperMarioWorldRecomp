#include "captain_falcon_foreign.h"

#include "falcon_locomotion.h"
#include "foreign_controller.h"

#include <string.h>

static FalconFighter s_fighter;

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

static const ForeignController k_captain_falcon = {
    SMW_CAPTAIN_FALCON_ID, "Captain Falcon", cf_reset, cf_tick, cf_resolve,
    cf_state_name,
};

int smw_captain_falcon_register(void)
{
    return snes_foreign_register(&k_captain_falcon);
}
