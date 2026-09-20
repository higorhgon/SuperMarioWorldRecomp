# SMW playtest workflow

Follow the project rules in `CLAUDE.md`.

Owner preference (2026-09-20): after publishing a tested SMW fix, stop the
running SMW process from this worktree and relaunch the updated build for the
owner. This restart is authorized; do not leave the owner playing the old
process or ask them to restart it themselves.

Use `tools/run_adaptive_renderer.ps1`, preserving the settings and saves in
`build-adaptive/playtest`. Keep Screen-based enemy spawning enabled for future
playtests. Limit process termination to this worktree's SMW executables.
