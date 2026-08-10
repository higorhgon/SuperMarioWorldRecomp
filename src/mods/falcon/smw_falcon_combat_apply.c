#include "smw_falcon_combat_apply.h"

#include "smw_falcon_combat_policy.h"

#include "funcs.h"

#include <stdint.h>
#include <string.h>

/* SMWDisX 3390ee1 WRAM contract, mirrored in src/variables.h. */
#define SMW_PLAYER_X 0x0094u
#define SMW_PLAYER_Y 0x0096u
#define SMW_TOUCH_Y  0x0098u
#define SMW_TOUCH_X  0x009Au
#define SMW_MAP16_GENERATE 0x009Cu
#define SMW_SCRATCH_FIRST 0x0000u
#define SMW_SCRATCH_COUNT 16u
#define SMW_SPR_STATUS 0x14C8u
#define SMW_SPR_Y_LO   0x00D8u
#define SMW_SPR_X_LO   0x00E4u
#define SMW_SPR_Y_HI   0x14D4u
#define SMW_SPR_X_HI   0x14E0u
#define SMW_SPR_TWEAKER_C 0x166Eu
#define SMW_SPR_TWEAKER_D 0x167Au
#define SMW_MAP16_CURRENT 0x1693u

#define SMW_SPRITE_SLOTS 12u

typedef struct {
    uint16_t A, X, Y, S, D;
    uint8_t DB, PB, host_return_valid, P;
    uint8_t m_flag, x_flag, emulation;
    uint8_t flag_n, flag_v, flag_z, flag_c, flag_i, flag_d;
    uint8_t open_bus;
} SmwFalconCpuSnapshot;

static uint8_t ram8(const CpuState *cpu, unsigned address)
{
    return cpu->ram[address];
}

static uint16_t ram16(const CpuState *cpu, unsigned address)
{
    return (uint16_t)ram8(cpu, address) | ((uint16_t)ram8(cpu, address + 1) << 8);
}

static void save_cpu(const CpuState *cpu, SmwFalconCpuSnapshot *saved)
{
    saved->A = cpu->A; saved->X = cpu->X; saved->Y = cpu->Y;
    saved->S = cpu->S; saved->D = cpu->D; saved->DB = cpu->DB;
    saved->PB = cpu->PB; saved->host_return_valid = cpu->host_return_valid;
    saved->P = cpu->P; saved->m_flag = cpu->m_flag; saved->x_flag = cpu->x_flag;
    saved->emulation = cpu->emulation; saved->flag_n = cpu->_flag_N;
    saved->flag_v = cpu->_flag_V; saved->flag_z = cpu->_flag_Z;
    saved->flag_c = cpu->_flag_C; saved->flag_i = cpu->_flag_I;
    saved->flag_d = cpu->_flag_D; saved->open_bus = cpu->open_bus;
}

static void restore_cpu(CpuState *cpu, const SmwFalconCpuSnapshot *saved)
{
    cpu->A = saved->A; cpu->X = saved->X; cpu->Y = saved->Y;
    cpu->S = saved->S; cpu->D = saved->D; cpu->DB = saved->DB;
    cpu->PB = saved->PB; cpu->host_return_valid = saved->host_return_valid;
    cpu->P = saved->P; cpu->m_flag = saved->m_flag; cpu->x_flag = saved->x_flag;
    cpu->emulation = saved->emulation; cpu->_flag_N = saved->flag_n;
    cpu->_flag_V = saved->flag_v; cpu->_flag_Z = saved->flag_z;
    cpu->_flag_C = saved->flag_c; cpu->_flag_I = saved->flag_i;
    cpu->_flag_D = saved->flag_d; cpu->open_bus = saved->open_bus;
}

static int hook_contract_is_valid(const CpuState *cpu)
{
    /* The override manifest restricts $00:CD36 to M1X1.  This is after
     * $00:E92B, where SMW's DB and DP remain reset values. Do not synthesize
     * a consequence from an unproven entry state. */
    return cpu != NULL && cpu->ram != NULL && cpu->m_flag == 1 &&
           cpu->x_flag == 1 && cpu->DB == 0 && cpu->D == 0;
}

static void prepare_bank02_call(CpuState *cpu, unsigned slot)
{
    /* $02:9404 and $02:8752 are native M1X1 routines. DB=$02 addresses the
     * $0000-$1FFF WRAM mirror exactly as their original bank-02 callers do. */
    cpu->P |= 0x30u;
    cpu_p_to_mirrors(cpu);
    cpu->DB = 2;
    cpu->X = (uint16_t)slot;
}

static int sprite_is_supported_ordinary(const CpuState *cpu, unsigned slot)
{
    uint8_t id;
    if (ram8(cpu, SMW_SPR_STATUS + slot) != 8 ||
        (ram8(cpu, SMW_SPR_TWEAKER_C + slot) & 0x20u) != 0 ||
        (ram8(cpu, SMW_SPR_TWEAKER_D + slot) & 0x02u) != 0) return 0;
    id = ram8(cpu, 0x009Eu + slot);
    /* Deliberate support allowlist: Koopa/shell families, Goomba/Paragoomba,
     * and Buzzy Beetle. Bosses, hazards, carried entities, and unfamiliar
     * sprites remain host-unsupported rather than guessed. */
    return id <= 0x09u || id == 0x0Fu || id == 0x10u || id == 0x11u;
}

static SmwFalconAabb sprite_bounds(const CpuState *cpu, unsigned slot)
{
    uint16_t x;
    uint16_t y;
    /* $00D8/$14D4 and $00E4/$14E0 are low/high position tables. */
    x = (uint16_t)(ram8(cpu, SMW_SPR_X_LO + slot) |
                   ((uint16_t)ram8(cpu, SMW_SPR_X_HI + slot) << 8));
    y = (uint16_t)(ram8(cpu, SMW_SPR_Y_LO + slot) |
                   ((uint16_t)ram8(cpu, SMW_SPR_Y_HI + slot) << 8));
    /* Every admitted target has native status $08. IDs $04-$07 are upright
     * shelled Koopas, not loose shells; loose shells use $09/$0A and are
     * intentionally excluded by sprite_is_supported_ordinary. Use the proven
     * conservative 16x24 union for all admitted upright enemies. */
    {
        SmwFalconAabb result = { x, y, (double)x + 16.0, (double)y + 24.0 };
        return result;
    }
}

static int apply_one_sprite(CpuState *cpu, const ForeignAttackHitbox *attack,
                            float facing)
{
    SmwFalconAabb hit = smw_falcon_attack_world_aabb(
        attack, ram16(cpu, SMW_PLAYER_X), ram16(cpu, SMW_PLAYER_Y), facing);
    unsigned slot;
    for (slot = 0; slot < SMW_SPRITE_SLOTS; ++slot) {
        SmwFalconCpuSnapshot saved;
        uint8_t scratch[SMW_SCRATCH_COUNT];
        uint8_t before;
        if (!sprite_is_supported_ordinary(cpu, slot) ||
            !smw_falcon_aabb_overlaps(hit, sprite_bounds(cpu, slot))) continue;

        before = ram8(cpu, SMW_SPR_STATUS + slot);
        memcpy(scratch, cpu->ram + SMW_SCRATCH_FIRST, sizeof(scratch));
        save_cpu(cpu, &saved);
        memset(cpu->ram + SMW_SCRATCH_FIRST, 0, sizeof(scratch));
        /* $02:9404 uses $0E as its contact-effect selector; zero is the
         * source's regular (non-extended-sprite) native kill path. */
        prepare_bank02_call(cpu, slot);
        CheckPlayerAttackToNormalSpriteColl_029404(cpu);
        memcpy(cpu->ram + SMW_SCRATCH_FIRST, scratch, sizeof(scratch));
        restore_cpu(cpu, &saved);
        if (ram8(cpu, SMW_SPR_STATUS + slot) != before) return 1;
    }
    return 0;
}

static SmwFalconMap16Class native_collision_block_class(const CpuState *cpu)
{
    /* `$04 == 7` is the proven $00:F17F -> $02:8752 brick action. $1693 is
     * the current Map16 low byte supplied by $00:E92B; $1E is explicitly the
     * SMWDisX turn block value at $00:ECFA/$00:EE62. */
    if (ram8(cpu, 0x0004u) == 7) return SMW_FALCON_MAP16_BRICK;
    if (ram8(cpu, SMW_MAP16_CURRENT) == 0x1Eu)
        return SMW_FALCON_MAP16_TURN_BLOCK;
    return SMW_FALCON_MAP16_UNKNOWN;
}

static int attack_touches_native_collision_block(const CpuState *cpu,
                                                 const ForeignAttackHitbox *attack,
                                                 float facing)
{
    SmwFalconAabb hit = smw_falcon_attack_world_aabb(
        attack, ram16(cpu, SMW_PLAYER_X), ram16(cpu, SMW_PLAYER_Y), facing);
    unsigned x = ram16(cpu, SMW_TOUCH_X) & ~15u;
    unsigned y = ram16(cpu, SMW_TOUCH_Y) & ~15u;
    SmwFalconAabb block = { x, y, (double)x + 16.0, (double)y + 16.0 };
    return smw_falcon_aabb_overlaps(hit, block);
}

static int apply_native_block(CpuState *cpu, const ForeignAttackHitbox *attack,
                              float facing)
{
    SmwFalconCpuSnapshot saved;
    uint8_t scratch[SMW_SCRATCH_COUNT];
    uint8_t interaction[5];
    SmwFalconMap16Class map16_class = native_collision_block_class(cpu);
    uint8_t player_y_speed[2];
    if (!smw_falcon_can_break_map16(attack, map16_class) ||
        !attack_touches_native_collision_block(cpu, attack, facing)) return 0;

    memcpy(scratch, cpu->ram + SMW_SCRATCH_FIRST, sizeof(scratch));
    memcpy(interaction, cpu->ram + SMW_TOUCH_Y, sizeof(interaction));
    memcpy(player_y_speed, cpu->ram + 0x007Cu, sizeof(player_y_speed));
    save_cpu(cpu, &saved);
    /* Preserve $04 and $98-$9C from $00:E92B. SpawnBounceSprite consumes
     * exactly these native collision products, does score/debris/sound and
     * Map16 mutation itself, then we restore transient scratch for CD36. */
    prepare_bank02_call(cpu, 0);
    SpawnBounceSprite(cpu);
    memcpy(cpu->ram + SMW_SCRATCH_FIRST, scratch, sizeof(scratch));
    memcpy(cpu->ram + SMW_TOUCH_Y, interaction, sizeof(interaction));
    memcpy(cpu->ram + 0x007Cu, player_y_speed, sizeof(player_y_speed));
    restore_cpu(cpu, &saved);
    return 1;
}

int smw_falcon_combat_apply(CpuState *cpu, const ForeignAttackHitbox *attack,
                            float facing, ForeignCollisionResult *out_collision)
{
    int sprite_applied;
    if (out_collision == NULL || !hook_contract_is_valid(cpu) ||
        attack == NULL || !attack->active) return 0;
    sprite_applied = apply_one_sprite(cpu, attack, facing);
    if (sprite_applied) {
        out_collision->attack_connected = 1;
        /* A host contact window is deliberately lingered. Its first admitted
         * native sprite result is the whole move's consequence; do not also
         * consume a block under the same frame. */
        return 1;
    }
    return apply_native_block(cpu, attack, facing);
}
