# Falcon combat bridge

`smw_falcon_combat_policy.{c,h}` converts a `ForeignAttackHitbox` from source
units into SMW world pixels using the existing `0.08` scale. It is deliberately
pure: it projects supported ordinary targets, rejects
bosses, hazards, dead sprites, and unsupported classes, and allows
`BREAK_BLOCKS` only for a host-classified genuine brick or turn block. Question
blocks, pipes, scenery, and unknown Map16 are always rejected.

`smw_falcon_combat_apply(cpu, attack, facing, ledger, hit)` is the follow-up native
seam. The adapter owner calls it once inside `SmwFalconAfterPhysics`, after
`$00:E92B` collision and before `snes_foreign_resolve`, passing the live
`ForeignState.facing`. This tranche wires that one hook and its two sources;
it does not change plugin ownership.

The attack is source-center-relative (+Y-up): its SMW center is
`player + (8,32) + (facing*offset_x, -offset_y)*0.08`, with width/height as
full extents. Falcon Punch remains visually authored at frame 42, but its SMW
contact window is frame `42..<56`, through the visible fire effect. Its
arm-aligned host union reaches 112px ahead and covers the shell-height band;
Kick uses a low forward foot union reaching 72px. The per-move ledger lets one
move contact a group of distinct targets once each while preventing repeated
native consequences during linger. An aerial Punch landing retains this same
ledger and frame timeline (impact 42, end 56) as it changes to ground physics.
The policy tests both facings and vertical sign.

The live `$00:CD36` hook is restricted to `M1X1`, `DB=$00`, `D=$0000`.
Each native transaction saves/restores CPU registers and `$00-$0F` scratch;
the block route also restores `$98-$9C`. It makes only proven `M1X1`,
`DB=$02`, `D=$0000` generated calls:

- `$02:9404` `CheckPlayerAttackToNormalSpriteColl_029404`, the native
  spin-jump kill/status/score/contact-effect path. The bridge supplies only
  targets admitted by its reviewed host AABB policy; it does not treat entry
  to this routine as proof of a hit. SMWDisX/generated code reaches `$02:945B`
  after entry and writes literal status `$02` to `$14C8,X` before ID-specific
  follow-ups; the entry does not revalidate status `$08`. Thus admitted loose
  shell IDs `$04-$07` in native statuses `$09/$0A` use the real native destroy
  route rather than a host status write. A support allowlist accepts
  only Koopa/shell families, Goomba/Paragoomba, and Buzzy Beetle; status and
  tweaker guards reject dead, boss, hazard, and special entities. It invokes
  status-$08 enemies and loose shell IDs `$04-$07` at statuses `$09/$0A`;
  carried status `$0B` is excluded. Upright targets use a 16x24 union and
  loose shells a 16x16 union. The bridge marks a target only when the native
  transaction changes status or has another persistent guest-RAM consequence;
  this recognizes accepted multi-hit `$08` contacts without guessing a private
  enemy timer, and prevents repeat calls on later linger frames.
- `$02:8752` `SpawnBounceSprite`, only when the attack overlaps the current
  `$00:E92B` collision block and native action `$04=7` proves brick, or current
  Map16 byte `$1693=$1E` proves turn block. It owns native Map16 mutation,
  score, debris and sound; question blocks, pipes, scenery and unknown values
  are rejected. `$7C-$7D` player Y speed is restored so a Falcon block attack
  never inherits native head-bounce velocity.

Native stomps remain wholly SMW-owned. Normal-sprite processing happens after
the `$00:CD36` adapter seam. Once SMW's own collision has selected the stomp
branch, `$01:AA33` `BoostMarioSpeed` writes its exact `$D0` (or held-B `$A8`)
bounce before its `$01:AA41` return. A generated block callback only observes
that completed write and feeds the inverse-scaled impulse into Falcon's next
resolve; it does not select a sprite, change status, or call a native damage
routine. This retains SMW's enemy state, score, contact effect, and sound.

Some multi-hit/custom sprite routines can make a later side-damage decision in
the same normal-sprite pass. After a confirmed native bounce, the adapter sets
the native `$1497` IFrameTimer to `1` only if it was zero. That is the exact
native no-hurt guard tested by `$01:A8E6`; it blocks only the remaining
same-frame contact path. The next `$00:D5F2` seam clears only that unchanged
one-frame value, so it is neither a synthetic kill nor broad invulnerability.
The identical one-pass `$1497` guard is armed only after an active grounded or
aerial Falcon Kick receives a confirmed native target consequence; a miss or a
target behind Falcon leaves native contact damage untouched.

Run `test\falcon_combat\build.bat`, `test\falcon_combat_apply\build.bat`, and
`test\falcon_step_guard\build.bat` for policy, mocked-native, and exact
one-block-step regression validation.
