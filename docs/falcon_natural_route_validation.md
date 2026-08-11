# Falcon natural-route validation

`tools/falcon_natural_route.py` scouts a genuine SMW gameplay route without
pausing the runner or writing RAM. It starts the executable unpaused, disables
physical gamepads through an external temporary config, requests `loadstate N`
for consumption at the main thread's normal frame boundary, and then drives
only `set_controller` inputs from a JSON timeline.

It intentionally does not use `pause`, `step`, `write_ram`, synthetic sprite
seeding, or OAM relocation. The only observation commands are `frame`,
`read_ram`, `screenshot`, and `oam_render_get`.

`savestate N` is an exception only for preserving a naturally reached route:
the debug-server thread queues it and the unpaused main loop consumes it at its
ordinary frame boundary with `RtlSaveLoad(kSaveLoad_Save, N)`. It must never be
used to synthesize a target; hash the resulting `saves/saveN.sav` before using
it as a later route input.

Route JSON may declare ordered `savestates` checkpoints (`at`, `id`, `slot`).
The scout refuses to overwrite an existing slot, requests the save through the
asynchronous command at the listed live frame, and records the materialized
file's SHA-256 in evidence.

For a native load route, `load_signature` (`game_mode`, `player_state`, `x`,
`y`) waits for the saved state to become observable, then allows two ordinary
running frames before the controller timeline starts. This anchors free-running
inputs to the loaded game state without using a pause or step command.

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
route screenshot. Optional ordered `captures` entries add named, exact-route
screenshots (for example post-launch, apex, and landing). The scout stops on
the first observed player state `$09` rather than allowing a death sequence to
obscure the route failure. It releases controller input and verifies that TCP
port 4377 is free before it returns.

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
table. For Punch/Kick, evidence must cover pre-contact, active/recovery, and
the post-contact native status. Supported targets are ordinary status-`$08`
sprites and loose shell IDs `$04-$07` in status `$09/$0A`; a carried status-`$0B`
shell remains excluded. A multi-shell Punch proof must show that every natural
shell intersecting the same active volume receives its native consequence.

## Current route blocker

The supplied `save0.sav` begins immediately before the one-block step and does
not expose a normal sprite. A controller-only route combining the grounded
Dive and a full held aerial second jump now clears that step without state
`$09`. The game naturally saved the resulting post-step state as `save1.sav`
(SHA-256
`9088252C25082B968AD16DB7DC45EABBE85C8472936C024FE04E8E00641DD6E3`).
No status `$08/$09/$0A/$0B` sprite had spawned at that checkpoint, so validation
must continue from slot 1 to reach natural combat/carry targets.

To turn the scout into a proof scenario, provide a hash-pinned save slot or a
reviewed controller-only level path that reaches both a level-spawned loose
shell and a supported status `$08` enemy. A frame-stamped, read-only Falcon
move/hitbox export is additionally required to prove the desired Punch/Kick
active window rather than correlating it only by wall-clock timing.
