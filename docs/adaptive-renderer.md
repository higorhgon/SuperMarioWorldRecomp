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
stays 256x224. Immutable per-scanline registers, palette, VRAM and OAM preserve
the game's IRQ/HDMA timing. Mode 1 tiles use captured VRAM in the native area
and the full level's Map16 data outside it. Layer 2 level modes also use
Map16; repeating backgrounds retain their captured scroll and tilemap.
Horizontal pipes select their ROM graphics variant from the pipe's world
screen. SMW's live pipe pointer table belongs to the strip currently being
streamed, so reusing it across the expanded view would change pipe colors
as the camera moves. Vertical and background-only definitions retain their
native pointer lookup.

`src/smw_renderer_hooks.c` records signed sprite coordinates at the guest's
draw paths and changes the optional activation/despawn policy. OAM ownership
is paired with the exact emitted position and attributes, then latched before
NMI, so OAM reuse cannot inherit a stale far-away owner. Draw origins are
tracked per OAM piece; Yoshi's separate head/body passes retain both origins.
Fireballs have a separate ownership/lifecycle hook. Native initialization and allocation still
run once; the renderer does not replay simulation or shift the guest camera.

`tools/apply_renderer_hooks.py` injects small callbacks into generated banks
and checks every required site. Interpreter hooks cover fallback execution.
`recomp/renderer_aot_roots.c` preserves the compiled sites through regeneration.
SDL textures and OpenGL buffers follow the current host width.

## Verification

```sh
python tools/test_adaptive_renderer.py
python tools/test_adaptive_renderer.py --live --resize --audio
python tools/test_adaptive_renderer.py --live --output OpenGL --aspect 21:9
python tools/test_adaptive_renderer.py --live --scenario standing --window 2133x720
python tools/test_adaptive_renderer.py --live --scenario yoshi --window 2133x720 --state build-adaptive/playtest/saves/save0.sav
python tools/test_adaptive_renderer.py --live --scenario pipes --window 2048x352 --state build-adaptive/playtest/saves/save1.sav
```

Standalone checks cover arbitrary geometry, level edges, Map16 quadrants and
screen addressing, all eight pipe definitions across four screen variants,
spawn lookahead/original policy, fireballs, OAM reuse and
coordinates beyond 2048, and actual HUD pixels at three wide ratios and two
camera positions. The generated hook pass is checked for idempotence.

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

Developer environment overrides are `SMW_RENDER_ASPECT=Fit` or `N:D`,
`SMW_ENEMY_SPAWN=adaptive|original`, and `SMW_RENDER_DIAGNOSTICS=<directory>`.
`SMW_RENDER_CAPTURE_FRAME` captures a frame plus a local raster dump, or accepts
comma-separated frame numbers for a sequence of numbered dumps;
`SMW_RENDER_CAPTURE_EVERY` controls periodic BMPs. Captures and ROM-derived
data are local artifacts and are not committed.

## Remaining scope

This is a playable renderer experiment, not a full-game compatibility claim.
The original 12 regular sprite slots and level-specific reserved allocations
remain; very wide views can exhaust them before every visible enemy activates.
Specialized sprite paths beyond the covered ownership hooks need more level
coverage. Vertical levels retain native activation policy. Title screens,
the overworld, transitions, Mode 7 and other unsupported PPU modes use the
native view. Co-op remains native-only. CPU composition and snapshot copies
have not been performance-tuned for extreme widths. Linux/macOS/Visual Studio
builds and an entire-game playthrough have not been validated here.
