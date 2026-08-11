# Falcon owner-cache presentation module

`src/mods/falcon/falcon_presentation.{c,h}` is a presentation-only loader and
software mesh compositor for the owner-generated `falcon_runtime.bin` v4
format. It has no NES runtime, SDL, SMW WRAM, controller, or CMake dependency.
The host supplies a `FalconPresentationPose` plus an explicit ARGB8888
framebuffer, width, height, row pitch, and foot anchor.

## Provenance and scope

The binary layout, animation semantics, 1.05 TopN root-motion scale, and
Punch/Kick effect inventory and source-LR orientation rules were ported from the proven NES Falcon
`game_smash64_assets.{c,h}` implementation. The small triangle compositor is
adapted from the immediate mesh path in
`runner/{include,src}/voxel_renderer.{h,c}`. Neither dependency is included
or linked at runtime.

The recipe continues to target the public US assets cross-checked against
SmashBrosDecomp `054ffc23f396868cd1db2b87ee3a2c1d3bebb75a` and BattleShip
`4fc112883dfec7e2471280c954a172a2eec44829`. BattleShip's Captain status
tables establish the named Punch, Kick, and Dive animation/effect families;
its US relocation table remains external provenance, not a runtime dependency.

Only the external cache may supply proprietary model, texture, animation, and
effect bytes. The parser accepts exactly `FLCN64B\0` version 4 and rejects
truncation, unknown versions, invalid hierarchy/ranges/counts, non-finite
values, malformed texture sizes, duplicate/bad animation names, invalid track
references, descending track segments, trailing data, and animation names that
fill all 32 bytes without a NUL terminator.

## Host API

```c
FalconPresentation *p = falcon_presentation_load_file(external_blob_path);
FalconPresentationTarget target = {
    .framebuffer = pixels, .width = width, .height = height,
    .pitch_pixels = pitch, .anchor_x = player_x, .anchor_y = player_foot_y,
    .scale = 1.0f,
};
FalconPresentationPose pose = {
    .state = FALCON_PRESENT_DIVE, .frame = 13.0f, .facing_right = 1,
};
falcon_presentation_draw(p, &pose, &target);
```

`pitch_pixels` is authoritative: every pixel write uses it rather than assuming
tightly packed rows. `falcon_presentation_root_delta()` samples named animation
root tracks without modifying the pose or game state. Punch and Kick use the
owner-generated effect textures only inside their source windows; Dive uses
compact generated dust, spark, and white impact cards timed after the proven
NES `draw_falcon_dive_effect` windows, so no unrelated owner effect assets are
required. Model faces are emitted in a stable far-to-near average camera-z
order before alpha blending; equal-depth faces retain their blob order.

## Evidence

The isolated C harness validates malformed rejection (including unterminated
names), depth ordering, root sampling, and fixed synthetic framebuffer hashes
for right and left Punch/Kick cards. The synthetic card textures are asymmetric,
so the left cases also pin U-axis reversal and aerial-Kick LR roll direction.
With the verified external cache supplied by the user,
it also produced these ignored evidence files:

`_triage/falcon_owner_dive_particles.bmp` - Dive frame 13 with compact
particles; FNV-1a-64 `17c981d1bcc6af93`.

`_triage/falcon_owner_pose_sheet.bmp` - Idle, Run, Punch active, Kick active,
Dive frame 13, and left-facing Idle at readable scale; FNV-1a-64
`9d52c668584c47ba`.

The evidence images are deliberately untracked. They are not asset sources or
goldens committed to Git.
