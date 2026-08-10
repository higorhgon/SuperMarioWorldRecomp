# Captain Falcon movement dependency and SMW boundary

The portable controller was derived from `VetriTheRetri/ssb-decomp-re` at
`054ffc23f396868cd1db2b87ee3a2c1d3bebb75a`. It retains source locomotion,
aerials, selected specials, source-unit timing, and +Y-up physics; it excludes
the source renderer, stage collision, assets, and combat consequences.

## SMW host seam

The adapter in `overrides/falcon/falcon_smw_adapter.c` is the only scale/sign
boundary. Falcon `requested_dx` becomes native `$7B` horizontal speed;
`requested_dy` is negated before native `$7D` vertical speed because SMW uses
+Y down. Native `$94/$96` position deltas are converted back after physics,
with Y negated again before `snes_foreign_resolve`.

SMWDisX `3390ee1` bank_00 establishes the seam order: `CODE_00CCE0`, `CD24`,
`CD36`, and `CD82` place `UpdatePlayerSpritePosition` (`$00:DC2D`) immediately
before native level collision and `PlayerState00_00CD36` (`$00:CD36`) after its
ordinary-flow join. The injected pre-hook supplies intent at `$DC2D`; the
post-hook returns actual native displacement/collision at `$CD36`. SMW remains
the authority for tiles, slopes, water, blocks, sprites, death, pipes, goals,
and position. Falcon ownership is limited to GameMode `$14`, ordinary player
state `$00`, and no pipe/goal/keyhole timer; other states switch to scripted
ownership and later reseed from native WRAM.

Grounded Falcon Dive consumes its controller `force_airborne` edge by setting
native airborne state and at least one whole upward pixel of speed before
position integration. Its post-hook reports that same frame as airborne even
if SMW briefly rediscovers the floor; normal native collision feedback resumes
on the following frame.

## Controls and scope

- **B:** jump and short-hop hold logic
- **Y:** normal-attack intent
- **X:** special-attack intent
- **A:** reserved and consumed until the shell/carry tranche supplies explicit
  native carry semantics (it must not enter SMW's spin-jump path)
- D-pad: source stick; Down fast-falls while airborne

Attack hitbox and symbolic audio intent are exposed for a later combat layer;
this milestone intentionally adds no sprites, audio, assets, shell behavior,
or attack consequences. The source supports `jumps_max = 2`, but double jump
is deliberately disabled here. It is the next fidelity item and requires
explicit controller and host-seam tests before enabling.
