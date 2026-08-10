/* Game-owned, portable Captain Falcon controller boundary. */
#ifndef SMW_CAPTAIN_FALCON_CONTROLLER_H
#define SMW_CAPTAIN_FALCON_CONTROLLER_H

#include "falcon_locomotion.h"

#include <stdint.h>

/* Mirrors the planned SNES foreign-controller data flow without depending on
 * that framework header: host owns inputs/collision, Falcon owns moves. */
typedef struct {
    float stick_x; /* normalized -1.0 .. +1.0 */
    float stick_y;
    int jump_held;
    int jump_pressed;
    int attack_pressed;
    int special_pressed;
} SMWForeignInput;

typedef enum {
    SMW_FOREIGN_AIR_NONE = 0,
    SMW_FOREIGN_AIR_LAUNCHED = 1,
    SMW_FOREIGN_AIR_FELL = 2,
} SMWForeignAirCause;

/* Mutable host-facing state. The bridge supplies its current world truth
 * before tick and receives Falcon's intent after tick/resolve. */
typedef struct {
    double x;
    double y;
    double vx;
    double vy;
    float facing;
    int grounded;
    int fast_fall;
    int air_cause;
    int state;
    unsigned state_frame;
} SMWForeignState;

typedef struct {
    double actual_dx;
    double actual_dy;
    int grounded;
    int hit_ceiling;
    int hit_floor;
    int hit_wall;
    int attack_connected;
    int has_imposed_vy;
    double imposed_vy;
} SMWForeignCollision;

typedef struct {
    double requested_dx;
    double requested_dy;
    double vx;
    double vy;
    int state;
    int force_airborne;
    FalconAttack attack;
    uint32_t audio_cues;
} SMWForeignMove;

typedef struct {
    FalconFighter fighter;
} SMWCaptainFalconController;

void smw_captain_falcon_reset(SMWCaptainFalconController *controller,
                              SMWForeignState *state);
void smw_captain_falcon_tick(SMWCaptainFalconController *controller,
                             SMWForeignState *state,
                             const SMWForeignInput *input,
                             SMWForeignMove *out);
void smw_captain_falcon_resolve(SMWCaptainFalconController *controller,
                                SMWForeignState *state,
                                const SMWForeignCollision *collision);
const char *smw_captain_falcon_state_name(int state);
int smw_captain_falcon_serialize(const SMWCaptainFalconController *controller,
                                 uint8_t *buf, int cap);
int smw_captain_falcon_deserialize(SMWCaptainFalconController *controller,
                                   const uint8_t *buf, int len);

#endif
