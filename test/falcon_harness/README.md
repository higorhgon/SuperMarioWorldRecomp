# Captain Falcon controller harness

This standalone regression harness links only the portable controller. It does
not link the SNES runtime, generated game code, framework, or external assets,
which separates move-logic regressions from the later SMW collision adapter.

Run from a Visual Studio 2022 developer prompt, or double-click `build.bat`.
It runs all scripts and writes ignored `trace_*.csv` files. The scripts cover
locomotion, short/full hops, aerials, Falcon Punch, Falcon Kick, Falcon Dive,
host collisions, hitboxes, audio cues, and save-state round trips.
