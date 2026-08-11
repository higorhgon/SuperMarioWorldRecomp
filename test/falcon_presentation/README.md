# Falcon presentation harness

Build without the game or framework:

```powershell
gcc -std=c11 -Wall -Wextra -Werror test/falcon_presentation/falcon_presentation_harness.c src/mods/falcon/falcon_presentation.c -lm -o _triage/falcon_presentation_harness.exe
_triage/falcon_presentation_harness.exe
```

The first run creates a synthetic v4 blob in memory, rejects malformed variants,
samples its root motion, and compares a pitch-strided ARGB8888 framebuffer hash.
When an externally verified owner cache is available, use:

```powershell
_triage/falcon_presentation_harness.exe --blob <external-cache>/falcon_runtime.bin _triage/falcon_owner_dive.bmp
```

Create the six-pose review sheet (Idle, Run, Punch active, Kick active, Dive
frame 13, and left-facing Idle) with:

```powershell
_triage/falcon_presentation_harness.exe --sheet <external-cache>/falcon_runtime.bin _triage/falcon_owner_pose_sheet.bmp
```

Create a four-frame host-side death-tumble sheet with:

```powershell
_triage/falcon_presentation_harness.exe --death-sheet <external-cache>/falcon_runtime.bin _triage/falcon_owner_death_sheet.bmp
```

Create the feedback sheet (ground Kick and `DownSpecialAir`, each facing both
directions) with:

```powershell
_triage/falcon_presentation_harness.exe --feedback-sheet <external-cache>/falcon_runtime.bin _triage/falcon_owner_feedback_sheet.bmp
```

Create the run/kick cadence sheet (bind idle, four Run frames, then five
grounded Kick frames) with:

```powershell
_triage/falcon_presentation_harness.exe --motion-sheet <external-cache>/falcon_runtime.bin _triage/falcon_owner_motion_sheet.bmp
```

This is the focused review for a stable 32px mushroom-Mario reference scale:
the action silhouettes may extend or crouch naturally, but their mesh scale is
not recomputed per pose.

Create the attachment calibration sheet with the active Punch frames 42/48/54,
ground Kick frames 12/18/24, then aerial Kick frames 12/18/24 (left to right):

```powershell
_triage/falcon_presentation_harness.exe --attachment-sheet <external-cache>/falcon_runtime.bin _triage/falcon_owner_attachment_sheet.bmp
```

Create the carry-anchor sheet with a green 32px OAM-group outline centred on
the calibrated raised hand, first facing right then left:

```powershell
_triage/falcon_presentation_harness.exe --carry-sheet <external-cache>/falcon_runtime.bin _triage/falcon_owner_carry_sheet.bmp
```

The attachment indices are calibrated from the rendered owner-cache skeleton,
not raw DObj labels: Punch follows the striking hand, ground and aerial Kick
follow their visible active boots, and carry follows the raised hand. The
completed native carried-shell allocation is exactly two OAM entries (`$15EA`,
`$15EA+4`) from `StunnedShellDraw`. At prepare time the guest NMI/DMA has
already copied that OAM to the PPU, so the host test checks the transient PPU
pair (and its high-OAM bits), adjacent entries, and untouched guest WRAM.

The BMP is evidence only and must remain ignored. No blob, texture, model, or
animation data belongs in this repository.

`build_host_runtime_test.bat` compiles the game-owned host bridge without the
generated game and checks controller state/frame/facing pose mapping. The full
runtime cache and OBJ suppression contract is in
`docs/falcon_owner_cache_runtime.md`.

`python -m unittest test/falcon_presentation/test_host_runtime_contract.py`
guards the no-shell cache-helper process boundary, including hostile path
metacharacters.
