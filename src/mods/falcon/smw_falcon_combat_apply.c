#include "smw_falcon_combat_apply.h"

#include "smw_falcon_combat_policy.h"
#include "falcon_locomotion.h"

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
#define SMW_MINOR_SPRITE_PROC_INDEX 0x15E9u
#define SMW_MAP16_CURRENT 0x1693u

#define SMW_SPRITE_SLOTS 12u
#define SMW_RAM_SIZE 0x20000u

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

static uint32_t ram_effect_hash(const CpuState *cpu)
{
    /* $0000-$000F is deliberately transactional scratch and restored around
     * the native call. Hash every other guest byte instead of guessing which
     * status-$08 multi-hit table proves an accepted collision. */
    uint32_t hash = 2166136261u;
    unsigned address;
    for (address = SMW_SCRATCH_COUNT; address < SMW_RAM_SIZE; ++address)
        hash = (hash ^ cpu->ram[address]) * 16777619u;
    return hash;
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
    /* $00:CD36 reaches us with DB=$00, while the durable normal-sprite seam
     * at $01:80D2 executes after bank $01's PHK/PLB prologue and has DB=$01.
     * Both are WRAM mirrors; $02 is reserved for the temporary native
     * consequence call below. Do not synthesize a consequence from any other
     * unproven entry state. */
    return cpu != NULL && cpu->ram != NULL && cpu->m_flag == 1 &&
           cpu->x_flag == 1 && (cpu->DB == 0 || cpu->DB == 1) && cpu->D == 0;
}

static void prepare_bank01_call(CpuState *cpu, unsigned slot)
{
    /* The native spin-jump sequence is called from bank $01 with M=X=8 and
     * D=0.  Its absolute $14xx/$15xx accesses use that bank's low-WRAM
     * mirror; $15E9 names the target consumed by $07:FC3B. */
    cpu->P |= 0x30u;
    cpu_p_to_mirrors(cpu);
    cpu->DB = 1;
    cpu->X = (uint16_t)slot;
}

static void prepare_bank02_call(CpuState *cpu, unsigned slot)
{
    /* SpawnBounceSprite is a bank-$02 native consequence and receives the
     * same M1X1/D0 low-WRAM mirror contract as its source caller. */
    cpu->P |= 0x30u;
    cpu_p_to_mirrors(cpu);
    cpu->DB = 2;
    cpu->X = (uint16_t)slot;
}

static int sprite_is_supported_target(const CpuState *cpu, unsigned slot)
{
    const uint8_t status = ram8(cpu, SMW_SPR_STATUS + slot);
    uint8_t id;
    if ((status != 8 && status != 9 && status != 10) ||
        (ram8(cpu, SMW_SPR_TWEAKER_C + slot) & 0x20u) != 0 ||
        (ram8(cpu, SMW_SPR_TWEAKER_D + slot) & 0x02u) != 0) return 0;
    id = ram8(cpu, 0x009Eu + slot);
    if (status == 9 || status == 10) {
        /* Native loose/rolling shell lifecycle. Status $0B is deliberately
         * absent: Falcon-owned carried shells are never combat targets. */
        return id >= 0x04u && id <= 0x07u;
    }
    /* Ordinary status-$08 targets: Koopa families, Goomba/Paragoomba, Buzzy.
     * Bosses, hazards, and unfamiliar sprites remain unsupported. */
    return id <= 0x09u || id == 0x0Fu || id == 0x10u || id == 0x11u;
}

static void invoke_native_sprite_consequence(CpuState *cpu, unsigned slot)
{
    /* This is the source's post-contact spin-jump kill sequence from
     * $01:A938: status-$04 spin-kill plus $07:FC3B's four extended stars,
     * then the native stomp score/SFX transaction.  The omitted contact puff
     * and player bounce are Mario-body side effects, not a target consequence.
     *
     * Unlike Cape $02:9451 (ordinary sprites) and star $02:C7B1 (loose
     * shells), this one native lifecycle intentionally applies to every
     * admitted target, including status-$08 enemies and status-$09/$0A loose
     * shells. $15E9 is the source slot selector used by the star spawner. */
    cpu->ram[SMW_MINOR_SPRITE_PROC_INDEX] = (uint8_t)slot;
    prepare_bank01_call(cpu, slot);
    SprStatus02_Dead_SetNorSprStatus04(cpu);
    prepare_bank01_call(cpu, slot);
    SpawnSpinJumpStars(cpu);
    prepare_bank01_call(cpu, slot);
    CheckPlayerToNormalSpriteColl_01AB46(cpu);
}

/* Falcon Dive's source catch category is a fighter/enemy capture. A loose
 * shell has a different native lifecycle and must not be silently converted
 * into a grab target. */
static int sprite_is_dive_catch_target(const CpuState *cpu, unsigned slot)
{
    return ram8(cpu, SMW_SPR_STATUS + slot) == 8 &&
           sprite_is_supported_target(cpu, slot);
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
    /* Upright targets use the conservative 16x24 union. Native loose shells
     * are 16x16, which still shares the same front/foot contact projection. */
    {
        const double height = ram8(cpu, SMW_SPR_STATUS + slot) == 8 ? 24.0 : 16.0;
        SmwFalconAabb result = { x, y, (double)x + 16.0, (double)y + height };
        return result;
    }
}

static int apply_sprite_targets(CpuState *cpu, const ForeignAttackHitbox *attack,
                                float facing, SmwFalconCombatLedger *ledger)
{
    SmwFalconAabb hit = smw_falcon_attack_world_aabb(
        attack, ram16(cpu, SMW_PLAYER_X), ram16(cpu, SMW_PLAYER_Y), facing);
    unsigned slot;
    int contacts = 0;
    for (slot = 0; slot < SMW_SPRITE_SLOTS; ++slot) {
        SmwFalconCpuSnapshot saved;
        uint8_t scratch[SMW_SCRATCH_COUNT];
        uint8_t current_sprite;
        uint8_t before;
        uint32_t effects_before;
        if ((ledger->hit_slots & (uint16_t)(1u << slot)) != 0 ||
            !sprite_is_supported_target(cpu, slot) ||
            !smw_falcon_aabb_overlaps(hit, sprite_bounds(cpu, slot))) continue;

        before = ram8(cpu, SMW_SPR_STATUS + slot);
        memcpy(scratch, cpu->ram + SMW_SCRATCH_FIRST, sizeof(scratch));
        current_sprite = ram8(cpu, SMW_MINOR_SPRITE_PROC_INDEX);
        save_cpu(cpu, &saved);
        memset(cpu->ram + SMW_SCRATCH_FIRST, 0, sizeof(scratch));
        effects_before = ram_effect_hash(cpu);
        /* Host geometry is the admission decision. This is a post-contact
         * native spin-jump consequence, never a second Mario/cape test. */
        invoke_native_sprite_consequence(cpu, slot);
        memcpy(cpu->ram + SMW_SCRATCH_FIRST, scratch, sizeof(scratch));
        cpu->ram[SMW_MINOR_SPRITE_PROC_INDEX] = current_sprite;
        restore_cpu(cpu, &saved);
        /* A multi-hit native enemy can remain status $08 after accepting this
         * canonical transaction. Its slot is still a contact for this move:
         * record it so lingering Punch/Kick frames cannot replay native SFX,
         * score, or damage against the same target. */
        if (ram8(cpu, SMW_SPR_STATUS + slot) == before &&
            ram_effect_hash(cpu) == effects_before)
            continue;
        ledger->hit_slots |= (uint16_t)(1u << slot);
        ledger->new_hit_slots |= (uint16_t)(1u << slot);
        ++contacts;
    }
    return contacts;
}

static int latch_dive_target(CpuState *cpu, const ForeignAttackHitbox *attack,
                             float facing, SmwFalconCombatLedger *ledger)
{
    SmwFalconAabb hit = smw_falcon_attack_world_aabb(
        attack, ram16(cpu, SMW_PLAYER_X), ram16(cpu, SMW_PLAYER_Y), facing);
    unsigned slot;

    if (ledger->dive_latched_slot >= 0) return 0;
    /* Slot order is the host's deterministic stand-in for Smash's single
     * search_gobj.  Unlike Punch/Kick this stops at the first capture. */
    for (slot = 0; slot < SMW_SPRITE_SLOTS; ++slot) {
        if (!sprite_is_dive_catch_target(cpu, slot) ||
            !smw_falcon_aabb_overlaps(hit, sprite_bounds(cpu, slot))) continue;
        ledger->dive_latched_slot = (int)slot;
        ledger->dive_latched_id = ram8(cpu, 0x009Eu + slot);
        ledger->new_hit_slots |= (uint16_t)(1u << slot);
        return 1;
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

void smw_falcon_combat_ledger_update(SmwFalconCombatLedger *ledger,
                                     int move_state, int attack_active)
{
    /* Landing carries an active aerial Punch into ground physics without a
     * new source move; its target ledger must survive that state spelling. */
    if (move_state == FL_FALCON_PUNCH_AIR)
        move_state = FL_FALCON_PUNCH_GROUND;
    if (ledger == NULL) return;
    /* BattleShip ftCaptainSpecialHiProcCatch stores search_gobj in
     * catch_gobj; Catch and Throw then retain that same identity until
     * ftCaptainSpecialHiThrowSetStatus releases it.  Do not erase the host
     * equivalent merely because these authored states have no hitbox. */
    if (!attack_active && ledger->dive_latched_slot >= 0 &&
        (move_state == FL_FALCON_DIVE_CATCH ||
         move_state == FL_FALCON_DIVE_THROW)) {
        ledger->active = 1;
        ledger->move_state = move_state;
        return;
    }
    if (!attack_active) {
        memset(ledger, 0, sizeof(*ledger));
        ledger->dive_latched_slot = -1;
        return;
    }
    if (!ledger->active || ledger->move_state != move_state) {
        memset(ledger, 0, sizeof(*ledger));
        ledger->active = 1;
        ledger->move_state = move_state;
        ledger->dive_latched_slot = -1;
    }
}

int smw_falcon_combat_apply(CpuState *cpu, const ForeignAttackHitbox *attack,
                            float facing, SmwFalconCombatLedger *ledger,
                            ForeignCollisionResult *out_collision)
{
    int sprite_contacts;
    if (out_collision == NULL || !hook_contract_is_valid(cpu) ||
        attack == NULL || !attack->active || ledger == NULL || !ledger->active)
        return 0;
    ledger->new_hit_slots = 0;
    if ((attack->flags & FOREIGN_ATTACK_CONTACT_ONLY) != 0) {
        sprite_contacts = latch_dive_target(cpu, attack, facing, ledger);
        if (sprite_contacts != 0) {
            out_collision->attack_connected = 1;
            ledger->had_sprite_contact = 1;
        }
        /* A source catch never reaches either the native impact consequence
         * nor the block route.  Throw handles its one accepted target later. */
        return sprite_contacts;
    }
    sprite_contacts = apply_sprite_targets(cpu, attack, facing, ledger);
    if (sprite_contacts != 0) {
        out_collision->attack_connected = 1;
        ledger->had_sprite_contact = 1;
        /* Native sprite contacts own their score/SFX/status transaction. A
         * group may all receive it, but the block route remains separate. */
        return sprite_contacts;
    }
    if (ledger->had_sprite_contact || ledger->block_applied) return 0;
    if (apply_native_block(cpu, attack, facing)) {
        ledger->block_applied = 1;
        return 1;
    }
    return 0;
}

int smw_falcon_combat_release_dive(CpuState *cpu,
                                   SmwFalconCombatLedger *ledger,
                                   ForeignCollisionResult *out_collision)
{
    SmwFalconCpuSnapshot saved;
    uint8_t scratch[SMW_SCRATCH_COUNT];
    uint8_t current_sprite;
    const int latched_slot = ledger != NULL ? ledger->dive_latched_slot : -1;
    int released = 0;

    if (out_collision != NULL) out_collision->attack_connected = 0;
    if (!hook_contract_is_valid(cpu) || ledger == NULL || latched_slot < 0 ||
        latched_slot >= (int)SMW_SPRITE_SLOTS) return 0;

    /* The slot can have died or been reused during Catch.  Match the source
     * GObj ownership rule conservatively: only the exact, still-catchable
     * ordinary target may receive the Throw's native defeat. */
    if (sprite_is_dive_catch_target(cpu, (unsigned)latched_slot) &&
        ram8(cpu, 0x009Eu + latched_slot) == ledger->dive_latched_id) {
        memcpy(scratch, cpu->ram + SMW_SCRATCH_FIRST, sizeof(scratch));
        current_sprite = ram8(cpu, SMW_MINOR_SPRITE_PROC_INDEX);
        save_cpu(cpu, &saved);
        memset(cpu->ram + SMW_SCRATCH_FIRST, 0, sizeof(scratch));
        /* $02:C7B1 is SMW's accepted no-geometry star/kick defeat route.
         * It provides the one host-native consequence corresponding to
         * Falcon Dive's authored 20-damage Throw release. */
        prepare_bank02_call(cpu, (unsigned)latched_slot);
        KillNormalSprite_AcceptedConsequence(cpu);
        memcpy(cpu->ram + SMW_SCRATCH_FIRST, scratch, sizeof(scratch));
        cpu->ram[SMW_MINOR_SPRITE_PROC_INDEX] = current_sprite;
        restore_cpu(cpu, &saved);
        ledger->hit_slots |= (uint16_t)(1u << latched_slot);
        ledger->new_hit_slots |= (uint16_t)(1u << latched_slot);
        if (out_collision != NULL) out_collision->attack_connected = 1;
        released = 1;
    }
    ledger->dive_latched_slot = -1;
    ledger->dive_latched_id = 0;
    return released;
}
