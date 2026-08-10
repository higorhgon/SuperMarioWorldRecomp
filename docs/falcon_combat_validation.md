# Falcon combat TCP validation status

`test/falcon_validation/falcon_combat_observation.json` is a deterministic TCP
boot-to-GM14 observation script. It captures before and during the Falcon
Punch timing window and records native normal-sprite status (`$14C8-$14D3`) and
score-sprite state (`$16E1-$1704`) with screenshot hashes. Run it only as an
observation command:

```
python tools\falcon_validation.py --scenario test\falcon_validation\falcon_combat_observation.json --owner-rom <approved-owner-rom> --out _triage\falcon-combat-observation
```

It is intentionally **not** a native-consequence pass criterion yet. The
stock title-to-level route exposes no reviewed deterministic enemy or block
fixture. The TCP server has `read_ram` and `write_ram`, but writing sprite,
Map16, score, or player-collision scratch directly would bypass native spawn
and collision initialization and is not safe evidence. TCP also has no FTRING
read/export command, so it cannot prove the controller attack state itself.

The current executable was already owned by another live validation process
during this tranche, so no competing TCP client was opened. The accepted
observable proof remains `test/falcon_combat_apply`: it mocks only the two
validated generated calls and checks attack geometry/facing, the native
M1X1/DB2 contracts, FTRING-facing `attack_connected`, native status mutation,
brick transaction, score/sound side effects, scratch restoration, player
Y-speed preservation, and nonbreakable rejection.

To upgrade this scenario to a pass gate, an owner must provide both a
game-owned deterministic fixture/seed API and a read-only FTRING TCP export.
No validation script should add a write-RAM seed as a substitute.
