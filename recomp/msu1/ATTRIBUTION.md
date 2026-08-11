# MSU-1 behavior - attribution and thanks

The MSU-1 audio behavior in this build does not originate with us. The classic
game-side driver, which detects the MSU-1 chip and streams music in place of the
SPC soundtrack, is homebrew work by others, and we credit them plainly.

## Which patch behavior we implement

The host-side Mods plugin implements Conn's "Super Mario World MSU-1" track map
and playback behavior: the audio-only/native music-replacement patch. The ROM is
no longer patched during regeneration. Stock SMW remains the analysis input and
runtime ROM. The trusted static plugin observes SMW music commands and drives
the runner's MSU-1 device directly.

- `smw_msu.ips` is retained as legacy reference material. It injects a roughly
  520-byte driver into bank `$04` freespace at `$04:EF46` and five JSL hooks in
  banks `$00`/`$04`.
- `smw_msu1.asm` is the annotated driver source (Conn, 2015) used as the
  behavioral reference for the host-side plugin. Its header credits Conn, with
  thanks to Ikari_01, EmuandCo, Kiddo, and others.
- `manifest.bml` is the bsnes/higan board manifest that ships with the patch.

## Authors

- Conn wrote the Super Mario World MSU-1 driver (`smw_msu1.asm`) that this
  behavior is based on.
- Ikari_01, EmuandCo, Kiddo, and the wider SMW Central / Zeldix MSU-1 community
  are thanked in the driver source for the groundwork.

Patch home: <https://www.zeldix.net/t1436-super-mario-world-native>

## PCM packs are not interchangeable

Super Mario World has three distinct MSU-1 patches, each with its own PCM track
set. A pack built for one patch will play the wrong tracks, or silence, under
another because the track-number-to-file mapping differs. This build implements
only the first one:

| Patch | Scope | We use it? | Pack source |
|---|---|---|---|
| SMW MSU-1 (Conn, audio-only) | music replacement, no gameplay changes | yes, as host-side Mods behavior | <https://www.zeldix.net/t1436-super-mario-world-native> |
| SMW MSU+ | enhancement hack with gameplay/feature changes | no | <https://www.zeldix.net/t1437-super-mario-world-msu> |
| SMW MSU-1 Plus Ultra (130 tracks) | large enhancement hack | no | <https://www.zeldix.net/t2535-super-mario-world-msu-1-plus-ultra-130-tracks-total> |

Use a PCM pack built for "SMW MSU-1" (the t1436 audio-only patch). Packs for
SMW MSU+ or Plus Ultra will not line up with this build.

## License / what we redistribute

The IPS patch is retained only as legacy reference material for Conn's homebrew
behavior. The active regeneration pipeline no longer applies it:
`tools/regen.sh` recompiles from your legally-obtained stock SMW (USA) ROM. The
recompiled build then runs on that stock ROM directly. No pack means authentic
SPC audio; a matching pack plus the MSU-1 Audio mod streams music.

Thank you, Conn and the MSU-1 community. If you enjoy the streamed music here,
the credit is theirs.
