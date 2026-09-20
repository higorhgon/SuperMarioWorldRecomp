# Captain Falcon controller

`falcon_locomotion.c` is a portable, source-unit state machine. It contains
the full locomotion, aerial, Falcon Punch, Falcon Kick, and Falcon Dive timing,
along with attack descriptors and one-frame audio-cue outputs. It takes no
game pointers, assets, renderer objects, or framework headers.

`captain_falcon_controller.c` is the game-owned SMW boundary. Its
`SMWForeign*` structures deliberately mirror the pending SNES
foreign-controller contract while allowing this worktree to build and test
before that framework change lands. The eventual bridge should translate only
between these structures and its generic ABI; it must not duplicate Falcon's
state machine or source-unit constants.

No ROM data, animation data, sound data, textures, or other proprietary assets
are included here. Audio output is symbolic `FalconAudioCue` bits for the host
to map to lawful runtime audio later.
