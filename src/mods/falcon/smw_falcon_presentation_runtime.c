/* Game-owned bridge for the approved, external Falcon owner cache. */
#include "smw_falcon_presentation_runtime.h"

#include "captain_falcon_foreign.h"
#include "falcon_locomotion.h"
#include "falcon_presentation.h"
#include "foreign_controller.h"
#include "common_rtl.h"
#include "sha256.h"
#include "variables.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#ifdef _WIN32
#include <process.h>
#define SMW_GETPID _getpid
#else
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>
#define SMW_GETPID getpid
#endif

#define FALCON_CACHE_PREFIX "falcon-final-r1-e2929e10fccc0aa84e5776227e798abc07cedabf-"
#define FALCON_RUNTIME_MAX_BYTES (64u * 1024u * 1024u)
#define FALCON_PLAYER_OAM_FIRST 64u /* $0300 / four bytes per OAM entry */
#define FALCON_PLAYER_OAM_COUNT 12u /* $0300..$032f, SMW PlayerGFXRt */

static const uint8_t k_runtime_sha256[32] = {
    0x8a,0x8e,0x0a,0xc0,0x13,0x41,0x58,0x44,
    0x88,0xda,0xd5,0x68,0x1a,0xe7,0x56,0x3f,
    0x31,0x42,0xee,0x15,0x91,0x5f,0xf1,0x54,
    0xf5,0xe3,0x12,0x2c,0x57,0x14,0x6a,0x3e,
};

/* Binding is required for PPU's OBJ RemoveFromGame path. It is intentionally
 * not composited: only slots 64..75 are captured and removed. */
static uint32_t s_obj_scratch[512 * 240];
static FalconPresentation *s_presentation;
static int s_bound;
static int s_suppression_active;
static int s_mesh_draw_active;
static char s_last_gate[96];

/* Opt-in, path-free activation trace for TCP validation. The caller chooses
 * the external output file; no ROM/cache path or owner data is ever logged. */
static void trace(const char *event) {
    const char *path = getenv("SNESRECOMP_FALCON_PRESENTATION_TRACE");
    FILE *file;
    if (!path || !*path || !(file = fopen(path, "ab"))) return;
    fprintf(file, "%s\n", event);
    fclose(file);
}

static void note(const char *message) {
    fprintf(stderr, "Falcon presentation disabled: %s\n", message);
    trace(message);
}

static int absolute_path(const char *path) {
    if (!path || !*path) return 0;
#ifdef _WIN32
    return ((path[0] >= 'A' && path[0] <= 'Z') ||
            (path[0] >= 'a' && path[0] <= 'z')) && path[1] == ':' &&
           (path[2] == '\\' || path[2] == '/');
#else
    return path[0] == '/';
#endif
}

static void join_path(char *out, size_t out_size, const char *left,
                      const char *right) {
    const size_t n = strlen(left);
    if (n && (left[n - 1] == '/' || left[n - 1] == '\\'))
        snprintf(out, out_size, "%s%s", left, right);
    else
        snprintf(out, out_size, "%s/%s", left, right);
}

static int valid_cache_name(const char *name) {
    const size_t prefix = strlen(FALCON_CACHE_PREFIX);
    size_t i;
    if (!name || strncmp(name, FALCON_CACHE_PREFIX, prefix) != 0 ||
        strlen(name) != prefix + 16) return 0;
    for (i = prefix; i < prefix + 16; ++i)
        if (!((name[i] >= '0' && name[i] <= '9') ||
              (name[i] >= 'a' && name[i] <= 'f'))) return 0;
    return 1;
}

static int verify_runtime_blob(const char *path) {
    FILE *file;
    long length;
    uint8_t *bytes, actual[32];
    int ok = 0;
    if (!path || !(file = fopen(path, "rb"))) return 0;
    if (fseek(file, 0, SEEK_END) || (length = ftell(file)) <= 0 ||
        (unsigned long)length > FALCON_RUNTIME_MAX_BYTES || fseek(file, 0, SEEK_SET))
        goto done;
    bytes = (uint8_t *)malloc((size_t)length);
    if (!bytes) goto done;
    if (fread(bytes, 1, (size_t)length, file) == (size_t)length) {
        sha256_compute(bytes, (size_t)length, actual);
        ok = memcmp(actual, k_runtime_sha256, sizeof(actual)) == 0;
    }
    free(bytes);
done:
    fclose(file);
    return ok;
}

static int load_final_cache(const char *cache) {
    const char *base;
    char blob[1024], manifest[1024];
    FalconPresentation *loaded;
    if (!absolute_path(cache)) { note("cache path is not absolute"); return 0; }
    base = strrchr(cache, '/');
#ifdef _WIN32
    { const char *backslash = strrchr(cache, '\\'); if (!base || (backslash && backslash > base)) base = backslash; }
#endif
    base = base ? base + 1 : cache;
    if (!valid_cache_name(base)) { note("cache is not an immutable approved final-cache name"); return 0; }
    join_path(manifest, sizeof(manifest), cache, "manifest.json");
    join_path(blob, sizeof(blob), cache, "falcon_runtime.bin");
    { FILE *file = fopen(manifest, "rb");
      if (!file) { note("final cache has no manifest"); return 0; }
      fclose(file); }
    /* The helper validates the full inventory; host additionally pins exactly
     * the approved runtime bytes before the parser sees them. */
    if (!verify_runtime_blob(blob)) { note("runtime blob hash is not approved"); return 0; }
    loaded = falcon_presentation_load_file(blob);
    if (!loaded) { note("runtime blob is malformed"); return 0; }
    falcon_presentation_destroy(s_presentation);
    s_presentation = loaded;
    trace("approved runtime cache loaded");
    return 1;
}

static const char *default_cache_root(void) {
    const char *local = getenv("LOCALAPPDATA");
    return local && *local ? local : NULL;
}

/* Never route owner-controlled paths through a shell. The helper contract is
 * an executable, not a command string or .cmd/.sh wrapper; each path below is
 * one argv item even when it contains shell metacharacters or spaces. */
static int run_cache_helper(const char *helper, const char *owner_rom_path,
                            const char *root, const char *result) {
    char *const argv[] = { (char *)helper, "--rom", (char *)owner_rom_path,
                           "--cache-root", (char *)root, "--result-file",
                           (char *)result, NULL };
#ifdef _WIN32
    return _spawnv(_P_WAIT, helper, (const char *const *)argv) == 0;
#else
    pid_t child = fork();
    int status;
    if (child < 0) return 0;
    if (child == 0) {
        execv(helper, argv);
        _exit(127);
    }
    if (waitpid(child, &status, 0) < 0) return 0;
    return WIFEXITED(status) && WEXITSTATUS(status) == 0;
#endif
}

static int invoke_cache_helper(const char *owner_rom_path) {
    const char *helper = getenv("SNESRECOMP_FALCON_CACHE_HELPER");
    const char *root = getenv("SNESRECOMP_FALCON_CACHE_ROOT");
    char fallback_root[768], result[1024], cache[1024], name[256];
    FILE *file;
    int rc;
    if (!root || !*root) {
        const char *local = default_cache_root();
        if (!local) { note("set SNESRECOMP_FALCON_CACHE_ROOT for the external cache"); return 0; }
        snprintf(fallback_root, sizeof(fallback_root), "%s/SuperMarioWorldRecomp/smash64", local);
        root = fallback_root;
    }
    if (!absolute_path(helper) || !absolute_path(root) || !absolute_path(owner_rom_path)) {
        note("helper, cache root, or committed owner ROM path is not absolute");
        return 0;
    }
    snprintf(result, sizeof(result), "%s/.smw-falcon-cache-result-%ld.txt", root,
             (long)SMW_GETPID());
    rc = run_cache_helper(helper, owner_rom_path, root, result);
    if (!rc) { note("external final-cache helper failed (see helper output)"); return 0; }
    file = fopen(result, "rb");
    if (!file || !fgets(name, sizeof(name), file)) {
        if (file) fclose(file);
        note("external final-cache helper returned no result file");
        return 0;
    }
    fclose(file);
    remove(result);
    name[strcspn(name, "\r\n")] = '\0';
    if (!valid_cache_name(name) || strchr(name, '/') || strchr(name, '\\')) {
        note("external final-cache helper returned an invalid cache basename");
        return 0;
    }
    join_path(cache, sizeof(cache), root, name);
    return load_final_cache(cache);
}

FalconPresentationPose smw_falcon_presentation_pose_for_state(
    int state, unsigned state_frame, float facing) {
    FalconPresentationPose pose;
    pose.frame = (float)state_frame;
    pose.facing_right = facing >= 0.0f;
    switch (state) {
    case FL_WALK_SLOW: case FL_WALK_MIDDLE: case FL_WALK_FAST: pose.state = FALCON_PRESENT_WALK; break;
    case FL_DASH: case FL_RUN: case FL_RUN_BRAKE: pose.state = FALCON_PRESENT_RUN; break;
    case FL_KNEEBEND: case FL_JUMP_F: case FL_JUMP_B:
    case FL_JUMP_AERIAL_F: case FL_JUMP_AERIAL_B: pose.state = FALCON_PRESENT_JUMP; break;
    case FL_FALL: case FL_FALL_AERIAL: case FL_LANDING_LIGHT: case FL_LANDING_HEAVY: pose.state = FALCON_PRESENT_FALL; break;
    case FL_FALCON_PUNCH_GROUND: case FL_FALCON_PUNCH_AIR: pose.state = FALCON_PRESENT_PUNCH; break;
    case FL_FALCON_KICK_GROUND: case FL_FALCON_KICK_GROUND_AIR: case FL_FALCON_KICK_LANDING:
    case FL_FALCON_KICK_AIR: case FL_FALCON_KICK_BOUND: pose.state = FALCON_PRESENT_KICK; break;
    case FL_FALCON_DIVE_CATCH: pose.state = FALCON_PRESENT_DIVE_CATCH; break;
    case FL_FALCON_DIVE_THROW: pose.state = FALCON_PRESENT_DIVE_THROW; break;
    case FL_FALCON_DIVE_GROUND: case FL_FALCON_DIVE_AIR: case FL_FALCON_DIVE_FALL:
    case FL_FALCON_DIVE_LANDING: pose.state = FALCON_PRESENT_DIVE; break;
    default: pose.state = FALCON_PRESENT_IDLE; break;
    }
    return pose;
}

static const char *controllable_reason(void) {
    const ForeignController *controller = snes_foreign_active();
    if (!s_presentation) return "cache unavailable";
    if (!controller || strcmp(controller->id, SMW_CAPTAIN_FALCON_ID)) return "Falcon controller inactive";
    if (snes_foreign_ownership() != FOREIGN_OWNERSHIP_FOREIGN) return "controller handoff";
    if (misc_game_mode != 0x14) return "not level gameplay";
    if (player_current_state != 0) return "nonordinary player state";
    if (player_timer_pipe_warping || player_pipe_action || flag_about_to_warp_in_pipe) return "pipe handoff";
    if (timer_end_level || timer_end_level_via_keyhole) return "goal handoff";
    return "active";
}

static int controllable(void) {
    const char *reason = controllable_reason();
    if (strcmp(reason, s_last_gate)) {
        snprintf(s_last_gate, sizeof(s_last_gate), "%s", reason);
        trace(reason);
    }
    return !strcmp(reason, "active");
}

void smw_falcon_presentation_reset(void) {
    falcon_presentation_destroy(s_presentation);
    s_presentation = NULL;
    s_bound = 0;
    s_suppression_active = 0;
    s_mesh_draw_active = 0;
    s_last_gate[0] = '\0';
}

void smw_falcon_presentation_activate(const char *owner_rom_path) {
    const char *cache = getenv("SNESRECOMP_FALCON_CACHE");
    smw_falcon_presentation_reset();
    trace("activation requested");
    if (!owner_rom_path || !absolute_path(owner_rom_path)) { note("committed owner ROM path unavailable"); return; }
    if (cache && *cache) (void)load_final_cache(cache);
    else if (!invoke_cache_helper(owner_rom_path))
        note("set SNESRECOMP_FALCON_CACHE or install SNESRECOMP_FALCON_CACHE_HELPER");
}

int smw_falcon_presentation_is_active(void) { return controllable(); }

void smw_falcon_presentation_prepare_ppu(Ppu *ppu) {
    if (!ppu) return;
    PpuClearOverlayCaptures(ppu);
    if (!controllable()) { s_suppression_active = 0; return; }
    if (!s_bound) {
        if (!PpuBindOverlaySurface(ppu, kPpuOverlaySource_Obj,
                                   (uint8_t *)s_obj_scratch,
                                   sizeof(s_obj_scratch) / 240)) {
            note("could not bind narrow OBJ suppression surface");
            return;
        }
        s_bound = 1;
        trace("player OBJ suppression bound");
    }
    if (!PpuSetOverlayCapture(ppu, kPpuOverlaySource_Obj, -128, 0, 512, 224,
                              kPpuOverlayFlag_RemoveFromGame) ||
        !PpuSetOverlayOamRange(ppu, FALCON_PLAYER_OAM_FIRST,
                               FALCON_PLAYER_OAM_COUNT)) {
        s_suppression_active = 0;
        note("could not suppress the player OBJ range");
    } else if (!s_suppression_active) {
        s_suppression_active = 1;
        trace("player OBJ suppression active");
    }
}

void smw_falcon_presentation_present(uint8_t *pixels, size_t pitch,
                                     int width, int height) {
    const ForeignState *state;
    FalconPresentationTarget target;
    FalconPresentationPose pose;
    if (!controllable() || !pixels || pitch % sizeof(uint32_t)) {
        s_mesh_draw_active = 0;
        return;
    }
    state = snes_foreign_state();
    if (!state) return;
    memset(&target, 0, sizeof(target));
    target.framebuffer = (uint32_t *)pixels;
    target.width = width;
    target.height = height;
    target.pitch_pixels = (int)(pitch / sizeof(uint32_t));
    target.anchor_x = (float)((width - 256) / 2 + (int16_t)player_on_screen_pos_x + 8);
    target.anchor_y = (float)((int16_t)player_on_screen_pos_y + 24);
    target.scale = 1.0f;
    pose = smw_falcon_presentation_pose_for_state(
        state->state, state->state_frame, state->facing);
    if (!falcon_presentation_draw(s_presentation, &pose, &target)) {
        s_mesh_draw_active = 0;
        note("mesh compositor rejected the current target or pose");
    } else if (!s_mesh_draw_active) {
        s_mesh_draw_active = 1;
        trace("mesh compositor active");
    }
}
