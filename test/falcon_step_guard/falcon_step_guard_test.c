/* Exact mock of the native step-crush seam recorded from save slot 0.
 * This test reaches the adapter hook directly so it stays independent of the
 * broader controller-regression harness. */
#include "../../overrides/falcon/falcon_smw_adapter.h"
#include "../../src/mods/falcon/captain_falcon_foreign.h"
#include "../../src/mods/falcon/falcon_locomotion.h"
#include "types.h"
#include "cpu_state.h"
#include "../../src/variables.h"

#include <stdio.h>
#include <string.h>

uint8 g_ram[0x20000];
int snes_frame_counter;

void CheckPlayerAttackToNormalSpriteColl_029404(CpuState *cpu) { (void)cpu; }
void SpawnBounceSprite(CpuState *cpu) { (void)cpu; }
void smw_falcon_audio_play_events(const ForeignAudioEvents *events)
{
    (void)events;
}
int smw_falcon_presentation_root_delta(const char *animation, float frame,
                                       float *delta_y, float *delta_z)
{
    (void)animation; (void)frame;
    if (delta_y != NULL) *delta_y = 0.0f;
    if (delta_z != NULL) *delta_z = 0.0f;
    return 0;
}

static int fail(const char *message)
{
    fprintf(stderr, "FAIL: %s\n", message);
    return 1;
}

int main(void)
{
    ForeignState *state;

    memset(g_ram, 0, sizeof(g_ram));
    if (!smw_captain_falcon_register() ||
        !snes_foreign_select(SMW_CAPTAIN_FALCON_ID))
        return fail("register/select Falcon");
    misc_game_mode = 0x14;
    player_current_state = 0;
    player_current_power_up = 0;
    player_in_air_flag = 0;
    player_xpos = 0x070F;
    player_ypos = 0x0160;
    snes_foreign_set_ownership(FOREIGN_OWNERSHIP_FOREIGN);

    /* UpdatePlayerSpritePosition's pre-hook snapshots the immediately prior
     * valid coordinate. The f310/f324 live trace supplied the values and
     * $77=$1D signature, not an artificial 59px single-frame movement. */
    ++snes_frame_counter;
    SmwFalconBeforePhysics(NULL);
    state = snes_foreign_state();
    if (state == NULL) return fail("foreign state available");
    state->state = FL_RUN;
    state->grounded = 1;
    player_xpos = 0x074A;
    player_ypos = 0x0160;
    player_xspeed = 0;
    player_yspeed = 0x96;
    player_blocked_flags = 0x1D;
    SmwFalconBeforeCrushCheck(NULL);
    if (player_xpos != 0x070F || player_ypos != 0x0160 ||
        player_xspeed != 0 || player_yspeed != 0 ||
        player_blocked_flags != 0x05)
        return fail("step guard keeps native wall and floor contact ($1D -> $05)");

    /* The same bit pattern cannot shield an airborne / vertically displaced
     * Falcon from a real crush. */
    player_xpos = 0x074A;
    player_ypos = 0x015F;
    player_xspeed = 0x65;
    player_yspeed = 0x96;
    player_blocked_flags = 0x1D;
    state->grounded = 0;
    SmwFalconBeforeCrushCheck(NULL);
    if (player_xpos != 0x074A || player_ypos != 0x015F ||
        player_xspeed != 0x65 || player_yspeed != 0x96 ||
        player_blocked_flags != 0x1D)
        return fail("real vertical crush remains native-owned");

    puts("falcon_step_guard_test: PASS");
    return 0;
}
