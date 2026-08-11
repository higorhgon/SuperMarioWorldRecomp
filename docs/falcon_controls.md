# Falcon controls and movement parity

The SMW host reads the physical SNES pad before native player logic at
`HandlePlayerPhysics` (`$00:D5F2`).  Captain Falcon consumes the following
edges; all of these button meanings remain masked from native movement.

| Physical face button | SNES bit | Falcon/SMW meaning |
| --- | --- | --- |
| PlayStation Cross | B (`$15.7`) | Jump; press and hold/release choose the source KneeBend full/short-hop path. |
| PlayStation Circle | A (`$17.7`) | Native SMW shell carry: hold to carry, release to throw, Down + release to set down. It is not a Falcon attack. |
| PlayStation Square | Y (`$15.6`) | Smash special: neutral is Falcon Punch, Down is Falcon Kick, Up is Falcon Dive. |
| SNES X | X (`$17.6`) | Smash normal attack (Jab/tilt/aerial subset). |

The normal/special split follows the mature NES Falcon adapter: one logical
normal edge and one logical special edge are always distinct.  The SNES layout
puts special on position-true Square/Y as requested and keeps normal attack on
the remaining dedicated face button, X; Circle cannot be reused because SMW's
native status-$0B carry lifecycle owns it.

## D-pad locomotion

The source controller recognizes a fresh full analogue stick tap (`|x| >= 56`,
tap age below three frames) as Dash and converts Dash to Run at source frame
16.  A digital D-pad cannot express a walk magnitude, so the SMW adapter gives
the first Left/Right press a half-stick walk and records its direction.  A
second press of that same direction after release, within 15 host frames,
emits the source full-stick edge and enters the unchanged sourced Dash -> Run
path.  Holding that second press remains full-stick through Dash and Run.
Opposite directions replace the pending first tap; expiry and state load clear
it.  This is a host input adaptation only: the source `80` Dash speed and US
`75` Run speed/transition remain in `falcon_locomotion.c`.

## Falcon Kick root motion

Grounded and aerial Falcon Kick use the verified owner-cache animation TransN
tracks rather than a locally guessed velocity.  `DownSpecial` supplies the
ground horizontal travel; `DownSpecialAir` supplies the aerial down-forward
trajectory (mirrored by facing).  The controller samples the same named tracks
and previous-to-current-frame delta as the mature NES bridge, while the
existing source landing and bound state logic remains authoritative.  Missing
or invalid external cache yields no sampled root delta and never substitutes
proprietary bytes or invented motion.

A native side-wall collision during grounded SpecialLw's authored `flag1`
contact window enters the existing source `FL_FALCON_KICK_BOUND` state
(`ftcaptainspeciallw.c` Ground ProcMap/BoundCheck). The source mirror does not
integrate the rejected horizontal delta before the Bound transition. The
approved `FalconDiveEnd1` track is the fifth positional Captain SpecialL
motion—the slot `ftcaptain.h` identifies as SpecialLwBound—so it supplies the
authored rebound TransN. Ground-origin continuation and direct SpecialAirLw
use their own map paths: a side wall retains that Kick state with native
horizontal clamp and is never promoted to Ground Bound. This is limited to
Kick wall contact—ordinary jump/run wall handling is unchanged.

Source references: Smash decomp `054ffc23`,
`src/ft/ftchar/ftcaptain/ftcaptainspeciallw.c`; mature NES bridge
`mods/smash64/characters/captain_falcon.c`; and BattleShip `4fc11288`
relocation references for `DownSpecial`, `DownSpecialAir`, and
`VelocityXDownSpecialAir`.
