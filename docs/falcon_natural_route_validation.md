# Falcon natural-route validation

`tools/falcon_natural_route.py` scouts a genuine SMW gameplay route without
pausing the runner or writing RAM. It starts the executable unpaused, disables
physical gamepads through an external temporary config, requests `loadstate N`
for consumption at the main thread's normal frame boundary, and then drives
only `set_controller` inputs from a JSON timeline.

It intentionally does not use `pause`, `step`, `write_ram`, synthetic sprite
seeding, or OAM relocation. The only observation commands are `frame`,
`read_ram`, `screenshot`, and `oam_render_get`.

```powershell
py -3 tools/falcon_natural_route.py `
  --exe build-falcon/SuperMarioWorldSNESRecomp.exe `
  --rom build-falcon/smw.sfc `
  --route test/falcon_validation/falcon_natural_route_slot0.json `
  --out _triage/falcon-natural-route
```

The route JSON has a hashable slot number, a boot delay before the asynchronous
load, ordered `at` frame offsets, SNES controller strings, and a bounded route
duration. The tool records the executable/route SHA-256 values, every observed
native sprite table, PPU render-ring snapshots at each screenshot, and a final
route screenshot. It releases controller input and verifies that TCP port 4377
is free before it returns.

## Required natural evidence

For each capture the output records:

- game/player state: `$0100`, `$0071-$009A`, `$13DA-$13DD`;
- carry state: `$1470-$148F`;
- all normal-sprite IDs, status, position, speed, subposition, and OAM-selector
  tables: `$009E`, `$14C8`, `$00D8/$14D4`, `$00E4/$14E0`, `$00AA/$00B6`,
  `$14EC/$14F8`, `$15EA`;
- final PPU screenshot and `oam_render_get` snapshots, so the picture is tied
  to scanline-rendered OAM rather than a requested WRAM placement.

For natural carry, accept only a level-spawned loose shell with a real native
status `$09 -> $0B` transition and native carry flags; do not accept a seeded
table. For Punch/Kick, the intended ordinary target must be a natural status
`$08` sprite and evidence must cover pre-contact, active/recovery, and the
post-contact native status. Current combat code deliberately excludes loose
shell status `$09/$0A` from attack targets, so a Punch-vs-loose-shell scenario
can currently only prove preservation/no corruption, not a hit.

## Current route blocker

The supplied `save0.sav` begins immediately before the one-block step and does
not expose a normal sprite. Free-running, gamepad-disabled attempts using
right+jump, immediate right+jump, release-at-wall then right+jump, and a
rightward double-jump found no status `$08/$09/$0A/$0B` sprite. They either
entered the native death/map return sequence or settled at the step around
`X=$0742`. Therefore slot 0 is not yet a reviewed natural target route.

To turn the scout into a proof scenario, provide a hash-pinned save slot or a
reviewed controller-only level path that reaches both a level-spawned loose
shell and a supported status `$08` enemy. A frame-stamped, read-only Falcon
move/hitbox export is additionally required to prove the desired Punch/Kick
active window rather than correlating it only by wall-clock timing.
