# Falcon combat bridge

`smw_falcon_combat_policy.{c,h}` converts a `ForeignAttackHitbox` from source
units into SMW world pixels using the existing `0.08` scale. It is deliberately
pure: it chooses the first overlapping supported ordinary target, rejects
bosses, hazards, dead sprites, and unsupported classes, and allows
`BREAK_BLOCKS` only for a host-classified genuine brick or turn block. Question
blocks, pipes, scenery, and unknown Map16 are always rejected.

`smw_falcon_combat_apply(cpu, attack, facing, hit)` is the follow-up native
seam. The adapter owner calls it once inside `SmwFalconAfterPhysics`, after
`$00:E92B` collision and before `snes_foreign_resolve`, passing the live
`ForeignState.facing`. This tranche wires that one hook and its two sources;
it does not change plugin ownership.

The attack is source-center-relative (+Y-up): its SMW center is
`player + (8,32) + (facing*offset_x, -offset_y)*0.08`, with width/height as
full extents. The policy tests both facings and vertical sign.

The live `$00:CD36` hook is restricted to `M1X1`, `DB=$00`, `D=$0000`.
Each native transaction saves/restores CPU registers and `$00-$0F` scratch;
the block route also restores `$98-$9C`. It makes only proven `M1X1`,
`DB=$02`, `D=$0000` generated calls:

- `$02:9404` `CheckPlayerAttackToNormalSpriteColl_029404`, the native
  spin-jump kill/status/score/contact-effect path. A support allowlist accepts
  only Koopa/shell families, Goomba/Paragoomba, and Buzzy Beetle; status and
  tweaker guards reject dead, boss, hazard, and special entities. It invokes
  at most one target. All admitted status-$08 targets use a documented
  conservative 16x24 union (IDs `$04-$07` are upright shelled Koopas; loose
  shells have statuses `$09/$0A` and are excluded). A resulting status transition alone sets
  `attack_connected`, including Falcon Dive's contact-only resolution.
- `$02:8752` `SpawnBounceSprite`, only when the attack overlaps the current
  `$00:E92B` collision block and native action `$04=7` proves brick, or current
  Map16 byte `$1693=$1E` proves turn block. It owns native Map16 mutation,
  score, debris and sound; question blocks, pipes, scenery and unknown values
  are rejected. `$7C-$7D` player Y speed is restored so a Falcon block attack
  never inherits native head-bounce velocity.

Run `test\falcon_combat\build.bat` and
`test\falcon_combat_apply\build.bat` for policy and mocked-native validation.
