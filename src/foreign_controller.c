#include "foreign_controller.h"

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define FOREIGN_MAX_CONTROLLERS 16
#define FTRING_N 16384
#define FOREIGN_SAVE_MAGIC 0x53434653u
#define FOREIGN_SAVE_FORMAT_VERSION 1u
#define FOREIGN_SAVE_MAX_ID 96u
#define FOREIGN_SAVE_FIXED_SIZE 80u

static const ForeignController *s_controllers[FOREIGN_MAX_CONTROLLERS];
static int s_controller_count;
static const ForeignController *s_active;
static ForeignState s_state;
static ForeignOwnership s_ownership = FOREIGN_OWNERSHIP_NATIVE;
static ForeignTraceEntry s_ftring[FTRING_N];
static uint32_t s_ftring_head;
static const char *s_ftring_dump_path;

static uint64_t double_bits(double value) {
    uint64_t bits;
    memcpy(&bits, &value, sizeof(bits));
    return bits;
}

static double bits_double(uint64_t bits) {
    double value;
    memcpy(&value, &bits, sizeof(value));
    return value;
}

static uint32_t float_bits(float value) {
    uint32_t bits;
    memcpy(&bits, &value, sizeof(bits));
    return bits;
}

static float bits_float(uint32_t bits) {
    float value;
    memcpy(&value, &bits, sizeof(value));
    return value;
}

static void write_u16(uint8_t **dst, uint16_t value) {
    (*dst)[0] = (uint8_t)value;
    (*dst)[1] = (uint8_t)(value >> 8);
    *dst += 2;
}

static void write_u32(uint8_t **dst, uint32_t value) {
    for (int i = 0; i < 4; ++i) (*dst)[i] = (uint8_t)(value >> (i * 8));
    *dst += 4;
}

static void write_u64(uint8_t **dst, uint64_t value) {
    for (int i = 0; i < 8; ++i) (*dst)[i] = (uint8_t)(value >> (i * 8));
    *dst += 8;
}

static uint16_t read_u16(const uint8_t **src) {
    const uint8_t *p = *src;
    *src += 2;
    return (uint16_t)(p[0] | ((uint16_t)p[1] << 8));
}

static uint32_t read_u32(const uint8_t **src) {
    const uint8_t *p = *src;
    uint32_t value = 0;
    for (int i = 0; i < 4; ++i) value |= (uint32_t)p[i] << (i * 8);
    *src += 4;
    return value;
}

static uint64_t read_u64(const uint8_t **src) {
    const uint8_t *p = *src;
    uint64_t value = 0;
    for (int i = 0; i < 8; ++i) value |= (uint64_t)p[i] << (i * 8);
    *src += 8;
    return value;
}

static const ForeignController *find_controller(const char *id, uint32_t id_len) {
    for (int i = 0; i < s_controller_count; ++i) {
        const char *candidate = s_controllers[i]->id;
        if (strlen(candidate) == id_len && memcmp(candidate, id, id_len) == 0)
            return s_controllers[i];
    }
    return NULL;
}

static int controller_has_valid_save_callbacks(const ForeignController *controller) {
    if (!controller) return 1;
    if (!controller->serialize && !controller->deserialize)
        return controller->save_version == 0;
    return controller->serialize && controller->deserialize;
}

static void ftring_clear(void) {
    memset(s_ftring, 0, sizeof(s_ftring));
    s_ftring_head = 0;
}

int snes_foreign_register(const ForeignController *controller) {
    if (!controller || !controller->id || !controller->id[0] || !controller->tick)
        return 0;
    for (int i = 0; i < s_controller_count; ++i) {
        if (strcmp(s_controllers[i]->id, controller->id) == 0)
            return s_controllers[i] == controller;
    }
    if (s_controller_count >= FOREIGN_MAX_CONTROLLERS) return 0;
    s_controllers[s_controller_count++] = controller;
    return 1;
}

int snes_foreign_select(const char *id) {
    if (!id || !id[0]) {
        s_active = NULL;
        memset(&s_state, 0, sizeof(s_state));
        return 1;
    }
    const ForeignController *controller = find_controller(id, (uint32_t)strlen(id));
    if (!controller) return 0;
    s_active = controller;
    memset(&s_state, 0, sizeof(s_state));
    s_state.facing = 1.0f;
    if (s_active->reset) s_active->reset(&s_state);
    return 1;
}

const ForeignController *snes_foreign_active(void) { return s_active; }
ForeignState *snes_foreign_state(void) { return s_active ? &s_state : NULL; }
void snes_foreign_set_ownership(ForeignOwnership ownership) { s_ownership = ownership; }
ForeignOwnership snes_foreign_ownership(void) { return s_ownership; }

int snes_foreign_save(uint8_t *dst, uint32_t capacity, uint32_t *out_size) {
    uint8_t payload[SNES_FOREIGN_SAVE_MAX_PAYLOAD];
    uint32_t payload_size = 0;
    uint32_t id_len = 0;
    uint32_t controller_version = 0;
    if (!dst || !out_size || sizeof(ForeignMoveState) > sizeof(int32_t))
        return 0;
    if (s_ownership < FOREIGN_OWNERSHIP_NATIVE ||
        s_ownership > FOREIGN_OWNERSHIP_SCRIPTED) return 0;
    if (s_active) {
        id_len = (uint32_t)strlen(s_active->id);
        if (id_len == 0 || id_len > FOREIGN_SAVE_MAX_ID ||
            !controller_has_valid_save_callbacks(s_active)) return 0;
        if (s_active->serialize) {
            controller_version = s_active->save_version;
            if (!s_active->serialize(&s_state, payload, sizeof(payload),
                                     &payload_size) ||
                payload_size > sizeof(payload)) return 0;
        }
    }
    const uint64_t total = FOREIGN_SAVE_FIXED_SIZE + (uint64_t)id_len + payload_size;
    if (total > capacity) return 0;

    uint8_t *p = dst;
    write_u32(&p, FOREIGN_SAVE_MAGIC);
    write_u16(&p, FOREIGN_SAVE_FORMAT_VERSION);
    write_u16(&p, (uint16_t)id_len);
    write_u32(&p, (uint32_t)s_ownership);
    write_u32(&p, (uint32_t)(int32_t)s_state.state);
    write_u32(&p, (uint32_t)s_state.state_frame);
    write_u64(&p, double_bits(s_state.x));
    write_u64(&p, double_bits(s_state.y));
    write_u64(&p, double_bits(s_state.vx));
    write_u64(&p, double_bits(s_state.vy));
    write_u32(&p, float_bits(s_state.facing));
    write_u32(&p, (uint32_t)(int32_t)s_state.grounded);
    write_u32(&p, (uint32_t)(int32_t)s_state.fast_fall);
    write_u32(&p, (uint32_t)(int32_t)s_state.air_cause);
    write_u32(&p, (uint32_t)(int32_t)s_state.jump_phase);
    write_u32(&p, controller_version);
    write_u32(&p, payload_size);
    if (id_len) {
        memcpy(p, s_active->id, id_len);
        p += id_len;
    }
    if (payload_size) memcpy(p, payload, payload_size);
    *out_size = (uint32_t)total;
    return 1;
}

int snes_foreign_load(const uint8_t *src, uint32_t size) {
    ForeignState candidate;
    const ForeignController *candidate_controller = NULL;
    const uint8_t *p = src;
    if (!src || size < FOREIGN_SAVE_FIXED_SIZE ||
        sizeof(ForeignMoveState) > sizeof(int32_t)) return 0;
    if (read_u32(&p) != FOREIGN_SAVE_MAGIC ||
        read_u16(&p) != FOREIGN_SAVE_FORMAT_VERSION) return 0;
    uint32_t id_len = read_u16(&p);
    if (id_len > FOREIGN_SAVE_MAX_ID) return 0;
    ForeignOwnership candidate_ownership = (ForeignOwnership)read_u32(&p);
    if (candidate_ownership < FOREIGN_OWNERSHIP_NATIVE ||
        candidate_ownership > FOREIGN_OWNERSHIP_SCRIPTED) return 0;
    memset(&candidate, 0, sizeof(candidate));
    candidate.state = (ForeignMoveState)(int32_t)read_u32(&p);
    candidate.state_frame = (unsigned)read_u32(&p);
    candidate.x = bits_double(read_u64(&p));
    candidate.y = bits_double(read_u64(&p));
    candidate.vx = bits_double(read_u64(&p));
    candidate.vy = bits_double(read_u64(&p));
    candidate.facing = bits_float(read_u32(&p));
    candidate.grounded = (int)(int32_t)read_u32(&p);
    candidate.fast_fall = (int)(int32_t)read_u32(&p);
    candidate.air_cause = (ForeignAirCause)(int32_t)read_u32(&p);
    candidate.jump_phase = (ForeignJumpPhase)(int32_t)read_u32(&p);
    uint32_t controller_version = read_u32(&p);
    uint32_t payload_size = read_u32(&p);
    if (payload_size > SNES_FOREIGN_SAVE_MAX_PAYLOAD ||
        (uint64_t)FOREIGN_SAVE_FIXED_SIZE + id_len + payload_size != size)
        return 0;
    if (id_len == 0) {
        if (controller_version != 0 || payload_size != 0) return 0;
    } else {
        candidate_controller = find_controller((const char *)p, id_len);
        if (!candidate_controller ||
            !controller_has_valid_save_callbacks(candidate_controller)) return 0;
        if (candidate_controller->serialize) {
            if (controller_version != candidate_controller->save_version ||
                !candidate_controller->deserialize(&candidate, p + id_len,
                                                    payload_size, controller_version))
                return 0;
        } else if (controller_version != 0 || payload_size != 0) {
            return 0;
        }
    }
    s_active = candidate_controller;
    s_state = candidate;
    s_ownership = candidate_ownership;
    ftring_clear();
    return 1;
}

void snes_foreign_sweep(double x, double y, double dx, double dy,
                        double max_step, ForeignSweepProbe probe, void *user,
                        ForeignCollisionResult *out) {
    if (!out) return;
    memset(out, 0, sizeof(*out));
    if (!probe) {
        out->actual_dx = dx;
        out->actual_dy = dy;
        return;
    }
    if (!(max_step > 0.0)) max_step = 1.0;
    double distance = fabs(dx) > fabs(dy) ? fabs(dx) : fabs(dy);
    int steps = (int)ceil(distance / max_step);
    if (steps < 1) steps = 1;
    if (steps > 4096) steps = 4096;
    double step_x = dx / (double)steps;
    double step_y = dy / (double)steps;
    double cur_x = x;
    double cur_y = y;
    int blocked_x = 0;
    int blocked_y = 0;
    for (int i = 0; i < steps && !(blocked_x && blocked_y); ++i) {
        if (!blocked_x && step_x != 0.0) {
            if (probe(cur_x + step_x, cur_y, user)) blocked_x = 1;
            else cur_x += step_x;
        }
        if (!blocked_y && step_y != 0.0) {
            if (probe(cur_x, cur_y + step_y, user)) blocked_y = 1;
            else cur_y += step_y;
        }
    }
    out->actual_dx = cur_x - x;
    out->actual_dy = cur_y - y;
    out->hit_wall = blocked_x;
    out->hit_ceiling = blocked_y && dy < 0.0;
    out->hit_floor = blocked_y && dy > 0.0;
    out->grounded = out->hit_floor;
}

void snes_foreign_trace_push(const ForeignTraceEntry *entry) {
    if (!entry) return;
    s_ftring[s_ftring_head++ & (FTRING_N - 1)] = *entry;
}

static ForeignTraceEntry *latest_trace(void) {
    if (s_ftring_head == 0) return NULL;
    return &s_ftring[(s_ftring_head - 1) & (FTRING_N - 1)];
}

void snes_foreign_trace_note_native(int32_t native_x, int32_t native_y) {
    ForeignTraceEntry *entry = latest_trace();
    if (!entry) return;
    entry->native_x = native_x;
    entry->native_y = native_y;
}

void snes_foreign_trace_note_reseed(void) {
    ForeignTraceEntry *entry = latest_trace();
    if (entry) entry->reseeded = 1;
}

void snes_foreign_trace_note_flags(uint32_t flags) {
    ForeignTraceEntry *entry = latest_trace();
    if (entry) entry->collision_flags |= flags;
}

int snes_foreign_trace_last(int n, ForeignTraceEntry *dst) {
    if (!dst || n <= 0) return 0;
    int available = snes_foreign_trace_count();
    if (n > available) n = available;
    uint32_t start = s_ftring_head - (uint32_t)n;
    for (int i = 0; i < n; ++i)
        dst[i] = s_ftring[(start + (uint32_t)i) & (FTRING_N - 1)];
    return n;
}

int snes_foreign_trace_count(void) {
    return s_ftring_head < FTRING_N ? (int)s_ftring_head : FTRING_N;
}

int snes_foreign_trace_write_csv(const char *path) {
    if (!path || !path[0]) return 0;
    FILE *f = fopen(path, "w");
    if (!f) return 0;
    fprintf(f, "frame,raw_buttons,stick_x,stick_y,state,ownership,grounded,"
               "fast_fall,hit_wall,hit_ceiling,hit_floor,attack_connected,"
               "air_cause,jump_phase,reseeded,x,y,vx,vy,requested_dx,"
               "requested_dy,resolved_dx,resolved_dy,has_imposed_vy,"
               "imposed_vy,collision_flags,native_x,native_y\n");
    int count = snes_foreign_trace_count();
    uint32_t start = s_ftring_head - (uint32_t)count;
    for (int i = 0; i < count; ++i) {
        const ForeignTraceEntry *e = &s_ftring[(start + (uint32_t)i) & (FTRING_N - 1)];
        fprintf(f, "%llu,%d,%.6f,%.6f,%d,%u,%u,%u,%u,%u,%u,%u,%u,%u,%u,"
                   "%.9f,%.9f,%.9f,%.9f,%.9f,%.9f,%.9f,%.9f,%u,%.9f,%u,%d,%d\n",
                (unsigned long long)e->frame, e->raw_buttons, e->stick_x,
                e->stick_y, e->state, e->ownership, e->grounded, e->fast_fall,
                e->hit_wall, e->hit_ceiling, e->hit_floor, e->attack_connected,
                e->air_cause, e->jump_phase, e->reseeded, e->x, e->y, e->vx,
                e->vy, e->requested_dx, e->requested_dy, e->resolved_dx,
                e->resolved_dy, e->has_imposed_vy, e->imposed_vy,
                e->collision_flags, e->native_x, e->native_y);
    }
    fclose(f);
    return 1;
}

static void ftring_dump_atexit(void) {
    if (s_ftring_dump_path && s_ftring_dump_path[0])
        (void)snes_foreign_trace_write_csv(s_ftring_dump_path);
}

void snes_foreign_trace_init_dump(void) {
    if (s_ftring_dump_path) return;
    s_ftring_dump_path = getenv("SNESRECOMP_FTRING_DUMP");
    if (s_ftring_dump_path && s_ftring_dump_path[0])
        atexit(ftring_dump_atexit);
}

int snes_foreign_tick(uint64_t frame, const ForeignInput *input,
                      ForeignMoveResult *out) {
    if (!out) return 0;
    memset(out, 0, sizeof(*out));
    ForeignTraceEntry entry;
    memset(&entry, 0, sizeof(entry));
    entry.frame = frame;
    entry.ownership = (uint8_t)s_ownership;
    if (input) {
        entry.raw_buttons = input->raw_buttons;
        entry.stick_x = input->stick_x;
        entry.stick_y = input->stick_y;
    }
    if (!s_active || s_ownership != FOREIGN_OWNERSHIP_FOREIGN) {
        snes_foreign_trace_push(&entry);
        return 0;
    }
    s_active->tick(&s_state, input, out);
    entry.state = out->state;
    entry.grounded = (uint8_t)s_state.grounded;
    entry.fast_fall = (uint8_t)s_state.fast_fall;
    entry.air_cause = (uint8_t)s_state.air_cause;
    entry.jump_phase = (uint8_t)s_state.jump_phase;
    entry.x = s_state.x;
    entry.y = s_state.y;
    entry.vx = out->vx;
    entry.vy = out->vy;
    entry.requested_dx = out->requested_dx;
    entry.requested_dy = out->requested_dy;
    snes_foreign_trace_push(&entry);
    return 1;
}

void snes_foreign_resolve(const ForeignCollisionResult *hit) {
    if (!hit) return;
    ForeignTraceEntry *entry = latest_trace();
    if (entry) {
        entry->resolved_dx = hit->actual_dx;
        entry->resolved_dy = hit->actual_dy;
        entry->hit_wall = (uint8_t)hit->hit_wall;
        entry->hit_ceiling = (uint8_t)hit->hit_ceiling;
        entry->hit_floor = (uint8_t)hit->hit_floor;
        entry->attack_connected = (uint8_t)hit->attack_connected;
        entry->has_imposed_vy = (uint8_t)hit->has_imposed_vy;
        entry->imposed_vy = hit->imposed_vy;
        entry->collision_flags |= hit->flags;
    }
    if (s_active && s_ownership == FOREIGN_OWNERSHIP_FOREIGN && s_active->resolve)
        s_active->resolve(&s_state, hit);
}
