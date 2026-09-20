# SMW game policies

The adaptive renderer owns its surface in `src/smw_renderer.c`. It reads per-line
PPU snapshots and SMW Map16 data without expanding the native PPU tilemap.

Optional activation/despawn policy and signed sprite ownership are in
`src/smw_renderer_hooks.c`. `tools/apply_renderer_hooks.py` installs the
corresponding callbacks into generated C. CMake and Visual Studio run the
idempotent pass before compiling. `recomp/renderer_aot_roots.c` preserves the
required sites during regeneration. Missing sites fail the build.

The former `overrides/widescreen` renderer have been removed.
Do not edit generated banks by hand. To migrate an existing generated tree,
regenerate it with `tools/regen.sh --stock` before building this branch.

The separate Falcon hooks remain runtime-gated. `tools/apply_overrides.py`
installs only the Falcon manifest and its collision callbacks; it no longer
installs legacy widescreen patches.
