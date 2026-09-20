/* Focused integration test for the SMW RtlGameInfo foreign-controller chunk. */
#include "../../src/mods/falcon/captain_falcon_foreign.h"
#include "foreign_controller.h"
#include "common_cpu_infra.h"
#include "snes/saveload.h"
#include "smw_renderer.h"

#include <assert.h>
#include <stdio.h>
#include <string.h>

/* smw_cpu_infra.c's game descriptor references these frame callbacks. The
 * save-extension test does not execute a frame, so minimal definitions are
 * sufficient and keep generated SMW code out of this isolated harness. */
void RunOneFrameOfGame(void) {}
void SmwDrawPpuFrame(void) {}
void SmwFalconOnStateLoaded(void) {}
/* Owner-ROM animation assets are outside this serialization-only test. */
int smw_falcon_presentation_root_delta(const char *animation, float frame,
                                     float *dy, float *dz) {
    (void)animation; (void)frame; (void)dy; (void)dz; return 0;
}
uint8_t g_ram[0x20000];
const uint8_t *g_rom;

extern const RtlGameInfo kSmwGameInfo;

typedef struct {
    SaveLoadInfo base;
    uint8_t bytes[8192];
    size_t length;
    size_t position;
    int saving;
} TestSaveLoad;

static void test_sli_func(SaveLoadInfo *sli, void *data, size_t size)
{
    TestSaveLoad *test = (TestSaveLoad *)sli;
    assert(test->position + size <= sizeof(test->bytes));
    if (test->saving) {
        memcpy(test->bytes + test->position, data, size);
        test->length = test->position + size;
    } else {
        assert(test->position + size <= test->length);
        memcpy(data, test->bytes + test->position, size);
    }
    test->position += size;
}

static int same_state(const ForeignState *a, const ForeignState *b)
{
    return a->state == b->state && a->state_frame == b->state_frame &&
           a->x == b->x && a->y == b->y && a->vx == b->vx && a->vy == b->vy &&
           a->facing == b->facing && a->grounded == b->grounded &&
           a->fast_fall == b->fast_fall && a->air_cause == b->air_cause &&
           a->jump_phase == b->jump_phase;
}

int main(void)
{
    TestSaveLoad saved = { { test_sli_func }, { 0 }, 0, 0, 1 };
    TestSaveLoad loaded;
    ForeignInput input = { .stick_x = 1.0f, .jump_pressed = 1 };
    ForeignState before;
    ForeignMoveResult move;
    TestSaveLoad renderer = { { test_sli_func }, { 0 }, 0, 0, 1 };
    SmwRendererSaveExtra(&renderer.base);

    assert(kSmwGameInfo.state_save_extra && kSmwGameInfo.state_load_extra &&
           kSmwGameInfo.on_state_loaded);
    assert(smw_captain_falcon_register());
    assert(snes_foreign_select(SMW_CAPTAIN_FALCON_ID));
    snes_foreign_set_ownership(FOREIGN_OWNERSHIP_FOREIGN);
    assert(snes_foreign_tick(42, &input, &move));
    before = *snes_foreign_state();
    kSmwGameInfo.state_save_extra(&saved.base);
    assert(saved.length > 12);

    assert(snes_foreign_select(NULL));
    snes_foreign_set_ownership(FOREIGN_OWNERSHIP_NATIVE);
    memset(&loaded, 0, sizeof(loaded));
    loaded.base.func = test_sli_func;
    memcpy(loaded.bytes, saved.bytes, saved.length);
    loaded.length = saved.length;
    loaded.saving = 0;
    kSmwGameInfo.state_load_extra(&loaded.base, 7);
    kSmwGameInfo.on_state_loaded(7);
    assert(snes_foreign_active());
    assert(strcmp(snes_foreign_active()->id, SMW_CAPTAIN_FALCON_ID) == 0);
    assert(snes_foreign_ownership() == FOREIGN_OWNERSHIP_FOREIGN);
    assert(same_state(snes_foreign_state(), &before));

    /* Both legacy formats must load without reading beyond their exact end.
     * A main-branch state contains only SFW1; the renderer worktree used SMWS. */
    size_t foreign_offset = 4 + renderer.length;
    loaded = saved;
    loaded.saving = 0; loaded.position = 0;
    loaded.length = saved.length - foreign_offset;
    memmove(loaded.bytes, saved.bytes + foreign_offset, loaded.length);
    kSmwGameInfo.state_load_extra(&loaded.base, 7);
    kSmwGameInfo.on_state_loaded(7);
    assert(loaded.position == loaded.length);
    assert(same_state(snes_foreign_state(), &before));

    loaded = renderer;
    loaded.saving = 0; loaded.position = 0;
    kSmwGameInfo.state_load_extra(&loaded.base, 7);
    kSmwGameInfo.on_state_loaded(7);
    assert(loaded.position == loaded.length);
    assert(snes_foreign_active() == NULL);
    assert(snes_foreign_select(SMW_CAPTAIN_FALCON_ID));
    snes_foreign_set_ownership(FOREIGN_OWNERSHIP_FOREIGN);

    /* Damage the encapsulated framework record.  The core rejects it, and
     * SMW's post-load callback fails closed rather than retaining a prior
     * controller alongside newly loaded guest RAM. */
    saved.bytes[foreign_offset + 12] ^= 1;
    memset(&loaded, 0, sizeof(loaded));
    loaded.base.func = test_sli_func;
    memcpy(loaded.bytes, saved.bytes, saved.length);
    loaded.length = saved.length;
    kSmwGameInfo.state_load_extra(&loaded.base, 7);
    kSmwGameInfo.on_state_loaded(7);
    assert(snes_foreign_active() == NULL);
    assert(snes_foreign_ownership() == FOREIGN_OWNERSHIP_NATIVE);

    puts("SMW saves: combined roundtrip, legacy main/renderer formats and corruption PASS");
    return 0;
}
