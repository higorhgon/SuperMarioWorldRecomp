#include "smw_falcon_audio.h"

#include "mod_audio.h"

#include <stdint.h>
#include <stdio.h>
#include <string.h>

#ifdef _WIN32
#include <direct.h>
#define MAKE_DIR(path) _mkdir(path)
#define REMOVE_DIR(path) _rmdir(path)
#else
#include <sys/stat.h>
#include <unistd.h>
#define MAKE_DIR(path) mkdir(path, 0700)
#define REMOVE_DIR(path) rmdir(path)
#endif

#define CACHE_DIR "falcon_audio_test_cache"
#define CUE_COUNT 11

static const char *const s_names[CUE_COUNT] = {
    "falcon_jump_effort.wav", "falcon_punch_falcon.wav",
    "falcon_punch_punch.wav", "falcon_kick.wav",
    "falcon_punch_impact_fgm.wav", "falcon_kick_swing_fgm.wav",
    "falcon_kick_start_fgm.wav", "falcon_dive_launch_fgm.wav",
    "falcon_dive_catch_fgm.wav", "falcon_dive_explosion_fgm.wav",
    "falcon_dive_voice.wav",
};

static int s_register_calls, s_unregister_calls, s_stop_calls, s_play_calls;
static int s_fail_register_at;
static int s_unregistered[CUE_COUNT + 2];
static int s_played_clip[8], s_played_gain[8];

SNESModAudioClip snes_mod_audio_register_pcm_s16(const int16_t *samples,
    uint32_t frames, uint32_t rate, uint32_t channels)
{
    ++s_register_calls;
    if (samples == NULL || frames != 2 || rate != 22050 || channels != 1 ||
        (s_fail_register_at != 0 && s_register_calls == s_fail_register_at)) return 0;
    return s_register_calls;
}
void snes_mod_audio_unregister(SNESModAudioClip clip)
{
    s_unregistered[s_unregister_calls++] = clip;
}
int snes_mod_audio_play(SNESModAudioClip clip, int gain)
{
    s_played_clip[s_play_calls] = clip;
    s_played_gain[s_play_calls++] = gain;
    return 1;
}
void snes_mod_audio_stop_all(void) { ++s_stop_calls; }

static int failed(const char *what, int line)
{
    fprintf(stderr, "FAIL line %d: %s\n", line, what);
    return 1;
}
#define CHECK(x) do { if (!(x)) return failed(#x, __LINE__); } while (0)

static void write_u16(FILE *f, uint16_t x)
{
    fputc(x & 0xff, f); fputc(x >> 8, f);
}
static void write_u32(FILE *f, uint32_t x)
{
    write_u16(f, (uint16_t)x); write_u16(f, (uint16_t)(x >> 16));
}
static int write_wave(const char *name, int corrupt)
{
    char path[256];
    FILE *f;
    snprintf(path, sizeof(path), "%s/%s", CACHE_DIR, name);
    f = fopen(path, "wb");
    if (f == NULL) return 0;
    if (corrupt) {
        fputs("RIFF\x01\0\0\0WAVEfmt ", f);
        fclose(f);
        return 1;
    }
    fwrite("RIFF", 1, 4, f); write_u32(f, 40); fwrite("WAVE", 1, 4, f);
    fwrite("fmt ", 1, 4, f); write_u32(f, 16);
    write_u16(f, 1); write_u16(f, 1); write_u32(f, 22050);
    write_u32(f, 44100); write_u16(f, 2); write_u16(f, 16);
    fwrite("data", 1, 4, f); write_u32(f, 4);
    write_u16(f, 0x1234); write_u16(f, 0xfedc);
    fclose(f);
    return 1;
}
static int write_all(int corrupt_index)
{
    int i;
    for (i = 0; i < CUE_COUNT; ++i)
        if (!write_wave(s_names[i], i == corrupt_index)) return 0;
    return 1;
}
static void cleanup(void)
{
    int i;
    char path[256];
    for (i = 0; i < CUE_COUNT; ++i) {
        snprintf(path, sizeof(path), "%s/%s", CACHE_DIR, s_names[i]);
        remove(path);
    }
    REMOVE_DIR(CACHE_DIR);
}

int main(void)
{
    ForeignAudioEvents events;
    CHECK(MAKE_DIR(CACHE_DIR) == 0);
    CHECK(write_all(-1));
    CHECK(smw_falcon_audio_activate(CACHE_DIR));
    CHECK(smw_falcon_audio_is_active());
    CHECK(s_register_calls == CUE_COUNT);

    memset(&events, 0, sizeof(events));
    events.count = 4;
    events.events[0].cue = 1; events.events[0].gain_percent = 100;
    events.events[1].cue = 11; events.events[1].gain_percent = 175;
    events.events[2].cue = 0; events.events[2].gain_percent = 99;
    events.events[3].cue = 99; events.events[3].gain_percent = 99;
    smw_falcon_audio_play_events(&events);
    CHECK(s_play_calls == 2);
    CHECK(s_played_clip[0] == 1 && s_played_gain[0] == 100);
    CHECK(s_played_clip[1] == 11 && s_played_gain[1] == 175);

    CHECK(write_all(3));
    CHECK(!smw_falcon_audio_activate(CACHE_DIR));
    CHECK(!smw_falcon_audio_is_active());
    CHECK(s_unregister_calls == CUE_COUNT + 3);

    CHECK(write_all(-1));
    s_fail_register_at = s_register_calls + 5;
    CHECK(!smw_falcon_audio_activate(CACHE_DIR));
    CHECK(!smw_falcon_audio_is_active());
    CHECK(s_unregister_calls == CUE_COUNT + 3 + 4);
    s_fail_register_at = 0;

    CHECK(smw_falcon_audio_activate(CACHE_DIR));
    smw_falcon_audio_reset();
    CHECK(!smw_falcon_audio_is_active());
    CHECK(s_stop_calls >= 4);
    cleanup();
    puts("falcon_audio_test: PASS");
    return 0;
}
