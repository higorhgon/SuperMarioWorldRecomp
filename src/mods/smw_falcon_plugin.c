#include "mods/falcon/captain_falcon_foreign.h"

#include "foreign_controller.h"
#include "mod_runtime.h"

#define SMW_FALCON_PLUGIN "super-mario-world.smash64.captain-falcon"

static void smw_falcon_reset(void)
{
    snes_foreign_select(NULL);
    snes_foreign_set_ownership(FOREIGN_OWNERSHIP_NATIVE);
}

static void smw_falcon_activate(void)
{
    if (snes_foreign_select(SMW_CAPTAIN_FALCON_ID))
        snes_foreign_set_ownership(FOREIGN_OWNERSHIP_SCRIPTED);
}

SNES_MOD_CONSTRUCTOR(smw_register_falcon_plugin)
{
    (void)smw_captain_falcon_register();
    (void)snes_mod_register_reset_callback(smw_falcon_reset);
    (void)snes_mod_register_activation_plugin(SMW_FALCON_PLUGIN,
                                              smw_falcon_activate);
}
