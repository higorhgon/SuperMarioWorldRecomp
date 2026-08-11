# Falcon combat bridge

`smw_falcon_combat_policy.{c,h}` converts a `ForeignAttackHitbox` from source
units into SMW world pixels using the existing `0.08` scale. It is deliberately
pure: it projects supported ordinary targets, rejects
bosses, hazards, dead sprites, and unsupported classes, and allows
`BREAK_BLOCKS` only for a host-classified genuine brick or turn block. Question
blocks, pipes, scenery, and unknown Map16 are always rejected.

`smw_falcon_combat_apply(cpu, attack, facing, ledger, hit)` is the follow-up
native seam. The adapter can call it after `$00:E92B` collision, then the
guaranteed per-sprite `$01:80D2` seam applies any still-pending consequence
before that slot's native side-contact path. Both pass the live
`ForeignState.facing`; the per-frame ledger prevents a consequence from being
replayed when both seams run.

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

The live `$00:CD36` hook is restricted to `M1X1`, `DB=$00`, `D=$0000`; the
durable `$01:80D2` seam accepts the proven bank-$01 WRAM mirror as well.
Each native transaction saves/restores CPU registers and `$00-$0F` scratch;
the block route also restores `$98-$9C`. It makes only proven generated calls:

- `$01:9ACB` `SprStatus02_Dead_SetNorSprStatus04` changes an admitted target to
  SMW's native spin-kill state and starts its timer; `$07:FC3B`
  `SpawnSpinJumpStars` emits the four native extended stars; `$01:AB46`
  `CheckPlayerToNormalSpriteColl_01AB46` performs the native stomp score/SFX
  transaction. The bridge deliberately enters after Mario-body contact and
  bounce logic because Falcon's authored Punch/Kick AABB is the admission
  decision. A support allowlist accepts only Koopa/shell families,
  Goomba/Paragoomba, and Buzzy Beetle; status and tweaker guards reject dead,
  boss, hazard, and special entities. Status-$08 enemies and loose shell IDs
  `$04-$07` in statuses `$09/$0A` receive the same visible spin-vanish; carried
  status `$0B` is excluded. Upright targets use a 16x24 union and loose shells
  a 16x16 union. The bridge records each accepted slot so lingering attack
  frames cannot replay native stars, score, SFX, or status changes.
- Falcon Dive/Up-B is a capture, not an immediate Punch/Kick consequence. Its
  contact-only hitbox latches exactly one eligible status-$08 target identity,
  retains it through Catch and Throw, and invokes `$02:C7B1` only on Throw
  frame zero. The release revalidates both slot ID and status, so a dead or
  reused slot cannot be struck. Loose shells are not capture targets, and all
  latch state is cleared on handoff, load, or move termination.
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
Kick is stricter: `AfterPhysics` records only freshly accepted Kick sprite
slots. `$01:80D2` executes per ordinary slot with X preserved and before its
later `$01:A7E4` collision call; it clears the prior slot's exact `$1497=1`
then sets it only for a recorded slot. Consequently a connected retained
status-$08 target is protected, while an unhit/behind slot later in the same
pass sees zero and remains native-dangerous.

Run `test\falcon_combat\build.bat`, `test\falcon_combat_apply\build.bat`,
`test\falcon_kick_guard\build.bat`, and `test\falcon_step_guard\build.bat`
for policy, mocked-native, direct one-pass Kick, and exact one-block-step
regression validation.
