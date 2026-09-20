# Falcon audio cache seam

`smw_falcon_audio` is a deliberately unwired bridge between the verified
owner cache and the framework's statically linked PCM mixer. It contains no
owner bytes and retains neither a ROM path nor a cache path after activation.

## Cache contract

`smw_falcon_audio_activate(cache_dir)` requires all eleven files in one final
cache directory, in `FalconAudioCue` order:

1. `falcon_jump_effort.wav`
2. `falcon_punch_falcon.wav`
3. `falcon_punch_punch.wav`
4. `falcon_kick.wav`
5. `falcon_punch_impact_fgm.wav`
6. `falcon_kick_swing_fgm.wav`
7. `falcon_kick_start_fgm.wav`
8. `falcon_dive_launch_fgm.wav`
9. `falcon_dive_catch_fgm.wav`
10. `falcon_dive_explosion_fgm.wav`
11. `falcon_dive_voice.wav`

Each file must be an exactly sized RIFF/WAVE container with one `fmt ` and one
`data` chunk. Only canonical PCM (`format=1`, 16-bit signed, mono or stereo,
8–192 kHz) is accepted; chunk padding, declared sizes, byte rate, block align,
frame divisibility, duplicate chunks, truncation, and files above 16 MiB are
checked before mixer registration.

Activation is transactional: it first removes any old registry, then registers
each clip. A missing, malformed, corrupt, or mixer-rejected file unregisters
everything already registered and leaves the seam inactive. The framework
copies PCM at registration, so the loader frees the parsed bytes immediately.

## Integration seam (not wired here)

The owner-cache activation owner should call `smw_falcon_audio_activate` only
after its cache verification succeeds, and call `smw_falcon_audio_reset` on
cache loss, mod deactivation, emulator reset, and save-state load. The SMW
post-physics adapter owner should call
`smw_falcon_audio_play_events(&move_result.audio)` once per controller tick,
after it obtains the `ForeignMoveResult`. Cue values outside the Falcon enum
are ignored. This tranche deliberately changes neither the plugin, CMake, nor
the adapter hook.

Run the isolated mocked validation on Windows with:

```
test\falcon_audio\build.bat
```

It exercises valid registration, exact cue-to-clip event mapping, corrupt WAV
rejection, and mixer failure rollback.
