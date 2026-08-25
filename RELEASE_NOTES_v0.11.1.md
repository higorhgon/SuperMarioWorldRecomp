## What's new

This is a dependency refresh release for the public Super Mario World build.

- Updated `snesrecomp` to include the latest host/runtime fixes, including
  scheduler-deadline unwind handling.
- Updated `recomp-ui` to the current launcher, including the hidden disabled-mod
  feature cleanup and recent SNES gamepad binding UI.
- Removed the obsolete game-local `tools/sync_funcs_h.py`; regen now uses the
  framework `v2_sync_funcs_h.py` tool.
- Kept the stock title demo on the interpreter path to avoid the known generated
  bounce instability there.

## Validation

- Windows release build completed.
- Linux AppImage build completed.
- AppImage layout test passed: config, saves, ROM cache, and mods anchor beside
  the AppImage.
- Smoke launch confirmed the Linux AppImage loaded the ROM, initialized SDL/audio,
  entered the main loop, and simulated the first frame.

## Notes

- A legally-obtained Super Mario World (USA) ROM is required and is not included.
- Widescreen remains available from the launcher and defaults off.
