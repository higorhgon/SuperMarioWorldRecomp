# SuperMarioWorldRecomp

> _This recompilation is a **byproduct of developing
> [snesrecomp](https://github.com/mstan/snesrecomp)** — the games are the proving ground, the framework is the goal.
> **These are in-development previews, not finished ports — expect rough
> edges**, and depth will keep landing over months, not days. My time for any
> one title is limited, so I ask for your patience. Contributions are welcome —
> testing, issues, and PRs to the game or framework all help and will
> accelerate this game's polish. More on the why at:
> [Recomp + AI: 5 Months Later »](https://1379.tech/recomp-ai-5-months-later/)_

Static recompilation of *Super Mario World* (SNES) into native C,
using the [snesrecomp](https://github.com/mstan/snesrecomp) framework.
This repo is the per-game side: the runtime, the recompiled C output,
the per-game `.cfg`, and the build glue.

## What "static recompilation" means here

The 65816 CPU code from the ROM is statically translated to C — every
function the game runs on the SNES's main CPU is a real generated C
function in `src/gen/`. **The rest of the SNES is not recompiled** —
it's hardware. PPU rendering, the APU / SPC700 audio coprocessor, DMA
and HDMA channels, hardware register I/O, and bank-mapping all run
through a C reimplementation of the SNES hardware in
`snesrecomp/runner/src/snes/` — a [LakeSnes](https://github.com/elzo-d/LakeSnes)-derived
core (the same emulator family the [snesrev](https://github.com/snesrev)
reverse-engineered ports use), with individual algorithms credited to
snes9x. This is the same model other static-recomp projects (N64Recomp,
snesrev/zelda3, etc.) use: recompile the CPU, emulate the silicon. If
you expected the PPU and APU to be recompiled
too, they aren't — and a static recompiler can't recompile them
because the SNES PPU has no instruction stream and the SPC700 is a
separate processor with its own firmware that the cartridge uploads
to a separate chip.

## Current status: believed fully playable

Hand-verified end-to-end through:
- **Yoshi's Island (World 1)** including Iggy's castle boss.
- **Donut Plains (World 2)** end-to-end.
- **Vanilla Dome (World 3)** in progress at time of writing.

No catastrophic visible regressions surfaced through these worlds.
Two runtime tripwires (the M/X claim verifier and the
async-cpu->m_flag/x_flag-write detector) are armed at boot and have
not latched on the verified worlds. An off-rails event was captured
in `BufferScrollingTiles_Layer1_VerticalLevel_M1X1` during Donut
Plains castle (bank-out-of-range pointer read; runtime mirrors the
read to a safe location and gameplay continues) — see
[`ISSUES.md`](ISSUES.md) for the bucketed capture and the
`offrails_get` TCP query.

Worlds 4–7 and special content (Star Road, Special World) are not
yet hand-verified but are expected to play similarly. If you hit a
visible regression, please open an issue with a savestate and the
`offrails_get` / `mx_async_check_get` JSON snapshots.

Active development; expect:
- Some branches don't build; only `main` is guaranteed to build.
- Internal docs (`ISSUES.md`, `ENHANCEMENTS.md`) assume context.
- APIs and recompiler output change without notice.

See [`RELEASE.md`](RELEASE.md) for the latest release notes.

## Quick start (pre-built release)

1. Download the latest `SuperMarioWorldRecomp-windows-x64.zip` from
   [Releases](../../releases) and extract it.
2. Run `smw.exe`. On first launch a file picker asks for your
   **legally-obtained** Super Mario World (USA) ROM (`.sfc` / `.smc`).
   The path is remembered in `rom.cfg` next to the exe.
3. Edit `keybinds.ini` (auto-generated next to the exe on first run)
   to remap keys, then restart.

The ROM is **never** redistributed — supply your own dump.

## Optional Lua scripting

The Windows/Linux packages include an opt-in localhost Lua server and
a `lua/` folder with a **100-fireballs-per-second** hold-to-fire example.
See [lua/README.md](lua/README.md) for activation and controls. Nothing runs or
listens during ordinary play. Source builds enable this with
`-DSNESRECOMP_ENABLE_LUA=ON`; the framework option defaults OFF.

## Widescreen

Enable **SMW Adaptive Widescreen** in the one-player launcher's **Mods** page.
Its two controls are **Aspect ratio** (Fit to screen by default, plus fixed
4:3 through 100:9 choices) and **Enemy spawn behavior** (Screen-based by
default, or Original 4:3 activation). The HUD anchors automatically: lives and
bonus counters left, reserve item centered, TIME/coins/score right.

This experimental host renderer replaces the former PPU-expansion mod. Fit
follows the live window or fullscreen aspect, with native pixel proportions
and 224 lines; 100:9 renders 2134 columns without wrapping enemy coordinates.
Title screens, the overworld, transitions, and unsupported PPU modes retain
their native view. The original enemy-slot pool still limits how many enemies
can be active across very wide views.

See [the renderer guide](docs/adaptive-renderer.md) for building, isolated
playtesting, architecture, checks, and current limitations. Old `Widescreen`
and `WidescreenHud` INI fields no longer control this mod.

## Simultaneous co-op build

The repository also produces a separate `SuperMarioWorldCoopSNESRecomp` build. It
uses the simultaneous co-op gameplay patch while keeping the normal legal ROM
flow: select an untouched Super Mario World (USA) ROM in the launcher or pass
it on the command line.

On first use, the executable verifies the stock ROM and applies the bundled IPS
delta. The IPS contains the co-op hack's changed bytes, but not a complete ROM.
The generated file is written beside the executable as
`<stock-rom-name>.coop.sfc`; that generated co-op ROM is what the runtime loads.
Subsequent launches reuse it after verifying its CRC. Both headerless `.sfc`
dumps and 512-byte-headered `.smc` dumps are accepted as input. To force a clean
repatch, delete the generated `.coop.sfc` file.

Player 1 and Player 2 are active simultaneously in levels. Connect two SDL
game controllers, or configure the `[Player1]` and `[Player2]` keyboard
bindings in `keybinds.ini`. The stock ROM and generated `.coop.sfc` remain
local and must not be redistributed.

Widescreen is currently disabled in the co-op build, and its launcher omits the
aspect-ratio control. Co-op always runs in **Standard (4:3)** even if an older
configuration or `SNESRECOMP_WIDESCREEN` requests another mode. The experimental
IPS-specific hooks remain in the source for future work, but extended terrain
streaming is not yet reliable during normal scrolling.

### Network co-op

The co-op executable supports two-player delay-sync netplay through
`snesrecomp`'s `recomp-net` integration and recomp-ui. Both players must use the
same build and a matching verified SMW (USA) ROM.

1. Start `SuperMarioWorldCoopSNESRecomp` and open **Netplay** in the launcher.
   The first visit asks for a player name and saves it for later launches.
2. Choose **Host Lobby**. **Online** publishes the room through the configured
   lobby server; **LAN / Direct IP** advertises it directly on the local
   network. **Create** immediately opens the waiting room.
3. The guest joins the matching **Super Mario World Co-op** room. Once both
   seats are occupied, the host selects **Play**; the guest does not need a
   separate ready action.
4. The host controls Mario (network slot 1) and the guest controls Luigi
   (network slot 2). Each machine uses its Player 1 keyboard/controller by
   default, so the guest does not need separate Player 2 bindings.

Closing the game or pressing Escape during a network match returns both
players to the room for a rematch. Use **Leave Lobby** to disconnect instead.

The simulation waits for confirmed inputs before every frame. Turbo, pause,
and local reset are disabled during a session; save/load synchronization is
host-authoritative. LAN uses UDP directly. Internet games may require a build
configured with `-DSNESRECOMP_NET_ICE=ON`, which includes libjuice-based NAT
traversal, when the peers cannot reach each other directly.

For a launcher-free loopback or CI smoke test, start two instances with the
same session ID and opposite ports:

```powershell
# Host
$env:SNES_NETPLAY='1'; $env:SNES_NET_SLOT='0'
$env:SNES_NET_SESSION_ID='4242'; $env:SNES_NET_TRANSPORT='lan'
$env:SNES_NET_BIND='0.0.0.0:7777'; $env:SNES_NET_PEER='127.0.0.1:7778'
$env:SNES_NET_TEST_TICKS='600'
.\SuperMarioWorldCoopSNESRecomp.exe .\smw.sfc

# Guest (in another terminal)
$env:SNES_NETPLAY='1'; $env:SNES_NET_SLOT='1'
$env:SNES_NET_SESSION_ID='4242'; $env:SNES_NET_TRANSPORT='lan'
$env:SNES_NET_BIND='0.0.0.0:7778'; $env:SNES_NET_PEER='127.0.0.1:7777'
$env:SNES_NET_TEST_TICKS='600'
.\SuperMarioWorldCoopSNESRecomp.exe .\smw.sfc
```

### Co-op hack attribution

This build distributes an IPS delta for **Super Mario World - 2 Player
Simultaneous Co-op Hack**. Original hack credits:

- **Noobish Noobsicle** - creator of the original SMW co-op hack
- **Bloony Fox** and **NesDraug** - developed it into the full hack used here

Source and original credits:
[Romhack Plaza - Super Mario World - 2 Player Simultaneous Co-op Hack](https://romhackplaza.org/romhacks/super-mario-world-2-player-co-op-hack-snes/).
This recompilation project is not affiliated with or endorsed by the hack
authors.

## Controls (default `keybinds.ini`)

| SNES button | Default key |
|-------------|-------------|
| D-Pad       | Arrow keys |
| A           | X |
| B           | Z |
| X           | S |
| Y           | A |
| L           | C |
| R           | V |
| Start       | Enter |
| Select      | Right Shift |

Player 2 is unbound by default — fill in keys in `keybinds.ini` to enable a
second keyboard player. In the co-op build, the two bindings drive Mario and
Luigi at the same time rather than taking turns.

**Xbox / PlayStation / Switch Pro controllers** are auto-detected via
SDL_GameController (XInput on Windows). Plug it in before launching,
or hot-plug after. Default Xbox mapping is **position-true**: the
physical button position matches a SNES pad — so Xbox A (south face)
sends SNES B, Xbox B (east face) sends SNES A. To rebind, edit the
`[GamepadMap]` section of `config.ini` (auto-generated next to the exe
on first run); the recognized names and the full mapping table are
in [`CONTROLLER.md`](CONTROLLER.md).

System shortcuts (configured in `config.ini`'s `[KeyMap]` section):

| Action          | Default     |
|-----------------|-------------|
| Save state 1-10 | Shift+F1..F10 |
| Load state 1-10 | F1..F10 |
| Toggle pause    | P |
| Reset           | Ctrl+R |
| Toggle fullscreen | Alt+Enter |
| Turbo (fast-forward) | Tab |
| Toggle renderer | R |
| Display perf    | F |

## Building from source

Prerequisites: Windows 10+, Visual Studio 2022 (with C++ desktop
workload), Python 3.9+ on PATH, and `rustup` for regeneration.

```bash
git clone --recurse-submodules https://github.com/mstan/SuperMarioWorldRecomp
cd SuperMarioWorldRecomp
```

Regenerate the desired variant from your legally obtained stock ROM, then
build it. The generated C is intentionally untracked:

```bash
# Normal 1P build (the default)
bash tools/regen.sh --stock
msbuild smw.sln /p:Configuration=Production /p:Platform=x64 /m

# Simultaneous co-op build
bash tools/regen.sh --coop
msbuild smw.sln /p:Configuration=CoopProduction /p:Platform=x64 /m
```

The normal configuration remains `smw.exe`; the additive configuration creates
`SuperMarioWorldCoopSNESRecomp.exe` in its own output directory and copies
`smw_coop.ips` beside it. CMake builds the normal target by default; configure
with `-DSMW_BUILD_COOP=ON` to make both targets available in one build tree.

The netplay-enabled co-op target uses CMake so it can link recomp-net. A sibling
engine worktree can be selected without replacing the checked-in submodule:

```powershell
$engine = 'F:\path\to\snesrecomp-worktree'
$ui = 'F:\path\to\recomp-ui-worktree'
$sdl3 = 'F:\path\to\SDL3'
$env:SNESRECOMP_ROOT = $engine
bash tools/regen.sh --coop --no-tests
cmake -S . -B build-netplay -DSMW_BUILD_COOP=ON `
  -DSNESRECOMP_ROOT="$engine" -DRECOMP_UI_ROOT="$ui" `
  -DCMAKE_PREFIX_PATH="$sdl3" `
  -DSMW_NETPLAY_ICE=ON
cmake --build build-netplay --config Release `
  --target SuperMarioWorldCoopSNESRecomp --parallel
```

SDL3 is the default desktop backend. To build the maintained compatibility
fallback, configure a separate tree with
`-DSNESRECOMP_SDL_BACKEND=SDL2`.

### Regenerating the recompiled C (contributors)

If you change anything under `recomp/bank_*.cfg`, the snesrecomp
framework, or otherwise need to re-run the recompiler:

1. Drop a legally-obtained `smw.sfc` at the repo root (`.gitignore`
   excludes it).
2. Run `bash tools/regen.sh --stock` for the normal build and/or
   `bash tools/regen.sh --coop` for co-op. Stock emits to `src/gen/` from the
   vanilla SMW (USA) ROM. Co-op applies the bundled IPS to a throwaway verified
   ROM, layers the small CFG fragments in `recomp/coop/`, and emits
   independently to `src/gen-coop/`. It builds and requires the fast native
   analyzer by default; set
   `SNESRECOMP_ANALYSIS_BACKEND=python` only to use the slower reference path.
3. Rebuild as above.

(Build and run instructions are not yet stable — see scripts under
`tools/` and notes in `docs/` for the current shape, but expect them
to drift.)

## MSU-1 audio

The normal 1P build supports CD-quality MSU-1 streaming music using a stock
SMW (USA) ROM. Regeneration no longer applies Conn's audio-only "SMW MSU-1"
patch; the preloaded MSU-1 Audio Mods package activates trusted host-side code
that observes SMW music commands and drives the runner's MSU-1 device directly.
At runtime, no pack means authentic SPC audio; a matching PCM pack plus the
MSU-1 Audio mod means streamed music. Packs for SMW MSU+ or SMW MSU-1 Plus
Ultra are not interchangeable with this audio-only track map. Full credit and
pack details are in [`recomp/msu1/ATTRIBUTION.md`](recomp/msu1/ATTRIBUTION.md).

MSU-1 is disabled in the simultaneous co-op build for now.

## PortMaster / Anbernic H700 (muOS)

Experimental packaging for Anbernic H700-chip handhelds - the RG34XX,
RG34XX H, and RG34XXSP family - running muOS, installed as a PortMaster
port. **This has not been tested on real hardware.** Everything below was
built and reasoned about on a desktop machine with no H700 device
available; please try it and report back what does and doesn't work.

### Target devices

The RG34XX family's physical panel is 720x480, i.e. a **3:2** aspect
ratio - narrower than typical 16:9 handhelds. Getting SMW to fill that
panel correctly at boot needs no source changes: it falls out of a
feature that already exists.

### Why Fullscreen=1 + Fit-to-screen give correct 3:2 for free

`config.ini`'s `Fullscreen = 1` (desktop fullscreen, not `2`'s "fullscreen
w/ mode change" - riskier on an embedded panel with exactly one fixed
mode) makes the game's drawable window match the panel's native
720x480 exactly. **SMW Adaptive Widescreen**'s "Fit to screen" mode
(`mode = "adaptive"` in the mod's manifest; see
[`docs/adaptive-renderer.md`](docs/adaptive-renderer.md)) "follows the
drawable window, including fullscreen and live resizing" and keeps
native pixel proportions. Combine the two and the rendered image is
already the panel's real 3:2 - no new aspect-ratio logic needed, just
shipping the right defaults.

The catch: mods are not `config.ini` keys. They're enabled and configured
through a separate `mods/state.toml` next to `config.ini`, and the
manifest's own defaults ship the feature **off** (`default_enabled =
false`) and, if turned on some other way, defaulting to a fixed `16_9`
mode rather than adaptive Fit. [`portmaster/default-config/`](portmaster/default-config/)
ships a `state.toml` that enables the mod with `mode = "adaptive"` and
`spawn = "adaptive"` (the same structure `tools/run_adaptive_renderer.ps1`
writes for isolated playtesting), plus a copy of `mods/preloaded/packages/`
alongside it - the package files themselves have to be physically present
next to `state.toml` for the mod to be selectable at all, referencing its
id is not enough.

### What's verified and what isn't

Verified locally on x86_64 Linux in this sandbox (no ROM available here -
see below):

- `bash tools/test_adaptive_renderer.py --build build-adaptive` builds
  `test/renderer/renderer_test.c` against `src/smw_renderer.c`,
  `src/smw_renderer_hooks.c`, and `src/smw_video.c` (no `src/gen`, no ROM)
  and passes, including the geometry assertions for `SmwCalculateViewport`/
  `SmwDestination` that back Fit-to-screen. This is the same command
  [`.github/workflows/arm64-linux.yml`](.github/workflows/arm64-linux.yml)
  runs on `ubuntu-24.04-arm` (aarch64), matching the H700's CPU
  architecture.
- `portmaster/port.json` is valid JSON and `portmaster/default-config/mods/state.toml`
  is valid TOML (checked with Python's `json`/`tomllib`).
- The `state.toml` schema above matches `tools/run_adaptive_renderer.ps1`
  exactly, which is the project's own dev helper for this same renderer.

**Not verified - cannot be, from this environment:**

- Building the actual `SuperMarioWorldSNESRecomp` game binary for aarch64.
  `CMakeLists.txt` unconditionally includes the `snesrecomp`/`recomp-ui`
  submodules and then hard-fails with `FATAL_ERROR` if `src/gen/` is
  empty - this project has no `-DBUILD_GAME=OFF`-style escape hatch, so
  producing a playable binary needs a maintainer's own legally dumped ROM
  run through `tools/regen.sh`, on or for aarch64. None of that can exist
  in this sandbox or a public CI runner.
- Whether the packaged `mods/state.toml` + `mods/packages/` actually get
  picked up by a real running game - this was checked by reading
  `docs/adaptive-renderer.md`, the manifest, and the dev helper script that
  writes the identical structure, not by launching the game (no ROM here).
- Anything about actually running on an RG34XX/muOS: window creation,
  controller mapping, audio, performance, and the video driver PortMaster
  picks at runtime.
- **The GPU/driver path.** `src/opengl.c`'s `OpenGLRenderer_Init()`
  hard-fails (`Die("You need OpenGL 3.3")`) unless it gets an OpenGL 3.3
  core context, and that path runs whenever `OutputMethod = OpenGL` is
  selected at all - not only when a shader is set. The H700's Mali-G31
  runs Mesa's Panfrost driver, whose desktop OpenGL ceiling for
  Bifrost-class GPUs is currently **3.1** - below that requirement. The
  packaged config below ships `OutputMethod = SDL` instead, which uses
  SDL's own accelerated `SDL_Renderer` path and does not force a 3.3 core
  context. **Practical takeaway: leave `OutputMethod = SDL` on this
  handheld** - `OpenGL` is expected to fail to initialize on this
  GPU/driver combination. If `SDL` gives you a black screen or poor
  performance on real hardware, try `SDL-Software` next - `config.ini`'s
  own upstream comment already calls software rendering out as sometimes
  faster "on Raspberry Pi", i.e. other weak-GPU ARM SBCs, before trying
  `OpenGL`.

### Building and packaging

You need your own legally dumped Super Mario World (USA) ROM and an
aarch64 build environment (the RG34XX family is Cortex-A55, so an aarch64
host or a cross-compiling toolchain both work):

```bash
git clone --recurse-submodules https://github.com/higorhgon/supermarioworldrecomp
cd supermarioworldrecomp
# stage your verified ROM as smw.sfc, then:
bash tools/build-linux.sh --regen --no-package --out build-linux-prod
bash tools/package_portmaster.sh --build build-linux-prod
```

`tools/package_portmaster.sh` reuses `tools/build-linux.sh` for the actual
compile rather than duplicating its logic; it does not build anything
itself, only stages a build you already produced. It deliberately does
**not** ship the `.AppImage` `tools/build-linux.sh` normally produces -
AppImages need FUSE/squashfuse to mount at runtime, which minimal
handheld CFW images like muOS often don't ship. `--no-package` stops
`build-linux.sh` right after compiling, leaving a plain ELF plus the
`assets/` and `mods/packages/` it already stages beside the binary, which
`package_portmaster.sh` then copies into the PortMaster layout together
with the tracked defaults under [`portmaster/`](portmaster/).

This produces `release-portmaster/SuperMarioWorldRecomp-portmaster-<version>.zip`,
laid out the way PortMaster expects (verified against
[PortsMaster/PortMaster-New](https://github.com/PortsMaster/PortMaster-New)'s
own published ports): `port.json` and `SuperMarioWorldRecomp.sh` at the zip
root, alongside a `SuperMarioWorldRecomp/` folder holding the binary, its
assets, mod catalog, and a default `config.ini` / `mods/state.toml` with
`Fullscreen=1` and Fit-to-screen already enabled, so a first launch is
already fullscreen and correctly 3:2 without visiting the launcher's Mods
page first.

### Installing on muOS

1. Copy the zip's contents onto the SD card's PortMaster ports folder
   (typically `SD1:/roms/ports` under muOS), or install it through muOS's
   own PortMaster app if you're distributing it that way -
   [see muOS's PortMaster docs](https://muos.dev/) for the app-based flow.
2. Drop your own `Super Mario World (USA).sfc`/`.smc` ROM into the
   `SuperMarioWorldRecomp/` folder that was installed.
3. Launch **Super Mario World Recomp** from muOS's Ports list.

The launcher script (`portmaster/SuperMarioWorldRecomp.sh`) mirrors the
ROM auto-detection the official Linux AppImage already uses
(`tools/build-linux.sh`'s `AppRun`): it looks for a `.sfc`/`.smc` file
next to the binary and caches its path in `rom.cfg`, the same file the
game's own launcher UI reads. It deliberately does not hardcode
`SDL_VIDEODRIVER`, since real PortMaster ports disagree on the right
default for muOS (some force `x11`, some force `kmsdrm`, some only
override for a detected vendor `mali` driver) and which is correct here
cannot be checked without the actual device; the script has a commented
override for either, with the reasoning, if you hit a black screen.

## Repo layout

- `src/` — runtime C (CPU state, runtime helpers, hand-written
  bodies for things the framework doesn't yet recompile).
- `src/gen/` — recompiler output (do not hand-edit).
- `src/gen-coop/` — separate co-op recompiler output (do not hand-edit).
- `recomp/` — per-bank `.cfg` files describing what the framework
  cannot yet derive from the ROM (data regions, calling conventions,
  rare hints).
- `recomp/coop/` — co-op CFG overlays, distributed IPS, and attribution.
- `snesrecomp/` — pinned framework submodule.
- `tools/` — build, regen, audit, and triage scripts.
- `docs/` — design / debugging notes (internal-facing, may be stale).
- `third_party/` — vendored deps with their own licenses.
- `portmaster/` — PortMaster port sources (launcher script, `port.json`,
  default `config.ini`/`mods/`) for Anbernic H700 handhelds; see
  "PortMaster / Anbernic H700 (muOS)" above.

## Acknowledgements

This port did not start from scratch. It stands on prior
reverse-engineering and emulation work, and we're grateful for it:

- **[IsoFrieze/SMWDisX](https://github.com/IsoFrieze/SMWDisX)** — the
  Super Mario World disassembly used as the basis for this port. SMWDisX
  is the source of the symbol names, the RAM/variable map, and the
  per-bank function boundaries, and it serves as the differential
  conformance oracle for the recompiled output (see
  [`tools/smwdisx_compare.py`](tools/smwdisx_compare.py) and the vendored
  `SMWDisX/` clone). SMWDisX in turn credits mikeyk's original 2013
  disassembly and loveemu's SPC700 sound-engine work.
- **[snesrev](https://github.com/snesrev)** (`snesrev/smw`,
  `snesrev/zelda3`) — the runner and surrounding ecosystem were heavily
  based on the snesrev reverse-engineered ports: the "port the CPU code
  to C, emulate the rest of the silicon, verify against a reference
  emulator" model, the C runtime structure, and the SPC-image audio path.
- The C SNES hardware core under `snesrecomp/runner/src/snes/` derives
  from **[LakeSnes](https://github.com/elzo-d/LakeSnes)** (elzo-d), the
  emulator snesrev's projects vendor, with algorithms credited inline to
  **snes9x**.

See the [snesrecomp framework](https://github.com/mstan/snesrecomp)
README for the full framework-side attribution.

## Discord

https://discord.gg/S4MvUGQFwd

## License

PolyForm Noncommercial 1.0.0. See `LICENSE`. Code in this repo is original
except where noted in **Acknowledgements** above; vendored dependencies under
`third_party/` (and the `SMWDisX/` disassembly clone) retain their own
licenses.

The SMW ROM and any data extracted from it are **not** in this
repo and are not licensed for redistribution.

---

<p align="center">
  <sub><b>R.A.I.D. — Retro AI Development</b> · a Discord for AI-assisted retro reverse-engineering, decomp &amp; recomp</sub>
</p>

<p align="center">
  <a href="https://discord.gg/Ad9BwSzctP"><img src=".github/raid-discord.png" alt="Join the Retro AI Development (R.A.I.D.) Discord" width="200"></a>
</p>
