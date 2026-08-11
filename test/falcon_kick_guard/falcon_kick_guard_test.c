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
    /* Kick's low forward foot union includes this target. A retained status
     * models the enemy family whose later same-pass collision would hurt. */
    spr_current_status[0] = 8;
    spr_spriteid[0] = 0x0F;
    spr_xpos_lo[0] = 170;
    spr_ypos_lo[0] = 220;

    memset(&cpu, 0, sizeof(cpu));
    cpu.ram = g_ram;
    cpu.m_flag = cpu.x_flag = 1;
    cpu.P = 0x30;

    /* Frame 12 is the authored beginning of Kick's active interval. */
    for (int i = 0; i != 13; ++i)
        frame(i == 0 ? 0x44 : 0, i == 0 ? 0x44 : 0, &cpu);
    if (!smw_falcon_last_attack()->active || s_native_contacts != 1 ||
        spr_current_status[0] != 8 || timer_player_hurt != 0)
        return fail("active Kick records only its accepted retained $08 slot");
    /* $01:80D2 is re-entered once per ordinary slot before that slot's
     * collision check. X=0 is the connected multi-hit target. */
    cpu.X = 0;
    SmwFalconBeforeNormalSprites(&cpu);
    if (timer_player_hurt != 1)
        return fail("connected $08 slot receives its own collision guard");
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
