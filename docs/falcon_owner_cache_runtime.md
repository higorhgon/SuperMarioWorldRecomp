# Falcon owner-cache runtime contract

The Captain Falcon package requires a committed external ROM resource for the canonical Super Smash Bros. US v1.0 image (normalized SHA-1 `e2929e10fccc0aa84e5776227e798abc07cedabf`, 16 MiB). The mod runtime exposes the path only after final Play-time verification; pending launcher choices are never visible to the game plugin.

The game does not generate proprietary cache data in-process. It accepts an immutable `falcon-final-r1-<sha1>-<manifest-prefix>` directory outside the installation/source tree and pins `falcon_runtime.bin` to its approved SHA-256 before parsing it. Missing, malformed, or unapproved caches fail closed: stock Mario OBJ rendering remains present and no owner bytes are drawn.

For release or development first-run generation, set `SNESRECOMP_FALCON_CACHE_HELPER=<absolute helper executable>` and `SNESRECOMP_FALCON_CACHE_ROOT=<absolute external cache root>`. The helper is invoked exactly as:

```text
<helper> --rom <committed-owner-rom> --cache-root <root> --result-file <root>/.smw-falcon-cache-result-<pid>.txt
```

It must be an executable wrapper around the strict `tools/owner_ssb64/build_final_cache.py` recipe, atomically write only the final cache basename to the result file, and exit zero only after complete cache verification. The game launches it with an argv API (`_spawnv` or `fork`/`execv`), never a shell: paths containing `&|<>^%!$\`` are literal path arguments. The game rejects relative paths, separators in the returned basename, non-content-addressed names, and a runtime-blob hash mismatch. A verified cache may instead be supplied directly through `SNESRECOMP_FALCON_CACHE=<absolute final-cache directory>`.

During ordinary foreign-owned level control, PPU OBJ slots 64–75 (`$0300` to `$032f`, written by SMW `PlayerGFXRt`) are captured and removed before PPU composition; no other OBJ slots are affected. Falcon presentation is the current sole owner of the PPU overlay-capture policy, so it clears and re-publishes that policy once per frame; a future overlay user must compose this policy rather than independently calling `PpuClearOverlayCaptures`. After `RtlWidescreenPresent`, the approved mesh is composited at Mario's screen-foot anchor. Pipes, death, goals, and inactive/failed presentation keep native Mario visible.

For a coordinated TCP run, set `SNESRECOMP_FALCON_PRESENTATION_TRACE` to an
external writable text file. It records only gate transitions (`approved runtime
cache loaded`, `active`, handoff reasons, OBJ suppression, and mesh compositor
state), never an owner-ROM or cache path.
