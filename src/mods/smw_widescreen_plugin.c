#include "mod_runtime.h"
#include "recomp_launcher.h"
#include "smw_renderer.h"
#include <string.h>
#include <stdio.h>

#define PACKAGE "super-mario-world.enhancement.widescreen"
static void smw_renderer_reset(void) {
  g_smw_video = (SmwVideoSettings){false, true, 0};
}
static void smw_renderer_activate(void) {
  g_smw_video = (SmwVideoSettings){true, true, 0};
  const RecompLauncherCModProvider *p = snes_mod_runtime_launcher_provider_c();
  if (!p || !p->feature_option_get) return;
  RecompLauncherCModOption option;
  for (int i=0;i<16;++i) {
    memset(&option,0,sizeof(option));
    if (!p->feature_option_get(p->ctx,PACKAGE,"widescreen",i,&option)) break;
    if (!strcmp(option.id,"mode")) {
      int n=0,d=0;
      g_smw_video.aspect=sscanf(option.value,"%d_%d",&n,&d)==2 && n>0 && d>0 ? (double)n/d : 0;
    } else if (!strcmp(option.id,"spawn")) {
      g_smw_video.adaptive_spawns=strcmp(option.value,"original")!=0;
    }
  }
}
SNES_MOD_CONSTRUCTOR(smw_register_widescreen_plugin) {
  (void)snes_mod_register_reset_callback(smw_renderer_reset);
  (void)snes_mod_register_activation_plugin("super-mario-world.widescreen",smw_renderer_activate);
}
