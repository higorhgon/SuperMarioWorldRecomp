#include "common_cpu_infra.h"
#include "smw_rtl.h"
#include "smw_renderer.h"

const RtlGameInfo kSmwGameInfo = {
  .title = "smw",
  .initialize = NULL,
  .run_frame = &RunOneFrameOfGame,
  .draw_ppu_frame = &SmwDrawPpuFrame,
  .save_name_prefix = "save",
  .state_save_extra = &SmwRendererSaveExtra,
  .state_load_extra = &SmwRendererLoadExtra,
  .on_state_loaded = &SmwRendererStateLoaded,
};
