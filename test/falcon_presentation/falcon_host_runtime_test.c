/* Host seam unit test: source controller state -> owner compositor pose. */
#include "../../src/mods/falcon/smw_falcon_presentation_runtime.h"
#include "../../src/mods/falcon/falcon_locomotion.h"
#include "foreign_controller.h"

#include <stdio.h>
#include <string.h>

uint8_t g_ram[0x20000];

/* Mapping is pure, but link the real host bridge to keep its dependencies honest. */
const ForeignController *snes_foreign_active(void) { return 0; }
ForeignOwnership snes_foreign_ownership(void) { return FOREIGN_OWNERSHIP_NATIVE; }
ForeignState *snes_foreign_state(void) { return 0; }
int smw_falcon_audio_is_active(void) { return 0; }
void smw_falcon_audio_reset(void) {}
int smw_falcon_audio_activate(const char *dir) { (void)dir; return 0; }
void PpuClearOverlayCaptures(Ppu *ppu) { (void)ppu; }
bool PpuBindOverlaySurface(Ppu *ppu, PpuOverlaySource source, uint8_t *p, size_t pitch) { (void)ppu; (void)source; (void)p; (void)pitch; return true; }
bool PpuSetOverlayCapture(Ppu *ppu, PpuOverlaySource source, int x, int y, int w, int h, uint8_t flags) { (void)ppu; (void)source; (void)x; (void)y; (void)w; (void)h; (void)flags; return true; }
bool PpuSetOverlayOamRange(Ppu *ppu, uint8_t first, uint8_t count) { (void)ppu; (void)first; (void)count; return true; }

static int expect(int state, FalconPresentationState expected, int facing) {
    FalconPresentationPose pose = smw_falcon_presentation_pose_for_state(state, 37, facing ? 1.0f : -1.0f);
    if (pose.state != expected || pose.frame != 37.0f || pose.facing_right != facing) { fprintf(stderr, "FAIL state %d\n", state); return 0; }
    return 1;
}

static int expect_oam_group_contract(void) {
    OamEnt oam[4];
    OamEnt untouched;
    memset(oam, 0, sizeof(oam));
    oam[0].xpos = 90;  oam[0].ypos = 112; oam[0].charnum = 0x20; oam[0].flags = 0x31;
    oam[1].xpos = 106; oam[1].ypos = 128; oam[1].charnum = 0x21; oam[1].flags = 0x31;
    oam[2].xpos = 177; oam[2].ypos = 199; oam[2].charnum = 0x72; oam[2].flags = 0x44;
    oam[3].xpos = 188; oam[3].ypos = 201; oam[3].charnum = 0x73; oam[3].flags = 0x44;
    untouched = oam[2];
    smw_falcon_presentation_reanchor_oam_group((uint8_t *)oam, 2, 50, 60);
    if (oam[0].xpos != 42 || oam[0].ypos != 52 ||
        oam[1].xpos != 58 || oam[1].ypos != 68 ||
        memcmp(&oam[2], &untouched, sizeof(untouched)) ||
        oam[3].xpos != 188 || oam[3].ypos != 201) {
        fputs("FAIL carried shell OAM group contract\n", stderr);
        return 0;
    }
    return 1;
}

int main(void) {
    if (!expect(FL_WAIT, FALCON_PRESENT_IDLE, 1) || !expect(FL_WALK_FAST, FALCON_PRESENT_WALK, 0) ||
        !expect(FL_RUN, FALCON_PRESENT_RUN, 1) || !expect(FL_JUMP_F, FALCON_PRESENT_JUMP, 0) ||
        !expect(FL_FALL, FALCON_PRESENT_FALL, 1) || !expect(FL_FALCON_PUNCH_GROUND, FALCON_PRESENT_PUNCH, 1) ||
        !expect(FL_FALCON_KICK_GROUND, FALCON_PRESENT_KICK, 0) ||
        !expect(FL_FALCON_KICK_AIR, FALCON_PRESENT_KICK_AIR, 0) || !expect(FL_FALCON_DIVE_AIR, FALCON_PRESENT_DIVE, 1) ||
        !expect(FL_FALCON_DIVE_CATCH, FALCON_PRESENT_DIVE_CATCH, 0) || !expect(FL_FALCON_DIVE_THROW, FALCON_PRESENT_DIVE_THROW, 1) ||
        !expect_oam_group_contract()) return 1;
    puts("falcon_host_runtime: pose mapping and OAM contract PASS");
    return 0;
}
