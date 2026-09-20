# Falcon native shell-carry live validation

`tools/falcon_carry_validation.py` is an isolated, opt-in TCP evidence runner
for the Captain Falcon A-to-native-carry bridge.  It does not change game
logic, override manifests, or the shared showcase runner.

Run it against an already-built executable with the verified owner ROM:

```powershell
$env:SNESRECOMP_FALCON_CACHE = '<approved Falcon presentation cache>'
py -3 tools/falcon_carry_validation.py `
  --exe build-falcon/SuperMarioWorldSNESRecomp.exe `
  --rom smw.sfc `
  --owner-rom F:/Projects/SmashBrosDecomp/baserom.us.z64 `
  --out _triage/falcon_carry_live `
  --require-build
```

The runner starts paused, reaches game mode `$14`, and uses the bounded debug
server `write_ram` command only to initialize slot 0 as a stock green shell
(`$04`).  It clears the slot first, writes the number, position, speed,
subposition, working, OAM, and carry-flag tables, and commits its status last.
This is a test fixture, not an alternative carry implementation: stock SMW
continues to own collision eligibility, status `$0B` positioning, release,
throw, and set-down.
The focused adapter harness separately proves the ordering contract: physical
A is offered as native Y at the late normal-sprite seam, while native
`CheckPlayerToNormalSpriteColl` remains the only code that may promote an
eligible status `$09` sprite to `$0B`.  The live fixture begins carried so it
can deterministically inspect the native `$0B` position and both release
branches without claiming a synthetic overlap is a stock-level collision.

The generated `evidence.json` asserts game mode `$14`, sprite number, status,
carry flags, velocities, position, and player state at four screenshots:

- `shell_approach.bmp`: an eligible status `$09` green shell beside Falcon;
- `shell_carried.bmp`: A held, native status `$0B` and carry flag set;
- `shell_thrown.bmp`: A released, native status `$0A` and nonzero X speed;
- `shell_set_down.bmp`: A released with Down, native status `$09`.

Inspect the four BMPs alongside the WRAM evidence.  The visual carry offset is
provided by the stock status-$0B lifecycle; the runner never relocates the
sprite after seeding.
