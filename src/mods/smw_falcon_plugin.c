#include "mods/falcon/captain_falcon_foreign.h"
#include "mods/falcon/smw_falcon_presentation_runtime.h"

#include "foreign_controller.h"
#include "mod_runtime.h"

#define SMW_FALCON_PLUGIN "super-mario-world.smash64.captain-falcon"

const char *snes_mod_external_rom_path(const char *package_id,
                                       const char *feature_id,
                                       const char *resource_id);

static void smw_falcon_reset(void)
{
    smw_falcon_presentation_reset();
    snes_foreign_select(NULL);
    snes_foreign_set_ownership(FOREIGN_OWNERSHIP_NATIVE);
}

static void smw_falcon_activate(void)
{
    if (snes_foreign_select(SMW_CAPTAIN_FALCON_ID)) {
        const char *owner_rom = snes_mod_external_rom_path(
            SMW_FALCON_PLUGIN, "captain-falcon", "smash64-us-v10");
        snes_foreign_set_ownership(FOREIGN_OWNERSHIP_SCRIPTED);
        smw_falcon_presentation_activate(owner_rom);
    }
}

SNES_MOD_CONSTRUCTOR(smw_register_falcon_plugin)
{
    (void)smw_captain_falcon_register();
    (void)snes_mod_register_reset_callback(smw_falcon_reset);
    (void)snes_mod_register_activation_plugin(SMW_FALCON_PLUGIN,
                                              smw_falcon_activate);
}
