# Falcon source/host seam map and deterministic validation

This is the implementation contract for the Captain Falcon SMW mod. It records
the evidence consulted on 2026-08-09 and provides a test format that works
before a Falcon executable exists.

## Evidence baseline

| Source | Revision inspected | Relevant evidence | Implementation consequence |
|---|---:|---|---|
| `SuperMarioWorldRecomp/SMWDisX` | `3390ee1a094bce35defb51f456423d62017d28f9` | `SMW_U.sym` names `ControllerUpdate` at `$008650`, `WriteControllerInput` at `$009C8F`, and the 128 KiB `NonMirroredWRAM` at `$7E2000`. | SMW remains the authority for controller sampling, player state, collision, sprites, blocks, water, and save compatibility. A mod must not import Smash's runtime/object model. |
| `SmashBrosDecomp` | `054ffc23f396868cd1db2b87ee3a2c1d3bebb75a` | `src/ft/ftchar/ftcaptain/ftcaptain.h` defines the Captain state/motion vocabulary; `ftcaptainstatus.h` assigns update/physics/map callbacks; `ftcaptainspecial{n,lw,hi}.c` are the behavior sources. | Treat Captain's animation, move timing, and tuning as *reference data/behavior*. Translate selected moves into SMW-native state transitions and collision results. Do not link MIPS code or N64 relocation objects. |
| `BattleShip` | `4fc112883dfec7e2471280c954a172a2eec44829` (dirty worktree observed and not touched) | Its port layer and investigation docs demonstrate owner-data relocation/cache boundaries and evidence-led image debugging. | Keep owner-ROM extraction/cache separate from mod state; validate resource identity and visual output through artifacts, not host pointers. |
| `recomp-template/NES/TCP.md` | local reference | Defines one-command-per-line localhost TCP, JSON replies, execution control, ring history, screenshots, and evidence-first diagnosis. | The SNES driver reuses the same transport discipline; it does not add printf/log scraping. |
| SNES runner debug server | `SuperMarioWorldRecomp/snesrecomp/runner/src/debug_server.c` | `read_ram`, `set_controller`, `step`, `dump_frame_wram`, `screenshot`, and `fingerprint` are registered commands. `main.c` starts it on TCP `4377` and honors `--paused`; `common_rtl.c` records a frame snapshot each frame. | This is the actual host seam. A trace build is required; production builds compile server stubs and cannot satisfy this driver. |

## Source-to-host boundary

| Concern | Owner source | Host seam / planned rule | Evidence to preserve |
|---|---|---|---|
| Inputs | SMW controller update (`$008650`, `$009C8F`) | TCP `set_controller p1=...` drives the existing 12-bit SNES pad override; release it explicitly after every scripted hold. | TCP response plus frame number. |
| Locomotion, gravity, slopes, water | SMW player update and layer collision | A Falcon state machine may request intent/velocity only; SMW collision decides solid, slope, water, death, and level handoff. | Player WRAM regions and screenshot at each transition. |
| Combat and interactions | SMW sprites/blocks, not Smash hitboxes | A Falcon attack translates to native sprite/block/shell consequences. No foreign `GObj`, fighter status table, or N64 coordinate system crosses this seam. | Sprite-status WRAM slice, target-specific slice, hit/recovery shot. |
| Animation and specials | Smash `ftcaptainstatus.h`, `ftcaptainspecialn.c`, `ftcaptainspeciallw.c`, `ftcaptainspecialhi.c` | Copy only the intended observable contract (startup/active/recovery, aerial/ground variants); parameterize values in mod data. | Named move milestone screenshots and state hashes. |
| Art/audio assets | verified owner ROM and extracted cache | Extraction is offline and versioned by content hash; runtime loads cache data through the mod asset API, never a foreign live pointer. | Asset manifest hash and `asset_loaded` visual shot. |
| Saves/mod-off | SMW save/load and mod lifecycle | New data is versioned/cleared on disable; no persistent foreign-pointer state. | save/load and mod-off parity matrix rows. |

## TCP scenario format

`test/falcon_validation/falcon_smoke.json` is the reference. The JSON shape is
strictly validated by `tools/falcon_validation.py`:

```json
{
  "format": "falcon-validation/v1",
  "name": "descriptive-name",
  "steps": [
    {"op": "input", "p1": ["right", "b"], "p2": null},
    {"op": "step", "frames": 30},
    {"op": "capture", "id": "walk-right", "wram": [
      {"name": "player", "addr": "0x007b", "len": 32}
    ]}
  ]
}
```

`input` accepts an ordered string list, a server-supported hexadecimal mask, or
`null` (`none`). `step` requests positive whole frames. `capture.id` is a safe
file stem. Every WRAM range is checked against the full 128 KiB `$7E/$7F` map.
The driver stores `evidence.json`, a BMP for each capture, each BMP's SHA-256,
and the exact bytes plus SHA-256 of every requested WRAM range.

Run it once a trace Falcon build exists:

```powershell
python tools/falcon_validation.py --exe build-falcon/SuperMarioWorldSNESRecomp.exe --scenario test/falcon_validation/falcon_smoke.json --out _triage/falcon_validation
```

The executable is launched with `--paused`; the only execution operation the
driver sends is `step N`. It never sends `pause`, a block breakpoint, or an
instruction step. The runner's `step` endpoint releases just enough frames and
parks again, so controller changes and captures are boundary-deterministic.
There is deliberately no process-name kill: the driver terminates only the PID
it launched. Missing `--exe` prints `SKIP` and exits zero; add `--require-build`
when a CI job must fail instead.

## Milestone shot matrix

All rows need a named `capture` with the listed WRAM ranges; use SHA-256 as a
reproducibility signal, then inspect BMPs for semantic acceptance. Exact hashes
become goldens only after the implementation is stable.

| ID | Scripted setup / input | Required evidence | Acceptance target |
|---|---|---|---|
| `boot` | launch paused | `$0100`, `$0071`, `$0094..$009B`, BMP | trace build starts and Falcon mod selection is visually identifiable without advancing. |
| `idle` | step 1 with no input | player state/position, BMP | stable standing pose; no unwanted horizontal drift. |
| `walk_right` | hold right 30 frames | `$007B..$009A`, sprite status `$14C8` | visible SMW-grounded movement and deterministic displacement. |
| `jump_takeoff` | press jump for 1, release, step 1 | `$0071`, vertical speed/position, BMP | Falcon takeoff uses SMW jump/collision semantics. |
| `jump_apex` | advance to expected apex | player state/position, BMP | aerial visual and gravity remain bounded and repeatable. |
| `special_ground` | agreed move binding on solid ground | player state, target sprite/status, BMP | startup/active/recovery and native hit/block result. |
| `special_air` | same binding while airborne | player state/velocity, target sprite/status, BMP | airborne branch has no ground-only side effect. |
| `shell_contact` | perform attack near shell | shell slot/status and player state, BMP | native shell ownership/direction and no corrupt sprite table. |
| `block_contact` | attack a turn/block target | block/item sprite status, BMP | native block consequence; no direct tilemap corruption. |
| `water_entry` | walk/fall into water | `$0071`, position/speed, BMP | floaty water behavior transitions cleanly. |
| `level_handoff` | trigger door/goal/pipe | `$0100`, `$13D9`, BMP before/after | scripted handoff clears transient Falcon state safely. |
| `save_reload` | save at a stable point, reload, capture | same state slices and BMP | no pointer-derived state; post-load behavior continues. |
| `mod_off_parity` | disable mod, reset, repeat boot/walk | boot/player slices, BMP | stock behavior remains unchanged under the baseline scenario. |

## Result interpretation

For a deterministic re-run of the same build, scenario, ROM, and mod cache,
compare `evidence.json` capture SHA-256 and WRAM SHA-256 fields by capture ID.
A mismatch is a lead, not automatically a failure: first compare the recorded
frame, then the exact WRAM bytes, then the BMP. A build without
`SNESRECOMP_TRACE` is not a valid validation target because its TCP API is a
compiled-out no-op surface.
