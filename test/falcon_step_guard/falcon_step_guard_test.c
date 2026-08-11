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
    (void)frame;
    if (delta_y != NULL) *delta_y = 0.0f;
    if (delta_z != NULL) *delta_z = 0.0f;
    /* FalconDiveEnd1 is the approved SpecialLwBound motion.  A positive
     * cached local-Z sample must become recoil against a right-hand wall. */
    if (animation != NULL && strcmp(animation, "FalconDiveEnd1") == 0) {
        if (delta_z != NULL) *delta_z = 64.0f;
        return 1;
    }
    return 0;
}

static int fail(const char *message)
{
    fprintf(stderr, "FAIL: %s\n", message);
    return 1;
}

static void ground_adapter_frame(uint8_t hold1, uint8_t press1)
{
    io_controller_hold1 = hold1;
    io_controller_press1 = press1;
    io_controller_hold2 = io_controller_press2 = 0;
    ++snes_frame_counter;
    SmwFalconBeforePlayerPhysics(NULL);
    SmwFalconBeforePhysics(NULL);
    /* This fixture models the native floor result between DC2D and CD36. */
    player_in_air_flag = 0;
    player_blocked_flags = 0x04;
    SmwFalconAfterPhysics(NULL);
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
    player_sub_xpos = 0x33;
    player_sub_ypos = 0x55;
    snes_foreign_set_ownership(FOREIGN_OWNERSHIP_FOREIGN);

    /* UpdatePlayerSpritePosition's pre-hook snapshots the immediately prior
     * valid coordinate. The f310/f324 live trace supplied the values and
     * $77=$1D signature, not an artificial 59px single-frame movement. */
    io_controller_hold1 = 0x01; /* continue holding right into the step */
    ++snes_frame_counter;
    SmwFalconBeforePhysics(NULL);
    state = snes_foreign_state();
    if (state == NULL) return fail("foreign state available");
    state->state = FL_RUN;
    state->grounded = 1;
    state->facing = 1.0f;
    player_xpos = 0x074A;
    player_ypos = 0x0160;
    player_sub_xpos = 0xA0;
    player_sub_ypos = 0xB0;
    player_xspeed = 0;
    player_yspeed = 0x96;
    player_blocked_flags = 0x1D;
    SmwFalconBeforeCrushCheck(NULL);
    if (player_xpos != 0x070F || player_ypos != 0x0160 ||
        player_sub_xpos != 0x33 || player_sub_ypos != 0x55 ||
        player_in_air_flag != 0 ||
        player_xspeed != 0 || player_yspeed != 0 ||
        player_blocked_flags != 0x05)
        return fail("step guard keeps native wall and floor contact ($1D -> $05)");

    /* The f324 correction is not enough by itself: held-right must remain a
     * narrow wall stop over following frames, rather than re-integrating
     * Falcon downward through the step's floor.  Deliberately perturb every
     * native coordinate the latch owns to prove it restores the exact saved
     * position/subposition/ground state three times. */
    for (unsigned i = 0; i != 3; ++i) {
        ++snes_frame_counter;
        player_xpos = 0x074A;
        player_ypos = (uint16_t)(0x016F + i);
        player_sub_xpos = (uint8_t)(0xA1 + i);
        player_sub_ypos = (uint8_t)(0xB1 + i);
        player_in_air_flag = 0;
        player_xspeed = 0x65;
        player_yspeed = 0x96;
        player_blocked_flags = 0;
        state->grounded = 1;
        SmwFalconBeforePhysics(NULL);
        if (player_xpos != 0x070F || player_ypos != 0x0160 ||
            player_sub_xpos != 0x33 || player_sub_ypos != 0x55 ||
            player_in_air_flag != 0 || player_xspeed != 0 ||
            player_yspeed != 0 || player_blocked_flags != 0x05)
            return fail("held-right step latch keeps Falcon standing at the wall");
    }

    /* Neutral releases the latch; it must not turn later ordinary motion into
     * blanket position or crush immunity. */
    io_controller_hold1 = 0;
    ++snes_frame_counter;
    player_xpos = 0x0711;
    player_ypos = 0x0160;
    player_in_air_flag = 0;
    player_blocked_flags = 0x04;
    state->grounded = 1;
    SmwFalconBeforePhysics(NULL);
    if (player_xpos != 0x0711)
        return fail("neutral releases the persistent step-wall latch");

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

    /* Ground SpecialLw's source flag1 opens at frame 12. A one-block step
     * produces native $77=$1D before CD36/AfterPhysics, so this one-shot
     * correction must restore the exact DC2D snapshot, retain wall+floor,
     * then let the normal resolver select Ground SpecialLw Bound. No held
     * step latch is allowed: Bound's authored TransN recoil owns next frame. */
    if (!snes_foreign_select(SMW_CAPTAIN_FALCON_ID))
        return fail("reset selected controller for Kick low-step guard");
    SmwFalconOnStateLoaded();
    misc_game_mode = 0x14;
    player_current_state = 0;
    player_in_air_flag = 0;
    player_xpos = 0x070F;
    player_ypos = 0x0160;
    player_sub_xpos = 0x33;
    player_sub_ypos = 0x55;
    /* Enter Kick, then advance exactly through authored source frame 12. */
    ground_adapter_frame(0x44, 0x44);
    for (unsigned i = 0; i != 12; ++i)
        ground_adapter_frame(0, 0);
    state = snes_foreign_state();
    if (state == NULL || state->state != FL_FALCON_KICK_GROUND ||
        state->state_frame != 12u)
        return fail("Kick reaches authored Ground SpecialLw flag1 frame");

    io_controller_hold1 = io_controller_press1 = 0;
    ++snes_frame_counter;
    SmwFalconBeforePlayerPhysics(NULL);
    SmwFalconBeforePhysics(NULL); /* snapshot and advance to active frame 13 */
    player_xpos = 0x074A;
    player_ypos = 0x0160;
    player_sub_xpos = 0xA0;
    player_sub_ypos = 0xB0;
    player_xspeed = 0x65;
    player_yspeed = 0x96;
    player_blocked_flags = 0x1D;
    SmwFalconBeforeCrushCheck(NULL);
    if (player_xpos != 0x070F || player_ypos != 0x0160 ||
        player_sub_xpos != 0x33 || player_sub_ypos != 0x55 ||
        player_in_air_flag != 0 || player_xspeed != 0 || player_yspeed != 0 ||
        player_blocked_flags != 0x05)
        return fail("active Ground Kick restores low-step snapshot once ($1D -> $05)");
    SmwFalconAfterPhysics(NULL);
    state = snes_foreign_state();
    if (state == NULL || state->state != FL_FALCON_KICK_BOUND ||
        state->grounded)
        return fail("restored low-step wall reaches Ground Kick Bound after CD36");
    {
        const uint16_t safe_wall_x = player_xpos;
    ++snes_frame_counter;
    SmwFalconBeforePlayerPhysics(NULL);
    SmwFalconBeforePhysics(NULL);
        if (player_in_air_flag == 0 || (int8_t)player_xspeed >= 0)
            return fail("Bound projects its authored TransN away from the wall");
        /* Model the following native position integration.  The rebound's
         * first root-motion tick must retreat from the safe face, never cross
         * it as the rejected build did (0745 -> 0767). */
        player_xpos = (uint16_t)(player_xpos + (int8_t)player_xspeed / 16);
        if (player_xpos >= safe_wall_x)
            return fail("Bound next tick remains on the safe side of the wall");
        player_blocked_flags = 0;
        SmwFalconAfterPhysics(NULL);
    }

    puts("falcon_step_guard_test: PASS");
    return 0;
}
