/*
 * Owner-cache Captain Falcon presentation API.
 *
 * This module is a host-only ARGB8888 compositor. It never reads or writes
 * SMW state; callers pass both the pose and the target explicitly. Runtime
 * bytes are owner-generated and remain outside the source tree.
 */
#pragma once

#include <stddef.h>
#include <stdint.h>

typedef struct FalconPresentation FalconPresentation;

typedef enum FalconPresentationState {
    FALCON_PRESENT_IDLE,
    FALCON_PRESENT_WALK,
    FALCON_PRESENT_RUN,
    FALCON_PRESENT_JUMP,
    FALCON_PRESENT_FALL,
    FALCON_PRESENT_PUNCH,
    FALCON_PRESENT_KICK,
    FALCON_PRESENT_DIVE,
    FALCON_PRESENT_DIVE_CATCH,
    FALCON_PRESENT_DIVE_THROW,
    FALCON_PRESENT_JAB, FALCON_PRESENT_FTILT,
    FALCON_PRESENT_NAIR, FALCON_PRESENT_FAIR, FALCON_PRESENT_BAIR,
    FALCON_PRESENT_DAIR
} FalconPresentationState;

typedef struct FalconPresentationPose {
    FalconPresentationState state;
    float frame;
    /* Nonzero faces screen right; zero faces screen left. */
    int facing_right;
} FalconPresentationPose;

typedef struct FalconPresentationTarget {
    uint32_t *framebuffer;
    int width;
    int height;
    /* Row stride in ARGB8888 pixels. It may exceed width. */
    int pitch_pixels;
    /* The model's screen-space foot anchor. */
    float anchor_x;
    float anchor_y;
    /* <= 0 uses the native 32-pixel presentation height. */
    float scale;
} FalconPresentationTarget;

/* Parses only the owner-generated FLCN64B v4 binary. The bytes are copied;
 * the caller may free its input immediately. Returns NULL on malformed input. */
FalconPresentation *falcon_presentation_load_memory(const void *data, size_t size);
FalconPresentation *falcon_presentation_load_file(const char *path);
void falcon_presentation_destroy(FalconPresentation *presentation);

/* Draw a deterministic, pitch-aware ARGB8888 overlay. Returns 1 when a pose
 * was drawn, 0 for an invalid target or an unavailable requested animation. */
int falcon_presentation_draw(const FalconPresentation *presentation,
                             const FalconPresentationPose *pose,
                             const FalconPresentationTarget *target);

/* Samples the previous-to-current root delta from a named v4 animation.
 * Returned values include Captain's 1.05 TopN scale. */
int falcon_presentation_root_delta(const FalconPresentation *presentation,
                                   const char *animation_name, float frame,
                                   float *delta_y, float *delta_z);

/* Returns the v4 animation chosen for a high-level presentation state. */
const char *falcon_presentation_animation(FalconPresentationState state);
