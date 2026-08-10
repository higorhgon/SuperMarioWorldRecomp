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

- **B / PlayStation Cross:** jump, short-hop hold logic, and aerial second jump
- **Y / PlayStation Square:** special intent (Punch, Down+Y Kick, Up+Y Dive)
- **X:** normal-attack intent
- **A / PlayStation Circle:** native shell carry; release throws and
  Down+release sets the item down
- D-pad: first press walks; a same-direction second tap within 15 frames enters
  the sourced Dash-to-Run path; Down fast-falls while airborne

Attack hitboxes now feed the conservative native SMW combat bridge, and the
verified owner cache supplies presentation, audio, and special-move root
motion. Falcon retains the verified source `jumps_max = 2` aerial jump.
