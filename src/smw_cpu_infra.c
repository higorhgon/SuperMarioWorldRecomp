#include "common_cpu_infra.h"
#include "smw_rtl.h"
#include "foreign_controller.h"
#include "snes/saveload.h"
#include "overrides/falcon/falcon_smw_adapter.h"

#include <stdio.h>
#include <string.h>

/* Fixed game chunk appended by the runner's RtlGameInfo save extension. The
 * framework record itself remains pointer-free and versioned; this wrapper
 * only gives the SMW stream a stable bounded slot for it. */
#define SMW_FOREIGN_SAVE_MAGIC 0x31574653u /* "SFW1" little-endian */
#define SMW_FOREIGN_SAVE_VERSION 1u
#define SMW_FOREIGN_SAVE_BLOB_CAP (SNES_FOREIGN_SAVE_MAX_PAYLOAD + 256u)

typedef struct {
  uint32 magic;
  uint32 version;
  uint32 blob_size;
  uint8 blob[SMW_FOREIGN_SAVE_BLOB_CAP];
} SmwForeignSaveChunk;

static int s_smw_foreign_chunk_loaded;

static void SmwStateSaveExtra(SaveLoadInfo *sli) {
  SmwForeignSaveChunk chunk;
  memset(&chunk, 0, sizeof(chunk));
  chunk.magic = SMW_FOREIGN_SAVE_MAGIC;
  chunk.version = SMW_FOREIGN_SAVE_VERSION;
  if (!snes_foreign_save(chunk.blob, sizeof(chunk.blob), &chunk.blob_size)) {
    /* A controller that cannot produce a complete payload must never create
     * a save that later revives stale host state. */
    chunk.magic = 0;
    fprintf(stderr, "[smw] foreign-controller save payload unavailable\n");
  }
  sli->func(sli, &chunk, sizeof(chunk));
}

static void SmwStateLoadExtra(SaveLoadInfo *sli, uint32 version) {
  SmwForeignSaveChunk chunk;
  (void)version;
  s_smw_foreign_chunk_loaded = 0;
  memset(&chunk, 0, sizeof(chunk));
  sli->func(sli, &chunk, sizeof(chunk));
  if (chunk.magic == SMW_FOREIGN_SAVE_MAGIC &&
      chunk.version == SMW_FOREIGN_SAVE_VERSION &&
      chunk.blob_size <= sizeof(chunk.blob) &&
      snes_foreign_load(chunk.blob, chunk.blob_size)) {
    s_smw_foreign_chunk_loaded = 1;
    return;
  }
  fprintf(stderr, "[smw] foreign-controller save payload rejected\n");
}

static void SmwOnStateLoaded(uint32 version) {
  (void)version;
  if (!s_smw_foreign_chunk_loaded) {
    /* Old, missing, or corrupt extensions must not combine restored guest
     * state with a live controller from the prior timeline. */
    snes_foreign_select(NULL);
    snes_foreign_set_ownership(FOREIGN_OWNERSHIP_NATIVE);
  }
  SmwFalconOnStateLoaded();
  s_smw_foreign_chunk_loaded = 0;
}

const RtlGameInfo kSmwGameInfo = {
  .title = "smw",
  .initialize = NULL,
  .run_frame = &RunOneFrameOfGame,
  .draw_ppu_frame = &SmwDrawPpuFrame,
  .save_name_prefix = "save",
  .state_save_extra = &SmwStateSaveExtra,
  .state_load_extra = &SmwStateLoadExtra,
  .on_state_loaded = &SmwOnStateLoaded,
};
