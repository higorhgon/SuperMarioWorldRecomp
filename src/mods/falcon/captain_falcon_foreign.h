#pragma once

/* Registration is separate from the SMW host adapter: the controller remains
 * portable and speaks only runner/foreign_controller.h. */
#define SMW_CAPTAIN_FALCON_ID "super-mario-world.smash64.captain-falcon"

int smw_captain_falcon_register(void);
