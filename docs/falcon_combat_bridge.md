# Falcon combat bridge policy milestone

`smw_falcon_combat_policy.{c,h}` converts a `ForeignAttackHitbox` from source
units into SMW world pixels using the existing `0.08` scale. It is deliberately
pure: it chooses the first overlapping supported ordinary target, rejects
bosses, hazards, dead sprites, and unsupported classes, and allows
`BREAK_BLOCKS` only for a host-classified genuine brick or turn block. Question
blocks, pipes, scenery, and unknown Map16 are always rejected.

This is not yet the native-consequence bridge. SMWDisX `BreakThrowBlock` and
`GenerateTile` are CPU-register/scratch-state routines, not safe C APIs; no
call is made without a proven contract. Likewise status/score/sound/debris and
the one-target Up-B `attack_connected` result await the single post-physics
adapter seam after its water-owner change lands. The policy test covers source
scale, overlap order (one target maximum), exclusions, and conservative Map16
classes. No generated CPU or Map16 contract is guessed.
