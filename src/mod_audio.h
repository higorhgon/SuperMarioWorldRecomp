#pragma once

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef int SNESModAudioClip;

#define SNES_MOD_AUDIO_CLIP_INVALID 0
#define SNES_MOD_AUDIO_CHANNELS_MONO 1u
#define SNES_MOD_AUDIO_CHANNELS_STEREO 2u

SNESModAudioClip snes_mod_audio_register_pcm_s16(
    const int16_t *samples, uint32_t frame_count, uint32_t sample_rate,
    uint32_t channels);
void snes_mod_audio_unregister(SNESModAudioClip clip);
int snes_mod_audio_play(SNESModAudioClip clip, int gain_percent);
void snes_mod_audio_stop_all(void);
void snes_mod_audio_mix(int16_t *dst, int frame_count, uint32_t output_rate,
                        uint32_t output_channels);

#ifdef __cplusplus
}
#endif
