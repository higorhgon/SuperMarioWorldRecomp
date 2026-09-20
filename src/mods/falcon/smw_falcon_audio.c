#include "smw_falcon_audio.h"

#include "falcon_locomotion.h"
#include "mod_audio.h"

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define SMW_FALCON_AUDIO_MAX_WAV_BYTES (16u * 1024u * 1024u)
#define SMW_FALCON_AUDIO_PATH_CAP 4096u

static const char *const s_filenames[FALCON_AUDIO_CUE_COUNT - 1] = {
    "falcon_jump_effort.wav",
    "falcon_punch_falcon.wav",
    "falcon_punch_punch.wav",
    "falcon_kick.wav",
    "falcon_punch_impact_fgm.wav",
    "falcon_kick_swing_fgm.wav",
    "falcon_kick_start_fgm.wav",
    "falcon_dive_launch_fgm.wav",
    "falcon_dive_catch_fgm.wav",
    "falcon_dive_explosion_fgm.wav",
    "falcon_dive_voice.wav",
};

static SNESModAudioClip s_clips[FALCON_AUDIO_CUE_COUNT - 1];

static uint16_t read_u16le(const uint8_t *p)
{
    return (uint16_t)p[0] | ((uint16_t)p[1] << 8);
}

static uint32_t read_u32le(const uint8_t *p)
{
    return (uint32_t)p[0] | ((uint32_t)p[1] << 8) |
           ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24);
}

static int read_file_bounded(const char *path, uint8_t **out_data,
                             uint32_t *out_size)
{
    FILE *file;
    long length;
    uint8_t *data;

    *out_data = NULL;
    *out_size = 0;
    file = fopen(path, "rb");
    if (file == NULL || fseek(file, 0, SEEK_END) != 0 ||
        (length = ftell(file)) < 0 || fseek(file, 0, SEEK_SET) != 0 ||
        (uint64_t)length > SMW_FALCON_AUDIO_MAX_WAV_BYTES) {
        if (file != NULL) fclose(file);
        return 0;
    }
    data = (uint8_t *)malloc((size_t)length);
    if (data == NULL || length == 0 ||
        fread(data, 1, (size_t)length, file) != (size_t)length) {
        free(data);
        fclose(file);
        return 0;
    }
    fclose(file);
    *out_data = data;
    *out_size = (uint32_t)length;
    return 1;
}

static int parse_pcm_wave(const uint8_t *file, uint32_t file_size,
                          const int16_t **out_samples, uint32_t *out_frames,
                          uint32_t *out_rate, uint32_t *out_channels)
{
    uint32_t offset = 12;
    uint32_t fmt_offset = 0, data_offset = 0, data_size = 0;
    uint32_t fmt_size = 0;
    int saw_fmt = 0, saw_data = 0;
    uint16_t channels, bits, block_align;
    uint32_t rate, byte_rate;

    if (file_size < 12 || memcmp(file, "RIFF", 4) != 0 ||
        memcmp(file + 8, "WAVE", 4) != 0 ||
        (uint64_t)read_u32le(file + 4) + 8u != file_size) return 0;

    while (offset < file_size) {
        uint32_t chunk_size;
        uint64_t next;
        if (file_size - offset < 8) return 0;
        chunk_size = read_u32le(file + offset + 4);
        next = (uint64_t)offset + 8u + chunk_size + (chunk_size & 1u);
        if (next > file_size) return 0;
        if (memcmp(file + offset, "fmt ", 4) == 0) {
            if (saw_fmt) return 0;
            saw_fmt = 1;
            fmt_offset = offset + 8;
            fmt_size = chunk_size;
        } else if (memcmp(file + offset, "data", 4) == 0) {
            if (saw_data) return 0;
            saw_data = 1;
            data_offset = offset + 8;
            data_size = chunk_size;
        }
        offset = (uint32_t)next;
    }
    if (offset != file_size || !saw_fmt || !saw_data || fmt_size != 16 ||
        data_size == 0) return 0;

    if (read_u16le(file + fmt_offset) != 1) return 0; /* PCM only. */
    channels = read_u16le(file + fmt_offset + 2);
    rate = read_u32le(file + fmt_offset + 4);
    byte_rate = read_u32le(file + fmt_offset + 8);
    block_align = read_u16le(file + fmt_offset + 12);
    bits = read_u16le(file + fmt_offset + 14);
    if ((channels != SNES_MOD_AUDIO_CHANNELS_MONO &&
         channels != SNES_MOD_AUDIO_CHANNELS_STEREO) ||
        rate < 8000 || rate > 192000 || bits != 16 ||
        block_align != channels * 2u ||
        byte_rate != rate * (uint32_t)block_align || data_size % block_align ||
        data_size / block_align > UINT32_MAX) return 0;

    *out_samples = (const int16_t *)(const void *)(file + data_offset);
    *out_frames = data_size / block_align;
    *out_rate = rate;
    *out_channels = channels;
    return 1;
}

static int load_one(const char *path, SNESModAudioClip *out_clip)
{
    uint8_t *file;
    uint32_t file_size, frames, rate, channels;
    const int16_t *samples;
    SNESModAudioClip clip;
    if (!read_file_bounded(path, &file, &file_size) ||
        !parse_pcm_wave(file, file_size, &samples, &frames, &rate, &channels)) {
        free(file);
        return 0;
    }
    clip = snes_mod_audio_register_pcm_s16(samples, frames, rate, channels);
    free(file);
    if (clip == SNES_MOD_AUDIO_CLIP_INVALID) return 0;
    *out_clip = clip;
    return 1;
}

void smw_falcon_audio_reset(void)
{
    unsigned i;
    snes_mod_audio_stop_all();
    for (i = 0; i < FALCON_AUDIO_CUE_COUNT - 1; ++i) {
        if (s_clips[i] != SNES_MOD_AUDIO_CLIP_INVALID) {
            snes_mod_audio_unregister(s_clips[i]);
            s_clips[i] = SNES_MOD_AUDIO_CLIP_INVALID;
        }
    }
}

int smw_falcon_audio_activate(const char *cache_dir)
{
    unsigned i;
    char path[SMW_FALCON_AUDIO_PATH_CAP];
    size_t directory_length;
    char separator[2] = { '/', '\0' };

    smw_falcon_audio_reset();
    if (cache_dir == NULL || cache_dir[0] == '\0') return 0;
    directory_length = strlen(cache_dir);
    if (directory_length + 1 >= sizeof(path)) return 0;
    if (cache_dir[directory_length - 1] == '/' ||
        cache_dir[directory_length - 1] == '\\') separator[0] = '\0';

    for (i = 0; i < FALCON_AUDIO_CUE_COUNT - 1; ++i) {
        int written = snprintf(path, sizeof(path), "%s%s%s", cache_dir,
                               separator, s_filenames[i]);
        if (written < 0 || (size_t)written >= sizeof(path) ||
            !load_one(path, &s_clips[i])) {
            smw_falcon_audio_reset();
            return 0;
        }
    }
    return 1;
}

int smw_falcon_audio_is_active(void)
{
    unsigned i;
    for (i = 0; i < FALCON_AUDIO_CUE_COUNT - 1; ++i)
        if (s_clips[i] == SNES_MOD_AUDIO_CLIP_INVALID) return 0;
    return 1;
}

void smw_falcon_audio_play_events(const ForeignAudioEvents *events)
{
    uint32_t i, count;
    if (events == NULL || !smw_falcon_audio_is_active()) return;
    count = events->count;
    if (count > FOREIGN_AUDIO_EVENT_CAPACITY) count = FOREIGN_AUDIO_EVENT_CAPACITY;
    for (i = 0; i < count; ++i) {
        uint32_t cue = events->events[i].cue;
        if (cue >= FALCON_AUDIO_JUMP_EFFORT && cue < FALCON_AUDIO_CUE_COUNT)
            (void)snes_mod_audio_play(s_clips[cue - 1],
                                      events->events[i].gain_percent);
    }
}
