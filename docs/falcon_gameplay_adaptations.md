# Falcon gameplay adaptations

## Portable aerial jump

Captain Falcon uses the verified common `jumps_max = 2` rule. A grounded jump
sets `jumps_used` to one, and one later B press or buffered up-stick input in
an air state selects `JUMPAERIAL_F` or `JUMPAERIAL_B` from facing-relative
stick direction. Its launch velocity follows the source common aerial-jump
formula. `jumps_used`, the directional state, velocity, and input buffer are
already part of `FalconFighter`, so the existing controller-owned save payload
round-trips an in-progress aerial jump without a separate compatibility field.

## SMW input and water boundary

The adapter has two deliberately separate host seams:

- `HandlePlayerPhysics` at `$00:D5F2` runs before SMW's native gameplay input
  reads. For every playable Falcon frame, including the initial scripted-to-
  foreign handoff, it snapshots the physical pad for Falcon, then masks native
  movement/action buttons and clears competing spin-jump, fireball, cape,
  swim, and active Yoshi rider/tongue state. This is the seam that suppresses
  native abilities; `$00:DC2D` is too late for that purpose.
- `UpdatePlayerSpritePosition` at `$00:DC2D` remains the later
  velocity/collision seam. It applies Falcon motion and resolves the native
  collision result, but does not claim to suppress native action branches. A
  future shell-carry bridge may explicitly expose a translated A input only at
  an appropriate downstream seam.

Underwater levels remain Falcon-controlled: no native swim input is used, and
attacks retain the saved raw controller input. At the `$00:DC2D` boundary every
vertical Falcon movement request is scaled by `0.45`; falling is capped to 42
source units before that scale. This creates intentional floaty gravity and a
terminal fall speed while retaining the same locomotion and attack controller.

Power-up state (`$19`) and the reserve item box remain entirely SMW-owned, so
they continue to supply health, reserve, and progression. Yoshi mounting is
not supported by this boundary: Falcon is cleanly dismounted and active tongue
input is disabled, while owned-Yoshi persistence, wings, and the level entity
remain under native SMW rather than sharing Falcon movement ownership.
