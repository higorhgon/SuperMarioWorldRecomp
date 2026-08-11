#include "smw_falcon_combat_apply.h"

#include "smw_falcon_combat_policy.h"
#include "falcon_locomotion.h"

#include "funcs.h"

#include <math.h>
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
#define SMW_SPR_TWEAKER_B 0x1662u
#define SMW_SPR_TWEAKER_C 0x166Eu
#define SMW_SPR_TWEAKER_D 0x167Au
#define SMW_MINOR_SPRITE_PROC_INDEX 0x15E9u
#define SMW_MAP16_CURRENT 0x1693u
#define SMW_IO_SFX_1DF9 0x1DF9u

#define SMW_SPRITE_SLOTS 12u
#define SMW_RAM_SIZE 0x20000u
#define SMW_FALCON_DIVE_SNAP_MAX_PX 4.0

typedef struct {
    uint16_t A, X, Y, S, D;
    uint8_t DB, PB, host_return_valid, P;
    uint8_t m_flag, x_flag, emulation;
    uint8_t flag_n, flag_v, flag_z, flag_c, flag_i, flag_d;
    uint8_t open_bus;
} SmwFalconCpuSnapshot;

typedef void (*SmwFalconNativeEntry)(CpuState *cpu);

typedef enum {
    SMW_FALCON_SPRITE_CONSEQUENCE_NONE,
    SMW_FALCON_SPRITE_CONSEQUENCE_SPIN,
    SMW_FALCON_SPRITE_CONSEQUENCE_STAR_KILL,
} SmwFalconSpriteConsequence;

typedef struct {
    uint8_t index;
    int8_t x_offset, y_offset;
    uint8_t width, height;
} SmwFalconNativeSpriteClip;

/* Exact entries from SMW's shared GetSpriteClippingA/B tables:
 * $03:B56C X offset, $03:B5A8 width, $03:B5E4 Y offset, $03:B620 height.
 * $01:A7DC calls GetSpriteClippingA ($03:B69F) before CheckForContact
 * ($03:B72B); Banzai's $02:D587 explicitly JSLs that $01:A7DC body. The
 * oracle's vanilla property rows are $9F: B=$B6/C=$31/D=$01 and
 * $91: B=$0D/C=$0B/D=$F9. B is masked with $3F by GetSpriteClippingA, so
 * Banzai selects index $36 rather than index $00. These are interaction
 * bounds, not drawn tile dimensions. Keep this deliberately small: only the
 * explicitly admitted exceptional source signatures consume this table in the Falcon host
 * boundary. */
static const SmwFalconNativeSpriteClip k_big_target_clips[] = {
    { 0x01u,  2,  3, 12, 21 }, /* Rex ($1F), $81 & $3F */
    { 0x36u,  8,  8, 52, 46 }, /* Banzai Bill ($9F), $B6 & $3F */
    { 0x0Du,  0, -4, 15, 16 }, /* Chargin' Chuck ($91) */
};

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
    /* Bank-$02 native consequences receive the same M1X1/D0 low-WRAM mirror
     * contract as their source callers. */
    cpu->P |= 0x30u;
    cpu_p_to_mirrors(cpu);
    cpu->DB = 2;
    cpu->X = (uint16_t)slot;
}

static void prepare_bank00_call(CpuState *cpu)
{
    /* GenerateTile is a bank-$00 long-return helper. It consumes $98/$9A as
     * the current block coordinate and $9C as a command byte. */
    cpu->P |= 0x30u;
    cpu_p_to_mirrors(cpu);
    cpu->DB = 0;
}

static void write_ram16(CpuState *cpu, unsigned address, uint16_t value)
{
    cpu->ram[address] = (uint8_t)value;
    cpu->ram[address + 1] = (uint8_t)(value >> 8);
}

static void invoke_native_jsr(CpuState *cpu, uint8_t target_bank,
                              SmwFalconNativeEntry entry)
{
    /* A generated void alias still executes the guest RTS. Host glue must
     * supply the two-byte frame that a real bank-local JSR would have pushed;
     * otherwise the alias consumes the live normal-sprite caller's frame. */
    cpu->PB = target_bank;
    cpu_push_jsr_return_frame(cpu);
    entry(cpu);
}

static void invoke_native_jsl(CpuState *cpu, uint8_t target_bank,
                              SmwFalconNativeEntry entry)
{
    /* Cross-bank aliases end in RTL and require their own three-byte frame.
     * In particular, SpawnSpinJumpStars is a $01->$07 JSL in the ROM. */
    cpu->PB = target_bank;
    cpu_push_jsl_return_frame(cpu);
    entry(cpu);
}

static void invoke_native_map16_lookup(CpuState *cpu)
{
    prepare_bank00_call(cpu);
    invoke_native_jsr(cpu, 0, GetPlayerLevelCollisionMap16ID_Entry2);
}

static void invoke_native_brick_pieces(CpuState *cpu)
{
    /* SpawnBrickPieces is the safe native turn-block debris/SFX primitive.
     * It does not run the bounce/content route that can spawn items/enemies.
     * Its generated alias ends in RTL and therefore requires a JSL frame. */
    prepare_bank02_call(cpu, 0);
    cpu->A = 0; /* Same timer argument used by the native brick-break path. */
    invoke_native_jsl(cpu, 2, SpawnBrickPieces);
}

static SmwFalconSpriteConsequence sprite_target_consequence(const CpuState *cpu,
                                                            unsigned slot)
{
    const uint8_t status = ram8(cpu, SMW_SPR_STATUS + slot);
    const uint8_t id = ram8(cpu, 0x009Eu + slot);
    const uint8_t tweaker_b = ram8(cpu, SMW_SPR_TWEAKER_B + slot);
    const uint8_t tweaker_c = ram8(cpu, SMW_SPR_TWEAKER_C + slot);
    const uint8_t tweaker_d = ram8(cpu, SMW_SPR_TWEAKER_D + slot);

    /* These two source sprites opt out of the generic cape/star routes, not
     * of all player damage.  Their exact ID/tweaker/clip signatures keep the
     * exceptional admission narrow and reject ROM-hack variants. */
    if (status == 8 && id == 0x9Fu && tweaker_b == 0xB6u &&
        tweaker_c == 0x31u && tweaker_d == 0x01u)
        return SMW_FALCON_SPRITE_CONSEQUENCE_SPIN;
    if (status == 8 && id == 0x91u && tweaker_b == 0x0Du &&
        tweaker_c == 0x0Bu && tweaker_d == 0xF9u)
        return SMW_FALCON_SPRITE_CONSEQUENCE_STAR_KILL;
    if (status == 8 && id == 0x1Fu && tweaker_b == 0x81u &&
        tweaker_c == 0x4Fu && tweaker_d == 0x02u)
        return SMW_FALCON_SPRITE_CONSEQUENCE_STAR_KILL;

    if ((status != 8 && status != 9 && status != 10) ||
        (tweaker_c & 0x20u) != 0 || (tweaker_d & 0x02u) != 0)
        return SMW_FALCON_SPRITE_CONSEQUENCE_NONE;
    if (status == 9 || status == 10) {
        /* Native loose/rolling shell lifecycle. Status $0B is deliberately
         * absent: Falcon-owned carried shells are never combat targets. */
        return id >= 0x04u && id <= 0x07u
            ? SMW_FALCON_SPRITE_CONSEQUENCE_SPIN
            : SMW_FALCON_SPRITE_CONSEQUENCE_NONE;
    }
    /* Ordinary status-$08 targets: Koopa families, Goomba/Paragoomba, Buzzy.
     * Bosses, hazards, and unfamiliar sprites remain unsupported. */
    return id <= 0x09u || id == 0x0Fu || id == 0x10u || id == 0x11u
        ? SMW_FALCON_SPRITE_CONSEQUENCE_SPIN
        : SMW_FALCON_SPRITE_CONSEQUENCE_NONE;
}

static void invoke_native_sprite_consequence(CpuState *cpu, unsigned slot,
                                             SmwFalconSpriteConsequence consequence)
{
    if (consequence == SMW_FALCON_SPRITE_CONSEQUENCE_STAR_KILL) {
        /* $02:C7B1 is the native post-star-contact defeat transaction.  It
         * has no geometry query; host AABB admission replaces that part of
         * the source path, and the framed JSR preserves its RTS ABI. */
        prepare_bank02_call(cpu, slot);
        invoke_native_jsr(cpu, 2, KillNormalSprite_AcceptedConsequence);
        return;
    }
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
    invoke_native_jsr(cpu, 1, SprStatus02_Dead_SetNorSprStatus04);
    prepare_bank01_call(cpu, slot);
    invoke_native_jsl(cpu, 7, SpawnSpinJumpStars);
    prepare_bank01_call(cpu, slot);
    invoke_native_jsr(cpu, 1, CheckPlayerToNormalSpriteColl_01AB46);
    /* Exact continuation at $01:A93F after the three framed calls. */
    cpu->ram[SMW_IO_SFX_1DF9] = 8;
}

/* Falcon Dive's source catch category is a fighter/enemy capture. A loose
 * shell has a different native lifecycle and must not be silently converted
 * into a grab target. */
static int sprite_is_dive_catch_target(const CpuState *cpu, unsigned slot)
{
    return ram8(cpu, SMW_SPR_STATUS + slot) == 8 &&
           sprite_target_consequence(cpu, slot) ==
               SMW_FALCON_SPRITE_CONSEQUENCE_SPIN &&
           ram8(cpu, 0x009Eu + slot) != 0x9Fu;
}

static uint16_t sprite_xpos(const CpuState *cpu, unsigned slot)
{
    return (uint16_t)(ram8(cpu, SMW_SPR_X_LO + slot) |
                      ((uint16_t)ram8(cpu, SMW_SPR_X_HI + slot) << 8));
}

static uint16_t sprite_ypos(const CpuState *cpu, unsigned slot)
{
    return (uint16_t)(ram8(cpu, SMW_SPR_Y_LO + slot) |
                      ((uint16_t)ram8(cpu, SMW_SPR_Y_HI + slot) << 8));
}

static SmwFalconAabb sprite_bounds(const CpuState *cpu, unsigned slot)
{
    uint16_t x;
    uint16_t y;
    /* $00D8/$14D4 and $00E4/$14E0 are low/high position tables. */
    x = sprite_xpos(cpu, slot);
    y = sprite_ypos(cpu, slot);
    const uint8_t id = ram8(cpu, 0x009Eu + slot);
    const uint8_t clip_index = ram8(cpu, SMW_SPR_TWEAKER_B + slot) & 0x3Fu;
    unsigned i;
    for (i = 0; i < sizeof(k_big_target_clips) / sizeof(k_big_target_clips[0]); ++i) {
        const SmwFalconNativeSpriteClip *clip = &k_big_target_clips[i];
        if (clip->index == clip_index &&
            ((id == 0x1Fu && clip_index == 0x01u) ||
             (id == 0x9Fu && clip_index == 0x36u) ||
             (id == 0x91u && clip_index == 0x0Du))) {
            SmwFalconAabb result = {
                (double)(int32_t)x + clip->x_offset,
                (double)(int32_t)y + clip->y_offset,
                (double)(int32_t)x + clip->x_offset + clip->width,
                (double)(int32_t)y + clip->y_offset + clip->height,
            };
            return result;
        }
    }
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
        SmwFalconSpriteConsequence consequence;
        consequence = sprite_target_consequence(cpu, slot);
        if ((ledger->hit_slots & (uint16_t)(1u << slot)) != 0 ||
            consequence == SMW_FALCON_SPRITE_CONSEQUENCE_NONE ||
            !smw_falcon_aabb_overlaps(hit, sprite_bounds(cpu, slot))) continue;

        before = ram8(cpu, SMW_SPR_STATUS + slot);
        memcpy(scratch, cpu->ram + SMW_SCRATCH_FIRST, sizeof(scratch));
        current_sprite = ram8(cpu, SMW_MINOR_SPRITE_PROC_INDEX);
        save_cpu(cpu, &saved);
        memset(cpu->ram + SMW_SCRATCH_FIRST, 0, sizeof(scratch));
        effects_before = ram_effect_hash(cpu);
        /* Host geometry is the admission decision. This is a post-contact
         * native spin-jump consequence, never a second Mario/cape test. */
        invoke_native_sprite_consequence(cpu, slot, consequence);
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

static SmwFalconMap16Class map16_low_class(uint8_t map16_low)
{
    if (map16_low == 0x1Eu) return SMW_FALCON_MAP16_TURN_BLOCK;
    return SMW_FALCON_MAP16_UNKNOWN;
}

static int block_intersects_attack(const SmwFalconAabb *hit, int x, int y)
{
    SmwFalconAabb block = {
        (double)x, (double)y, (double)x + 16.0, (double)y + 16.0
    };
    return smw_falcon_aabb_overlaps(*hit, block);
}

static SmwFalconAabb block_break_volume(const ForeignAttackHitbox *attack,
                                        int move_state,
                                        double player_x, double player_y,
                                        double facing)
{
    SmwFalconAabb hit = smw_falcon_attack_world_aabb(
        attack, player_x, player_y, facing);
    if (move_state == FL_FALCON_KICK_AIR ||
        move_state == FL_FALCON_KICK_GROUND_AIR) {
        const double forward = facing < 0.0 ? -32.0 : 32.0;
        /* Block breaking follows the fiery boot's platformer path, not the
         * compact enemy hitbox.  A short-hop DownSpecialAir should carve the
         * next few down-forward tiles it visibly passes through, producing a
         * diagonal crater without re-widening sprite damage. */
        if (forward > 0.0) hit.right += forward;
        else hit.left += forward;
        hit.bottom += 48.0;
    } else if (move_state == FL_FALCON_PUNCH_GROUND ||
               move_state == FL_FALCON_PUNCH_AIR ||
               move_state == FL_FALCON_KICK_GROUND) {
        /* Give ground specials enough vertical tolerance to clear a line of
         * same-row yellow blocks even when native collision quantizes Falcon
         * one pixel above or below the tile edge. */
        hit.top -= 8.0;
        hit.bottom += 8.0;
    }
    return hit;
}

static int clean_break_block_at(CpuState *cpu, int x, int y)
{
    write_ram16(cpu, SMW_TOUCH_X, (uint16_t)x);
    write_ram16(cpu, SMW_TOUCH_Y, (uint16_t)y);
    invoke_native_brick_pieces(cpu);
    cpu->ram[SMW_MAP16_GENERATE] = 1;
    prepare_bank00_call(cpu);
    invoke_native_jsl(cpu, 0, GenerateTile);
    return 1;
}

static int apply_native_blocks(CpuState *cpu, const ForeignAttackHitbox *attack,
                               float facing, int move_state)
{
    SmwFalconCpuSnapshot saved;
    uint8_t scratch[SMW_SCRATCH_COUNT];
    uint8_t interaction[5];
    uint8_t player_y_speed[2];
    uint8_t map16_current;
    SmwFalconAabb hit;
    int start_x, end_x, start_y, end_y;
    int x, y, broken = 0;

    if (attack == NULL ||
        (attack->flags & FOREIGN_ATTACK_BREAK_BLOCKS) == 0)
        return 0;

    memcpy(scratch, cpu->ram + SMW_SCRATCH_FIRST, sizeof(scratch));
    memcpy(interaction, cpu->ram + SMW_TOUCH_Y, sizeof(interaction));
    memcpy(player_y_speed, cpu->ram + 0x007Cu, sizeof(player_y_speed));
    map16_current = cpu->ram[SMW_MAP16_CURRENT];
    save_cpu(cpu, &saved);

    hit = block_break_volume(attack, move_state,
        ram16(cpu, SMW_PLAYER_X), ram16(cpu, SMW_PLAYER_Y), facing);
    start_x = ((int)floor(hit.left)) & ~15;
    end_x = ((int)floor(hit.right - 0.001)) & ~15;
    start_y = ((int)floor(hit.top)) & ~15;
    end_y = ((int)floor(hit.bottom - 0.001)) & ~15;

    /* A Falcon special is an authored destructive volume, not Mario's single
     * collision probe.  Scan every overlapped 16px tile and blank only the
     * confirmed destructible turn-block class.  Re-query Map16 after each
     * GenerateTile call so adjacent blocks observe the mutated level state. */
    for (y = start_y; y <= end_y; y += 16) {
        for (x = start_x; x <= end_x; x += 16) {
            SmwFalconMap16Class cls;
            if (x < 0 || y < 0 || y >= 0x01B0 ||
                !block_intersects_attack(&hit, x, y))
                continue;
            write_ram16(cpu, SMW_TOUCH_X, (uint16_t)x);
            write_ram16(cpu, SMW_TOUCH_Y, (uint16_t)y);
            invoke_native_map16_lookup(cpu);
            cls = map16_low_class(cpu->ram[SMW_MAP16_CURRENT]);
            if (!smw_falcon_can_break_map16(attack, cls)) continue;
            broken += clean_break_block_at(cpu, x, y);
        }
    }

    if (broken == 0) {
        SmwFalconMap16Class cls;
        unsigned cx, cy;
        memcpy(cpu->ram + SMW_SCRATCH_FIRST, scratch, sizeof(scratch));
        memcpy(cpu->ram + SMW_TOUCH_Y, interaction, sizeof(interaction));
        memcpy(cpu->ram + 0x007Cu, player_y_speed, sizeof(player_y_speed));
        cpu->ram[SMW_MAP16_CURRENT] = map16_current;
        restore_cpu(cpu, &saved);
        cls = native_collision_block_class(cpu);
        cx = ram16(cpu, SMW_TOUCH_X) & ~15u;
        cy = ram16(cpu, SMW_TOUCH_Y) & ~15u;
        if (smw_falcon_can_break_map16(attack, cls) &&
            block_intersects_attack(&hit, (int)cx, (int)cy))
            broken += clean_break_block_at(cpu, (int)cx, (int)cy);
    }

    memcpy(cpu->ram + SMW_SCRATCH_FIRST, scratch, sizeof(scratch));
    memcpy(cpu->ram + SMW_TOUCH_Y, interaction, sizeof(interaction));
    memcpy(cpu->ram + 0x007Cu, player_y_speed, sizeof(player_y_speed));
    cpu->ram[SMW_MAP16_CURRENT] = map16_current;
    restore_cpu(cpu, &saved);
    return broken;
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
    if (ledger->had_sprite_contact) return 0;
    if (apply_native_blocks(cpu, attack, facing, ledger->move_state)) {
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
        invoke_native_jsr(cpu, 2, KillNormalSprite_AcceptedConsequence);
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

int smw_falcon_combat_dive_snap_delta(const CpuState *cpu,
                                      const SmwFalconCombatLedger *ledger,
                                      int *out_dx, int *out_dy)
{
    const int slot = ledger != NULL ? ledger->dive_latched_slot : -1;
    int dx, dy;
    double length;

    if (out_dx != NULL) *out_dx = 0;
    if (out_dy != NULL) *out_dy = 0;
    if (cpu == NULL || cpu->ram == NULL || ledger == NULL || slot < 0 ||
        slot >= (int)SMW_SPRITE_SLOTS ||
        !sprite_is_dive_catch_target(cpu, (unsigned)slot) ||
        ram8(cpu, 0x009Eu + (unsigned)slot) != ledger->dive_latched_id)
        return 0;

    /* BattleShip's ftCommonCaptureCaptainUpdatePositions pulls both Captain
     * and his captured fighter toward the joint-29/TopN capture anchor and
     * caps each source displacement at 180 units.  We do not have the
     * captured fighter's state machine in SMW, so reproduce only Captain's
     * half: align the 16x32 player centre with the captured 16x24 centre.
     * 180 source units scale to 14.4 host pixels; use four whole pixels --
     * one quarter of that cap and below a native 16px tile -- before SMW's
     * ordinary physics/collision pass can correct the move. */
    dx = (int)(int16_t)(sprite_xpos(cpu, (unsigned)slot) -
                        ram16(cpu, SMW_PLAYER_X));
    dy = (int)(int16_t)(sprite_ypos(cpu, (unsigned)slot) - 4u -
                        ram16(cpu, SMW_PLAYER_Y));
    length = sqrt((double)dx * (double)dx + (double)dy * (double)dy);
    if (length > SMW_FALCON_DIVE_SNAP_MAX_PX) {
        dx = (int)((double)dx * SMW_FALCON_DIVE_SNAP_MAX_PX / length);
        dy = (int)((double)dy * SMW_FALCON_DIVE_SNAP_MAX_PX / length);
    }
    if (out_dx != NULL) *out_dx = dx;
    if (out_dy != NULL) *out_dy = dy;
    return dx != 0 || dy != 0;
}
