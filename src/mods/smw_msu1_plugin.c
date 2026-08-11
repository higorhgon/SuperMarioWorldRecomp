#include "mod_runtime.h"
#include "common_rtl.h"
#include "config.h"
#include "snes/msu1.h"

#include <stdint.h>

#define SMW_MSU_PLUGIN "super-mario-world.msu1"

#define SMW_MSU_VOLUME 0xff
#define SMW_MSU_FADE_STEP 0x08

#define MSU_STATUS_ERROR 0x08
#define MSU_CONTROL_PLAY 0x01
#define MSU_CONTROL_REPEAT 0x02

static int g_smw_msu1_active;
static int g_smw_msu1_fading;

static int smw_msu1_is_overworld_context(void) {
  return g_ram[0x00d0] == 0 || (g_ram[0x04a0] == 0 && g_ram[0x04a1] == 0);
}

static uint16_t smw_msu1_mapped_track(uint8_t command) {
  if (smw_msu1_is_overworld_context()) {
    if (command >= 0x01 && command <= 0x08)
      return (uint16_t)(0x1d + command);
    if (command == 0x09)
      return g_ram[0x0070] == 0x7e ? 0x27 : 0x26;
    return command;
  }

  if (g_ram[0x0070] == 0x7e) {
    if (command == 0x0a) return 0x28;
    if (command == 0x0b) return 0x29;
  }
  return command;
}

static int smw_msu1_command_loops(uint8_t command) {
  switch (command) {
    case 0x08:
      return !smw_msu1_is_overworld_context();
    case 0x09:
      return g_ram[0x00d0] != 0 && g_ram[0x0070] != 0x7e;
    case 0x0a:
    case 0x0b:
    case 0x0c:
    case 0x0f:
    case 0x10:
    case 0x11:
    case 0x13:
    case 0x14:
    case 0x15:
    case 0x17:
    case 0x18:
    case 0x1b:
    case 0x1c:
    case 0x1d:
      return 0;
    default:
      return 1;
  }
}

static int smw_msu1_try_play(uint8_t command) {
  const uint16_t track = smw_msu1_mapped_track(command);
  const uint8_t control = (uint8_t)(MSU_CONTROL_PLAY |
      (smw_msu1_command_loops(command) ? MSU_CONTROL_REPEAT : 0));

  msu1_write(0x2004, (uint8_t)(track & 0xff));
  msu1_write(0x2005, (uint8_t)(track >> 8));
  if (msu1_read(0x2000) & MSU_STATUS_ERROR) {
    msu1_write(0x2007, 0);
    return 0;
  }

  msu1_write(0x2007, control);
  msu1_write(0x2006, SMW_MSU_VOLUME);
  g_smw_msu1_fading = 0;
  g_ram[0x1def] = control;
  g_ram[0x1dff] = 0x80;
  g_ram[0x1dfb] = 0;
  return 1;
}

static void smw_msu1_tick(void) {
  if (!g_smw_msu1_active || !msu1_enabled())
    return;
  if (!g_smw_msu1_fading)
    return;

  if (g_ram[0x0087] <= SMW_MSU_FADE_STEP + 1) {
    g_ram[0x0087] = 0;
    g_smw_msu1_fading = 0;
    msu1_write(0x2007, 0);
    return;
  }

  g_ram[0x0087] = (uint8_t)(g_ram[0x0087] - SMW_MSU_FADE_STEP);
  msu1_write(0x2006, g_ram[0x0087]);
}

static int smw_msu1_apu_write(uint16_t reg, uint8_t value) {
  if (!g_smw_msu1_active || !msu1_enabled() || reg != 0x2142)
    return 0;

  if (value == 0)
    return 0;

  if (value == 0x80) {
    if (g_ram[0x0087] == 0)
      g_ram[0x0087] = SMW_MSU_VOLUME;
    g_smw_msu1_fading = 1;
    g_ram[0x1dff] = 0x80;
    g_ram[0x1dfb] = 0x80;
    return 1;
  }

  return smw_msu1_try_play(value);
}

static void smw_msu1_reset(void) {
  g_smw_msu1_active = 0;
  g_smw_msu1_fading = 0;
  g_config.msu1_enabled = false;
}

static void smw_msu1_activate(void) {
  g_smw_msu1_active = 1;
  g_smw_msu1_fading = 0;
  g_config.msu1_enabled = true;
  (void)snes_mod_register_frame_callback(smw_msu1_tick);
  (void)snes_mod_register_apu_write_callback(smw_msu1_apu_write);
}

SNES_MOD_CONSTRUCTOR(smw_register_msu1_plugin) {
  (void)snes_mod_register_reset_callback(smw_msu1_reset);
  (void)snes_mod_register_activation_plugin(SMW_MSU_PLUGIN, smw_msu1_activate);
}
