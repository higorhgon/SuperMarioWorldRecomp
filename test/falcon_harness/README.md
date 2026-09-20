# Captain Falcon controller harness

This standalone regression harness links only the portable controller. It does
not link the SNES runtime, generated game code, framework, or external assets,
which separates move-logic regressions from the later SMW collision adapter.

Run from a Visual Studio 2022 developer prompt, or double-click `build.bat`.
It runs all scripts and writes ignored `trace_*.csv` files. The scripts cover
locomotion, short/full hops, aerials, Falcon Punch, Falcon Kick, Falcon Dive,
host collisions, hitboxes, audio cues, and save-state round trips.

After the `snesrecomp` submodule includes the foreign-controller API, run
`build_smw_adapter_test.bat` too. It validates raw SMW WRAM button mapping,
monotonic trace frames, the +Y-up/+Y-down conversion, native displacement and
wall/ceiling/floor feedback, and pipe handoff ownership.
