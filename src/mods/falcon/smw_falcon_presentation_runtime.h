#pragma once

#include <stddef.h>
#include <stdint.h>

#include "falcon_presentation.h"
#include "snes/ppu.h"

/* Called only after mod_runtime has committed the exact owner-ROM resource. */
void smw_falcon_presentation_activate(const char *owner_rom_path);
void smw_falcon_presentation_reset(void);
/* Call after the host audio mutex exists and before the first game frame. */
void smw_falcon_presentation_audio_ready(void);

/* Configure the narrow player OBJ suppression before PPU draw, then composite
 * the approved owner cache into the PPU-owned frame before presentation. */
void smw_falcon_presentation_prepare_ppu(Ppu *ppu);
void smw_falcon_presentation_present(uint8_t *pixels, size_t pitch,
                                     int width, int height);
int smw_falcon_presentation_is_active(void);
/* Convert SMW PlayerGFXRt's screen origin ($80) to its 32px foot contact. */
float smw_falcon_presentation_foot_anchor_y(int player_screen_y);

/* Exposed for focused host-boundary tests. */
FalconPresentationPose smw_falcon_presentation_pose_for_state(
    int state, unsigned state_frame, float facing);
