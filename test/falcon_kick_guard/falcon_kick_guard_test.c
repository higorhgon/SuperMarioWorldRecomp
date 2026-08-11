/* Direct adapter regression for the narrow active-Kick side-damage guard.
 * It intentionally omits the broad pad/dash fixture so this contract stays
 * deterministic even when that larger integration fixture changes. */
#include "../../overrides/falcon/falcon_smw_adapter.h"
#include "../../src/mods/falcon/captain_falcon_foreign.h"
#include "../../src/mods/falcon/falcon_locomotion.h"
#include "types.h"
#include "cpu_state.h"
#include "../../src/variables.h"

#include <stdint.h>
#include <stdio.h>
#include <string.h>

uint8 g_ram[0x20000];
int snes_frame_counter;
static int s_native_contacts;

void CheckPlayerAttackToNormalSpriteColl_029404(CpuState *cpu)
{
    if (cpu == NULL || cpu->m_flag != 1 || cpu->x_flag != 1 ||
        cpu->DB != 2 || cpu->D != 0) return;
    ++s_native_contacts;
    /* A genuine native multi-hit consequence may retain status $08. This
     * persistent SFX-side write is the observed acceptance proof, not a
     * guessed per-enemy timer. */
    ++cpu->ram[0x1DFC];
    /* $02:9404's accepted loose-shell route reaches $02:945B and stores
     * status $02. Keep the ordinary Koopa at $08 to model the later native
     * side-contact pass that needs the exact-slot guard. */
    if (cpu->ram[0x14C8u + (cpu->X & 0xffu)] == 9)
        cpu->ram[0x14C8u + (cpu->X & 0xffu)] = 2;
}

void SpawnBounceSprite(CpuState *cpu) { (void)cpu; }
void smw_falcon_audio_play_events(const ForeignAudioEvents *events)
{
    (void)events;
}
int smw_falcon_presentation_root_delta(const char *animation, float frame,
                                       float *delta_y, float *delta_z)
{
    (void)animation;
    (void)frame;
    if (delta_y != NULL) *delta_y = 0.0f;
    if (delta_z != NULL) *delta_z = 0.0f;
    return 0;
}

static int fail(const char *message)
{
    fprintf(stderr, "FAIL: %s\n", message);
    return 1;
}

static void frame(uint8_t hold1, uint8_t press1, CpuState *cpu)
{
    io_controller_hold1 = hold1;
    io_controller_press1 = press1;
    io_controller_hold2 = io_controller_press2 = 0;
    ++snes_frame_counter;
    SmwFalconBeforePlayerPhysics(NULL);
    SmwFalconBeforePhysics(NULL);
    SmwFalconAfterPhysics(cpu);
}

static void model_later_side_damage(void)
{
    if (timer_player_hurt == 0) player_current_state = 9;
}

int main(void)
{
    CpuState cpu;

    memset(g_ram, 0, sizeof(g_ram));
    if (!smw_captain_falcon_register() ||
        !snes_foreign_select(SMW_CAPTAIN_FALCON_ID))
        return fail("register/select Falcon");
    misc_game_mode = 0x14;
    player_current_state = 0;
    player_in_air_flag = 0;
    player_xpos = 100;
    player_ypos = 200;
    /* Slot 8 is the retained Koopa side-contact. Slot 9 is a loose shell:
     * both are immediately ahead, mirroring save1's native lifecycle mix. */
    spr_current_status[8] = 8;
    spr_spriteid[8] = 0x05;
    spr_xpos_lo[8] = 170;
    spr_ypos_lo[8] = 220;
    spr_current_status[9] = 9;
    spr_spriteid[9] = 0x05;
    spr_xpos_lo[9] = 176;
    spr_ypos_lo[9] = 220;

    memset(&cpu, 0, sizeof(cpu));
    cpu.ram = g_ram;
    cpu.m_flag = cpu.x_flag = 1;
    cpu.P = 0x30;

    /* Advance to source frame 12.  Model the real low ground-collision
     * nonlocal return by deliberately omitting CD36/AfterPhysics on the
     * active frame; $01:80D2 must still apply the consequence before native
     * side damage. */
    for (int i = 0; i != 12; ++i)
        frame(i == 0 ? 0x44 : 0, i == 0 ? 0x44 : 0, &cpu);
    io_controller_hold1 = io_controller_press1 = 0;
    io_controller_hold2 = io_controller_press2 = 0;
    ++snes_frame_counter;
    SmwFalconBeforePlayerPhysics(NULL);
    SmwFalconBeforePhysics(NULL);
    if (!smw_falcon_last_attack()->active || s_native_contacts != 0)
        return fail("active Kick reaches the normal-sprite seam without CD36");
    /* $01:80D2 is re-entered once per ordinary slot before that slot's
     * collision check. X=8 is the connected retained multi-hit target. */
    cpu.X = 8;
    SmwFalconBeforeNormalSprites(&cpu);
    if (s_native_contacts != 2 || spr_current_status[8] != 8 ||
        spr_current_status[9] != 2 || timer_player_hurt != 1)
        return fail("normal-sprite seam destroys shell and guards its Koopa");
    model_later_side_damage();
    if (player_current_state != 0)
        return fail("connected Kick slot prevents its same-pass side damage");

    /* The next slot is behind/unhit. Its entry removes exactly the previous
     * guard, so this later native collision stays dangerous. */
    cpu.X = 1;
    SmwFalconBeforeNormalSprites(&cpu);
    if (timer_player_hurt != 0)
        return fail("Kick guard clears before the following unhit slot");
    model_later_side_damage();
    if (player_current_state != 9)
        return fail("unhit/behind same-pass slot remains native-dangerous");
    puts("falcon_kick_guard_test: PASS");
    return 0;
}
