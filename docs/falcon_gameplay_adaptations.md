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
  collision result, but does not claim to suppress native action branches.

Underwater levels remain Falcon-controlled: no native swim input is used, and
attacks retain the saved raw controller input. At the `$00:DC2D` boundary every
vertical Falcon movement request is scaled by `0.45`; falling is capped to 42
source units before that scale. This creates intentional floaty gravity and a
terminal fall speed while retaining the same locomotion and attack controller.

Power-up state (`$19`) and the reserve item box remain entirely SMW-owned, so
they continue to supply health, reserve, and progression. Yoshi mounting is
hard-disabled for an active Falcon level. The early input seam releases an
already-mounted state (`$187A` and Yoshi-only `$00C2=1`) and tongue timers.
More importantly, the `Spr035_Yoshi` entry (`$01:EBCA`) precedes the native
`$01:ECE1` mount-contact branch. For a falling Falcon it temporarily makes
that branch's Y-speed predicate upward, then restores the exact velocity at
`PlayerDraw`, immediately after normal-sprite processing. The native contact
therefore stays on ordinary off-Yoshi behavior before it can write the rider,
mounted C2 state, carry-over, colour/facing, sound, smoke, bounce, or player
position. State restoration also releases the transient mounted state whenever
the Falcon controller is enabled, including a saved transition frame. It never
changes Yoshi sprite status/position, the selected slot/entity, owned-Yoshi
flags, colour, wings, or transition metadata: those remain native SMW
progression. The title/attract demo and mod-off path are unmodified.

The focused seam test models the confirmed `$01:ECE1` eligibility inputs and
asserts that every observed pre-`$01:EB82` side-effect field remains unchanged.
TCP validation must use a real stock Yoshi encounter; a WRAM-only mounted-bit
fixture cannot establish the contact ordering and is intentionally not claimed
as visual proof.

## Native shell carry

Falcon uses physical **A** for the one-handed native carry lifecycle. Physical
**Y** (position-true PlayStation Square) remains exclusively a Falcon special,
while physical **X** is the Falcon normal-attack edge; neither is ever passed
through as native carry input. The early `$00:D5F2` hook captures and masks
all three inputs.
After all player input, physics, climb, pipe, and door decisions, the adapter
uses the first generated `ProcessNormalSprites` entry at `$01:80D2` (the parent
routine starts at `$01:808C`) to translate captured A into native Y:

- hold A: native Y held, allowing SMW to pick up or retain an item;
- release A: no native Y, so the native status-$0B handler throws it;
- release A while holding Down: native Down only, selecting the handler's
  native set-down path.

The adapter does not search, move, or change sprites. SMWDisX
`CheckPlayerToNormalSpriteColl` at `$01:AA42` performs eligibility and only
promotes supported collisions to sprite status `$0B`; its native `$01:9F9B`
carried lifecycle owns positioning, throw, and set-down. Unsupported objects
therefore remain rejected by stock SMW logic. The A/Down translation is
one-frame host state, cleared on pipe/goal/death handoff and after a savestate
load; the status-$0B sprite and carry flags live in the normal SMW RAM
savestate.
