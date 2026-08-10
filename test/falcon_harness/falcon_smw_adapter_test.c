/* Focused host-seam test: raw pad mapping, sign conversion, and native
 * movement/collision feedback without generated SMW sources. */
#include "../../overrides/falcon/falcon_smw_adapter.h"
#include "../../src/mods/falcon/captain_falcon_foreign.h"
#include "../../src/mods/falcon/falcon_locomotion.h"
#include "types.h"
#include "../../src/variables.h"

#include <math.h>
#include <stdio.h>
#include <string.h>

uint8 g_ram[0x20000];
int snes_frame_counter;

static int fail(const char *message)
{
    fprintf(stderr, "FAIL: %s\n", message);
    return 1;
}

int main(void)
{
    ForeignTraceEntry trace;
    int count;

    if (!smw_captain_falcon_register() ||
        !snes_foreign_select(SMW_CAPTAIN_FALCON_ID))
        return fail("register/select Captain Falcon");
    memset(g_ram, 0, sizeof(g_ram));
    misc_game_mode = 0x14;
    player_current_state = 0;
    player_xpos = 100;
    player_ypos = 200;
    player_in_air_flag = 0;
    /* $15=%byetUDLR, $17=%axlr0000. B/Y/X are Falcon-owned; A is reserved
     * rather than accidentally becoming SMW's native spin-jump. */
    io_controller_hold1 = 0xF1;  /* B,Y,Select,Start,Right */
    io_controller_press1 = 0x01; /* Right */
    io_controller_hold2 = 0xC0;  /* A,X */
    io_controller_press2 = 0x80; /* A */
    snes_frame_counter = 1000;

    SmwFalconBeforePhysics(NULL);
    if (snes_foreign_ownership() != FOREIGN_OWNERSHIP_FOREIGN)
        return fail("ordinary level grants foreign ownership");
    if (io_controller_hold1 != 0x30 || io_controller_hold2 != 0 ||
        io_controller_press1 != 0 || io_controller_press2 != 0)
        return fail("B/Y/directions/A/X cleared but Start/Select preserved");
    count = snes_foreign_trace_last(1, &trace);
    if (count != 1 || trace.frame != 1000 || trace.stick_x != 1.0f ||
        trace.stick_y != 0.0f || trace.raw_buttons != 0xC0F1)
        return fail("raw WRAM pad mapping and monotonic frame trace");
    if (trace.state != FL_IDLE || smw_falcon_last_attack()->active)
        return fail("reserved A does not become a Falcon jump or attack");

    /* Emulate native position integration and level collision between seams. */
    player_xpos = (uint16)(player_xpos + 3);
    player_ypos = (uint16)(player_ypos + 2);
    player_blocked_flags = 0x0B; /* wall plus ceiling; grounded yields floor */
    SmwFalconAfterPhysics(NULL);
    if (snes_foreign_trace_last(1, &trace) != 1 ||
        fabs(trace.resolved_dx - 37.5) > 0.001 ||
        fabs(trace.resolved_dy + 25.0) > 0.001 ||
        !trace.hit_wall || !trace.hit_ceiling || !trace.hit_floor)
        return fail("native delta and collision flags resolve with Y inversion");

    player_timer_pipe_warping = 1;
    SmwFalconBeforePhysics(NULL);
    if (snes_foreign_ownership() != FOREIGN_OWNERSHIP_SCRIPTED)
        return fail("pipe handoff is scripted, not foreign");

    /* Grounded Up-B publishes force_airborne. The boundary must consume it
     * instead of leaving a dead result that SMW immediately grounds again. */
    player_timer_pipe_warping = 0;
    player_in_air_flag = 0;
    player_blocked_flags = 0;
    io_controller_hold1 = io_controller_press1 = 0x08; /* Up */
    io_controller_hold2 = io_controller_press2 = 0x40; /* X */
    ++snes_frame_counter;
    SmwFalconBeforePhysics(NULL);
    if (player_in_air_flag == 0 || (int8_t)player_yspeed > -16)
        return fail("grounded Falcon Dive consumes force_airborne upward");
    player_ypos = (uint16)(player_ypos - 1);
    player_in_air_flag = 0; /* emulate native floor rediscovery this frame */
    SmwFalconAfterPhysics(NULL);
    if (snes_foreign_trace_last(1, &trace) != 1 ||
        trace.state != FL_FALCON_DIVE_GROUND || trace.grounded != 0)
        return fail("Up-B startup resolves as airborne after native collision");
    puts("falcon_smw_adapter: pad mapping, seam order, collision feedback PASS");
    return 0;
}
