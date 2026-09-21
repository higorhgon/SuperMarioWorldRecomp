# Adaptive SMW renderer experiment

The one-player **SMW Adaptive Widescreen** mod replaces the old expanded-PPU
implementation. Its two controls are:

| Control | Default | Other choices |
| --- | --- | --- |
| Aspect ratio | Fit to screen | 4:3, 16:9, 16:10, 21:9, 32:9, 48:9, 100:9 |
| Enemy spawn behavior | Screen-based | Original 4:3 |

Fit follows the drawable window, including fullscreen and live resizing. It
keeps 224 lines and the native 7:6 pixel aspect: 4:3 uses 256 columns, 16:9
uses 342, 32:9 uses 682, and 100:9 uses 2134. Width is rounded to an even
number. Views narrower than 4:3 are letterboxed; the host allocation ceiling
is 16384 columns. The visible area stops at actual horizontal level bounds.

Screen-based spawning uses the visible horizontal area plus a 32-pixel
lookahead and extends ordinary despawn bounds. It scans while stationary at
the left level boundary too, including when the window becomes wider. Placed
shells and enemy groups use this same activation area. Original 4:3 leaves
the native activation and despawn policy intact, so objects can appear within a wider
view. Both use signed host coordinates for rendering. Scroll commands and
generators keep their native activation frontier, avoiding premature level
progression when the entire area is visible.

The HUD anchors automatically. Lives and bonus counters stay at the left,
the reserve box and its item stay centered, and TIME/coins/score stay at the
right. There is no third HUD option. The existing package/plugin identity is
retained so installed mod enablement and aspect choices carry over. The old
`hud` option and `Widescreen`/`WidescreenHud` INI settings are inactive.

## Build and play

Use the repository's normal CMake build with its pinned submodules and a local
US ROM. Regenerate existing banks first when switching from the old mod:

```sh
bash tools/regen.sh --stock --no-tests
cmake -S . -B build-adaptive -G Ninja -DCMAKE_BUILD_TYPE=Release
cmake --build build-adaptive
```

On Windows, use the intended native compiler/CMake/Python executables when
MSYS shims are also on PATH. This worktree was built with MinGW GCC, SDL3,
Python 3.12 and `SNESRECOMP_ENABLE_TRACE=OFF`. CMake installs the guest hooks
automatically. Do not edit `src/gen` by hand.

```powershell
.\tools\run_adaptive_renderer.ps1
```

The helper launches the built executable and creates isolated settings/saves
under `build-adaptive/playtest`. Its first-run defaults enable the mod with
Fit to screen and Screen-based spawning, with audio and a 1280x720 window.
Later launches preserve those local preferences. The catalog itself remains
an optional mod. `-CheckOnly` checks the executable/runtime paths without
launching; `-DirectRomPath` skips ROM selection. On other platforms, enable
the mod in the launcher normally. An explicit `--config` keeps the caller's
working directory authoritative for settings and saves.

## How it works

`src/smw_renderer.c` composes an independent host surface. The native PPU
stays 256x224. Scene RAM and completed OAM ownership are latched together before
NMI uploads that scene. The subsequent simulation tick can advance the camera
without moving the already-uploaded terrain. Scanout and repeated draws use
the latched camera, Map16 and sprite metadata, including after a resize.
Immutable per-scanline registers, palette, VRAM and OAM preserve
the game's IRQ/HDMA timing. Mode 1 tiles use captured VRAM in the native area
and the full level's Map16 data outside it. Layer 2 level modes also use
Map16. Horizontal repeating backgrounds project the native zero/full/half-speed
camera component from the visible viewport origin. This prevents scenery from
sliding with the embedded 256-pixel camera while the wider view is clamped to
either level edge. Captured animation, scanline offsets and tilemaps remain
authoritative. Layer 2 terrain keeps its world projection. The correction is
derived each frame, so resizing or loading a save needs no parallax history.
Horizontal pipes select their ROM graphics variant from the pipe's world
screen. SMW's live pipe pointer table belongs to the strip currently being
streamed, so reusing it across the expanded view would change pipe colors
as the camera moves. Vertical and background-only definitions retain their
native pointer lookup.

`src/smw_renderer_hooks.c` records signed sprite coordinates at the guest's
draw paths and changes the optional activation/despawn policy. A host display
list snapshots each normal/extended object's completed draw before the next
object can reuse its guest OAM entries. It retains final tiles, palette, size
and coordinates, including caller edits made after shared graphics helpers
return (such as the Volcano Lotus leaves). Host storage grows beyond 128
pieces; separate draws sharing the same guest entry survive independently.
The list retains native OAM priority order and still uses per-line palette,
VRAM and overlay state. Guest actor counts and simulation are not expanded.
Draw origins are tracked per OAM piece; Yoshi's separate head/body passes retain both origins.
The shared wing routine uses the visible horizontal bounds and records each
completed wing. Single-tile generic draws record their exact OAM piece before
composite sprites advance the allocation. This keeps adjacent sprites' inferred
allocation spans from stealing a Piranha head. Final tile/attribute changes by
the caller are retained at the object boundary and latched at NMI. Native vertical
culling still applies.
Fireballs and Chuck baseballs have separate ownership/lifecycle hooks. Baseballs
use the expanded horizontal bounds with Screen-based spawning and retain native
despawn behavior with Original 4:3 spawning. Native initialization and allocation still
run once; the renderer does not replay simulation or shift the guest camera.

Screen-based activation remembers each successfully loaded placement until its
source leaves the view plus the 32-pixel lookahead. Native transformations can
clear the game's load flag while still visible (a shell-less Koopa entering a
shell calls the normal erase routine); that no longer creates another copy.
Failed allocations remain eligible for retry. Level transitions and a changed
sprite list reset the guard, and generators retain their original frontier.
An optional `SMWS` version-1 game chunk in RTLS snapshots preserves activation
history across save/load. Older saves are still accepted and use their native
loaded flags; duplicates already present in an older save are not deleted.
The release writes a combined `SMX1` extension containing both this history
and main's foreign-controller state. Legacy `SMWS` renderer saves and `SFW1`
main-branch saves remain readable; `tools/test_smw_savestates.py` checks both.

Ghost-house sprite-memory preset `$11` makes its two unused regular slots
available to single-tile Eeries and the five-Eerie factory. Their OAM allocations
use the two free tail tiles at `$F8/$FC`; `$00-$27` stays reserved for Mario and
his cape. Multi-tile enemies and moving holes retain the ordinary pool, and
Fishin' Boo keeps its reserved slots. A full placement pool
defers that record while the scan continues to later eligible records, so one
unavailable slot does not stall the whole visible area. Native initialization
and physics still run; Original 4:3 and vertical-level allocation stay native.

`tools/apply_renderer_hooks.py` injects small callbacks into generated banks
and checks every required site. Interpreter hooks cover fallback execution.
`recomp/renderer_aot_roots.c` preserves the compiled sites through regeneration.
SDL textures and OpenGL buffers follow the current host width.

Level presentation remains adaptive during outgoing stage, pipe and death
fades, and during a level's incoming fade. Shared fade modes retain the scene
that entered them; title and overworld fades keep their native presentation.
Frozen fade frames retain full OAM coordinates only while the exact uploaded
position and attributes still match. Loaders and snapshot restores clear that
association. Brightness and mosaic continue to come from the captured PPU.

The pinned engine resets host audio interpolation and occupancy history after
reset or snapshot loading. It waits for guest execution to rebuild the existing
four-block audio cushion before resuming delivery, with continuous short fades
across callbacks. It does not advance the SPC/DSP to fill the buffer. Disabled
window checks and tile-index division were removed from the renderer's hot
path so the tested 100:9 view can keep pace with the audio device.

## Verification

```sh
python tools/test_adaptive_renderer.py
python tools/test_adaptive_renderer.py --live --resize --audio
python tools/test_adaptive_renderer.py --live --output OpenGL --aspect 21:9
python tools/test_adaptive_renderer.py --live --scenario standing --window 2133x720
python tools/test_adaptive_sprite_parts.py --scenario wings --state /path/to/winged-block.sav
python tools/test_adaptive_sprite_parts.py --scenario plant --state /path/to/jumping-piranha.sav --window 2000x180
python tools/test_adaptive_mario.py --state /path/to/split-mario-ghost-house.sav
python tools/test_adaptive_sprite_parts.py --scenario lotus --state /path/to/lotus-baseballs.sav --window 2048x510
python tools/test_adaptive_renderer.py --live --scenario yoshi --window 2133x720 --state build-adaptive/playtest/saves/save0.sav
python tools/test_adaptive_renderer.py --live --scenario pipes --window 2048x352 --state build-adaptive/playtest/saves/save1.sav
python tools/test_adaptive_spawns.py --state build-adaptive/playtest/saves/save2.sav
python tools/test_adaptive_spawns.py --state build-adaptive/playtest/saves/save2.sav --window 2000x180
python tools/test_adaptive_background.py --window 2048x352
python tools/test_adaptive_background.py --window 2000x180
python tools/test_adaptive_background.py --window 1280x720
python tools/test_adaptive_transitions.py --scenario exit --state /path/to/right-exit.sav
python tools/test_adaptive_transitions.py --scenario pipe --state /path/to/yi2-pipe-colors.sav
python tools/test_adaptive_transitions.py --audio --state /path/to/right-exit.sav --window 2000x180
python tools/test_adaptive_ghost_house.py --state /path/to/ghost-house.sav
python tools/test_adaptive_ghost_house.py --state /path/to/ghost-house.sav --scenario standing
```

Standalone checks cover arbitrary geometry, level edges, Map16 quadrants and
screen addressing, all eight pipe definitions across four screen variants,
spawn lookahead/original policy, fireballs, OAM reuse and
coordinates beyond 2048, and actual HUD pixels at three wide ratios and two
camera positions. The generated hook pass is checked for idempotence.
Background pixel checks cover zero/full/half-speed parallax, odd camera
positions, both level edges, retained scanline offsets and Layer 2 terrain.

Live tests start with fresh isolated saves and use ordinary controller input
to reach Yoshi's Island 2. They record CSV diagnostics, screenshots and a
JSON report under `build-adaptive/smw-renderer-*`; no pausing is used. Recorded
validation for this experiment:

- One 2600-frame 4:3 run matched every native pixel with original spawning.
- A 4000-frame SDL/audio run rendered all 4000 frames, exercised Fit at 100:9
  and five live window sizes (342, 410, 682, 2134 and 512 render columns), and
  activated up to nine enemies beyond the original view.
- A 3400-frame fixed 21:9 OpenGL run exercised original activation.
- After fresh regeneration, the final 3400-frame Fit/Screen-based run rendered
  every frame at 2134 columns, with nine far enemies and the anchored HUD.
- The native-area comparison found zero unexplained differences. Wide HUD
  pixels are checked separately because they intentionally move. Corrections
  within the exact OAM rectangles of wrapped far objects are also classified
  separately; raw differences are retained in the CSV, not hidden.
- Actual launcher clicks exercised fixed/original and Fit/Screen-based
  choices and verified their persisted mod state. The isolated playtest
  launcher also showed the correct first-run defaults. Wide HUD captures were
  visually inspected at 100:9 and 32:9.

For subsequent playtests, keep **Screen-based** spawning enabled, including
when selecting a fixed wide aspect. Original 4:3 was checked during initial
validation; it remains a user option rather than the ongoing playtest policy.

The visibility scenarios additionally verify the opening platform before
Mario moves and a local F1 fixture with adult Yoshi left of the native view.
They independently decode the captured OBJ tiles and require the actual
rendered pixels: all eight Koopas (2044 opaque pixels across their heads and
bodies) and both Yoshi pieces (183 head pixels, 158 body pixels in the supplied
fixture). Both checks reject the original experimental build. The stationary
checks pass at the reported 2133x720 window and at 100:9. A subsequent
4000-frame resize/audio run also passes with zero unexplained differences.
The Yoshi fixture is local and must be supplied explicitly. No save or ROM
graphics are committed.

The pipe scenario copies the supplied F2 fixture and walks left, then right,
with Screen-based spawning. It obtains pipe colors from a captured native PPU
frame while both pipes are inside the original viewport, then compares 1296
interior pipe pixels in each of 15 captures as the pipes cross into the expanded
view. This exercises all four transient pipe pointer tables. The check rejects
the previous renderer and passes at the reported 2048x352 window and at 100:9,
with 19,440 pixel comparisons per run and zero unexplained native differences.
The checks account for the captured raster scroll offset separately from the
frame-start camera, and avoid the pipe edges overlapped by Yoshi and a flying
Koopa in the wider view.

The spawn regression starts from the reported F3 fixture. It checks whether
that saved game is paused before sending Start to resume it, then uses normal
movement to leave the existing duplicate enemies behind. After they despawn,
it approaches a fresh shell/Koopa pair and checks the actual shell-entry timer,
target slot, and subsequent sprite records. The baseline creates record 11
again immediately after shell entry; the fixed build retains only the occupied
shell. Both reported-width and 100:9 runs verify the occupied Koopa's rendered
head/body and save/reload a state whose native load flag has cleared. The 100:9
test stages the pair before widening so the native slot budget cannot prevent
the tested encounter from happening. All preparation and verification use
Fit with Screen-based spawning; original saves are copied and hash-checked.
Scripted input supports combined buttons such as `press left+b+y 100` and
`savestate N` as well as `loadstate N` for these repeatable checks.

The camera regression enters Yoshi's Island 2 from a fresh save and walks right
with Screen-based spawning. It compares 24 captures, including consecutive
frames across the reported 2095x720 window's camera threshold. Actual terrain
pixels are compared at fixed world positions to a native PPU reference;
captured sprite footprints exclude falling shells from the terrain samples.
Sky pixels are checked independently at the expected parallax displacement.
The old build fails as soon as the simulation camera advances past the uploaded
camera. Stationary scenery must now have exactly zero drift; the prior test's
two-pixel tolerance concealed the timing mismatch. Per-frame diagnostics also
check that the presentation camera agrees with the actual PPU scroll.
The latest terrain/sky checks pass at the reported width, 16:9 and during live
resizing, with zero unexplained native-control differences. The saved-pipe
regression also passes at 100:9. Scripted camera tests isolate controller
bindings, and the standalone tests vary the next simulation camera while
requiring the already-uploaded foreground, background and sprite positions to
stay fixed. Original 4:3 spawning is not used for these playtests.

Parallax intentionally changes the scenery within the original viewport too.
With diagnostics enabled, the renderer additionally composes that viewport
using the original background projection and compares it to the native PPU.
This control still checks terrain, sprites, priority and color math instead
of exempting background-shaped screen regions. CSV diagnostics retain raw
native differences, count the parallax changes separately, and report any
unexplained control differences. The sky regression independently verifies
the corrected projection against captured native pixels.

The sprite-parts regression copies the reported winged-block and jumping-Piranha
saves, verifies their hashes remain unchanged, and runs with Screen-based
spawning. Both pass at the reported 2095x720 window and at 100:9. Nine captures
check the block and both animated wings across the native viewport boundary
(1044 wing pixels outside the block's overlapping footprint). Five captures
check every opaque head pixel above the pipe in both animation poses (942
pixels), with a separate stem check. Both tests reject the previous build's
missing ownership. Native-control comparisons report zero unexplained
differences. Yoshi, pipe-color and camera regressions also pass after this fix.
These targeted saved scenes extend coverage, not the full-game testing claim.

The Volcano Lotus/baseball F1 regression checks all four plant pieces and three
approaching baseballs over six captures, including both expanded margins and
the transition into the native view. It verifies 2,604 opaque pixels and rejects
the previous build's stale leaf ownership. The standalone draw-list test renders
150 pieces that reuse one guest OAM entry, including caller changes to the final
palette, fade reuse and scene reset. The prior split-Mario scene and 1,150-frame
ghost-house route also pass with the host draw list enabled.

The transition regression uses the reported right-hand stage-exit save, or
the earlier Yoshi's Island 2 pipe-color fixture. The exit checks 97,280 far-view
pixels against the preceding scene scaled by the captured fade brightness.
The pipe route uses ordinary movement to enter the pipe, then checks expanded
scenery during both fades and native-control parity across the whole replay.
Standalone checks additionally cover every brightness value and unchanged OAM
ownership through frozen fades. Copies preserve the original saves.

The paced audio check runs for 35 seconds with a real audio device and no
benchmark flags or renderer capture overhead. It reports actual underflows,
dropped samples and intentional priming separately, and closes only its own
test process. On this Windows host, the F1 exit replay improved from 18
underflows to zero at the reported 886-column width. At 100:9 (2134 columns),
the final run also had zero underflows and dropped samples; startup and save
loading used six priming callbacks in total. The engine's
`tests/audio_delivery/run.ps1` checks PCM continuity, short-callback recovery,
three device rates and isolation from guest sample production. These are
delivery checks, not a claim that all music/instruments have been compared
against a synthesis reference. Existing benchmark-audio tests run unpaced and
should not be used to judge real-time audio stability.

The ghost-house check copies the reported F1 save and runs with Screen-based
spawning. At the reported 2048x672 window, all twelve later placements activate
at their expanded frontier during a 1150-frame run, including moving floor
holes and Eeries. The previous build loads two Eeries 54 and 58 frames late
and fails the same check. A stationary 100:9 check verifies the additional
slots and Fishin' Boo's reserved allocation. These runs check 4334 and 406
actual sprite pixels outside the native view, respectively, with zero
unexplained native-control differences. This adds one saved ghost-house scene
to the coverage; most testing remains in World 1-2.

Developer environment overrides are `SMW_RENDER_ASPECT=Fit` or `N:D`,
`SMW_ENEMY_SPAWN=adaptive|original`, and `SMW_RENDER_DIAGNOSTICS=<directory>`.
`SMW_RENDER_CAPTURE_FRAME` captures a frame plus a local raster dump, or accepts
comma-separated frame numbers for a sequence of numbered dumps;
`SMW_RENDER_CAPTURE_EVERY` controls periodic BMPs. Captures and ROM-derived
data are local artifacts and are not committed.
Each selected capture also writes `frame-NNNNNN.draws.json`, identifying retained
host pieces by guest OAM entry, final image, coordinates and source actor.
Frame diagnostics include the presentation and simulation cameras, captured
raster scroll and native-view offset. Diagnostics also record `spawns.csv` (candidates, successful loads, suppressed
reactivation and rearming) and `sprites.csv` (per-frame entity state).

## Remaining scope

Testing has focused mostly on **World 1-2 (Yoshi's Island 2)**. The rest of the
game has not yet been fully tested. The adaptive mod remains experimental
pending broader level and gameplay coverage.
The original 12 regular sprite slots remain, with two previously unused slots
reclaimed in ghost-house preset `$11`; very wide views can still exhaust the
available pool before every visible enemy activates.
Specialized sprite paths beyond the covered ownership hooks need more level
coverage. Vertical levels retain native activation policy. Title screens,
the overworld, Mode 7 and other unsupported PPU modes use the native view.
Co-op remains native-only. CPU composition and snapshot copies still impose
costs at extreme widths despite the tested 100:9 improvement. Windows and Linux
release packages have passed focused launcher and gameplay checks. macOS/Visual
Studio builds and an entire-game playthrough have not been validated here.
