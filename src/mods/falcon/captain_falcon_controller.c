#include "captain_falcon_controller.h"

#include <string.h>

static int to_source_stick(float stick)
{
    if (stick >= 1.0f) return 80;
    if (stick <= -1.0f) return -80;
    return (int)(stick * 80.0f);
}

void smw_captain_falcon_reset(SMWCaptainFalconController *controller,
                              SMWForeignState *state)
{
    falcon_reset(&controller->fighter);
    memset(state, 0, sizeof(*state));
    state->facing = 1.0f;
    state->grounded = 1;
    state->state = controller->fighter.state;
}

void smw_captain_falcon_tick(SMWCaptainFalconController *controller,
                             SMWForeignState *state,
                             const SMWForeignInput *input,
                             SMWForeignMove *out)
{
    FalconInputRaw raw;
    FalconMotion motion;
    FalconFighter *fighter = &controller->fighter;

    memset(&raw, 0, sizeof(raw));
    raw.stick_x = to_source_stick(input->stick_x);
    raw.stick_y = to_source_stick(input->stick_y);
    raw.jump_held = input->jump_held;
    raw.jump_pressed = input->jump_pressed;
    raw.attack_pressed = input->attack_pressed;
    raw.special_pressed = input->special_pressed;
    fighter->grounded = state->grounded;
    fighter->host_air_cause = state->air_cause;
    falcon_tick(fighter, &raw, &motion);

    memset(out, 0, sizeof(*out));
    out->requested_dx = motion.requested_dx;
    out->requested_dy = motion.requested_dy;
    out->vx = fighter->grounded ? fighter->vel_ground_x * fighter->lr
                                : fighter->vel_air_x;
    out->vy = fighter->vel_air_y;
    out->state = fighter->state;
    out->force_airborne =
        (fighter->state == FL_FALCON_DIVE_GROUND &&
         fighter->state_frame == 0.0) ||
        (fighter->state == FL_FALCON_KICK_BOUND &&
         fighter->state_frame == 1.0);
    out->attack = motion.attack;
    out->audio_cues = motion.audio_cues;

    state->state = fighter->state;
    state->state_frame = (unsigned)fighter->state_frame;
    state->facing = (float)fighter->lr;
    state->grounded = fighter->grounded;
    state->fast_fall = fighter->is_fastfall;
    state->vx = out->vx;
    state->vy = out->vy;
}

void smw_captain_falcon_resolve(SMWCaptainFalconController *controller,
                                SMWForeignState *state,
                                const SMWForeignCollision *collision)
{
    FalconCollision hit;

    memset(&hit, 0, sizeof(hit));
    hit.actual_dx = collision->actual_dx;
    hit.actual_dy = collision->actual_dy;
    hit.grounded = collision->grounded;
    hit.hit_ceiling = collision->hit_ceiling;
    hit.hit_floor = collision->hit_floor;
    hit.hit_wall = collision->hit_wall;
    hit.attack_connected = collision->attack_connected;
    hit.has_imposed_vy = collision->has_imposed_vy;
    hit.imposed_vy = collision->imposed_vy;
    falcon_resolve(&controller->fighter, &hit);

    state->x = controller->fighter.pos_x;
    state->y = controller->fighter.pos_y;
    state->vx = controller->fighter.grounded
                    ? controller->fighter.vel_ground_x * controller->fighter.lr
                    : controller->fighter.vel_air_x;
    state->vy = controller->fighter.vel_air_y;
    state->state = controller->fighter.state;
    state->state_frame = (unsigned)controller->fighter.state_frame;
    state->facing = (float)controller->fighter.lr;
    state->grounded = controller->fighter.grounded;
    state->fast_fall = controller->fighter.is_fastfall;
}

const char *smw_captain_falcon_state_name(int state)
{
    return falcon_state_name(state);
}

int smw_captain_falcon_serialize(const SMWCaptainFalconController *controller,
                                 uint8_t *buf, int cap)
{
    return falcon_serialize(&controller->fighter, buf, cap);
}

int smw_captain_falcon_deserialize(SMWCaptainFalconController *controller,
                                   const uint8_t *buf, int len)
{
    return falcon_deserialize(&controller->fighter, buf, len);
}
