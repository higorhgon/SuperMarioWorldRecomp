/*
 * Host-side movement controllers imported from another game.
 *
 * This module answers how a character wants to move; the host game remains
 * authoritative for collision, native consequences, and presentation.
 */
#pragma once

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    float stick_x;
    float stick_y;
    int jump_pressed;
    int jump_held;
    int down_pressed;
    int attack_pressed;
    int special_pressed;
    int raw_buttons;
} ForeignInput;

typedef enum {
    FOREIGN_OWNERSHIP_NATIVE = 0,
    FOREIGN_OWNERSHIP_FOREIGN = 1,
    FOREIGN_OWNERSHIP_SCRIPTED = 2,
} ForeignOwnership;

typedef int ForeignMoveState;

typedef enum {
    FOREIGN_AIR_NONE = 0,
    FOREIGN_AIR_LAUNCHED = 1,
    FOREIGN_AIR_FELL = 2,
} ForeignAirCause;

typedef enum {
    FOREIGN_JUMP_NONE = 0,
    FOREIGN_JUMP_CHARGING = 1,
    FOREIGN_JUMP_LAUNCH = 2,
} ForeignJumpPhase;

typedef struct {
    ForeignMoveState state;
    unsigned state_frame;
    double x, y;
    double vx, vy;
    float facing;
    int grounded;
    int fast_fall;
    ForeignAirCause air_cause;
    ForeignJumpPhase jump_phase;
} ForeignState;

typedef struct {
    double offset_x, offset_y;
    double width, height;
    double knockback_x, knockback_y;
    int damage;
    uint32_t flags;
    int active;
} ForeignAttackHitbox;

#define FOREIGN_ATTACK_BREAK_BLOCKS 0x00000001u
#define FOREIGN_ATTACK_CONTACT_ONLY 0x00000002u

#define FOREIGN_AUDIO_EVENT_CAPACITY 4
typedef struct { uint32_t cue; int gain_percent; } ForeignAudioEvent;
typedef struct {
    ForeignAudioEvent events[FOREIGN_AUDIO_EVENT_CAPACITY];
    uint32_t count;
} ForeignAudioEvents;

typedef struct {
    double requested_dx, requested_dy;
    double vx, vy;
    ForeignMoveState state;
    int force_airborne;
    ForeignAttackHitbox attack;
    ForeignAudioEvents audio;
} ForeignMoveResult;

typedef struct {
    double actual_dx, actual_dy;
    int grounded;
    int hit_ceiling, hit_floor, hit_wall;
    int attack_connected;
    int has_imposed_vy;
    double imposed_vy;
    uint32_t flags;
} ForeignCollisionResult;

typedef int (*ForeignControllerSerialize)(const ForeignState *state,
                                          uint8_t *dst, uint32_t capacity,
                                          uint32_t *out_size);
typedef int (*ForeignControllerDeserialize)(ForeignState *state,
                                            const uint8_t *src, uint32_t size,
                                            uint32_t version);

typedef struct ForeignController {
    const char *id;
    const char *name;
    void (*reset)(ForeignState *state);
    void (*tick)(ForeignState *state, const ForeignInput *input,
                 ForeignMoveResult *out);
    void (*resolve)(ForeignState *state, const ForeignCollisionResult *hit);
    const char *(*state_name)(ForeignMoveState state);
    uint32_t save_version;
    ForeignControllerSerialize serialize;
    ForeignControllerDeserialize deserialize;
} ForeignController;

int snes_foreign_register(const ForeignController *controller);
int snes_foreign_select(const char *id);
const ForeignController *snes_foreign_active(void);
ForeignState *snes_foreign_state(void);
void snes_foreign_set_ownership(ForeignOwnership ownership);
ForeignOwnership snes_foreign_ownership(void);

#define SNES_FOREIGN_SAVE_MAX_PAYLOAD 4096u
int snes_foreign_save(uint8_t *dst, uint32_t capacity, uint32_t *out_size);
int snes_foreign_load(const uint8_t *src, uint32_t size);

typedef int (*ForeignSweepProbe)(double x, double y, void *user);
void snes_foreign_sweep(double x, double y, double dx, double dy,
                        double max_step, ForeignSweepProbe probe, void *user,
                        ForeignCollisionResult *out);

typedef struct {
    uint64_t frame;
    int32_t raw_buttons;
    float stick_x, stick_y;
    int32_t state;
    uint8_t ownership, grounded, fast_fall;
    uint8_t hit_wall, hit_ceiling, hit_floor, attack_connected;
    uint8_t air_cause, jump_phase, reseeded;
    double x, y, vx, vy;
    double requested_dx, requested_dy, resolved_dx, resolved_dy;
    uint8_t has_imposed_vy;
    uint8_t pad2[7];
    double imposed_vy;
    uint32_t collision_flags;
    int32_t native_x, native_y;
} ForeignTraceEntry;

void snes_foreign_trace_push(const ForeignTraceEntry *entry);
void snes_foreign_trace_note_native(int32_t native_x, int32_t native_y);
void snes_foreign_trace_note_reseed(void);
void snes_foreign_trace_note_flags(uint32_t flags);
int snes_foreign_trace_last(int n, ForeignTraceEntry *dst);
int snes_foreign_trace_count(void);
void snes_foreign_trace_init_dump(void);
int snes_foreign_trace_write_csv(const char *path);

int snes_foreign_tick(uint64_t frame, const ForeignInput *input,
                      ForeignMoveResult *out);
void snes_foreign_resolve(const ForeignCollisionResult *hit);

#ifdef __cplusplus
}
#endif
