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

The BMP is evidence only and must remain ignored. No blob, texture, model, or
animation data belongs in this repository.

`build_host_runtime_test.bat` compiles the game-owned host bridge without the
generated game and checks controller state/frame/facing pose mapping. The full
runtime cache and OBJ suppression contract is in
`docs/falcon_owner_cache_runtime.md`.

`python -m unittest test/falcon_presentation/test_host_runtime_contract.py`
guards the no-shell cache-helper process boundary, including hostile path
metacharacters.
