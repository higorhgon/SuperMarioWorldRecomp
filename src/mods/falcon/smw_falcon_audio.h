/* Owner-cache Falcon PCM audio seam.  This module owns no game state. */
#ifndef SMW_FALCON_AUDIO_H
#define SMW_FALCON_AUDIO_H

#include "foreign_controller.h"

/*
 * Load all Falcon one-shots from a verified owner cache.  A nonzero result
 * means every cue was registered; any failure leaves no registered clips.
 * cache_dir is a directory, never retained after this call returns.
 */
int smw_falcon_audio_activate(const char *cache_dir);

/* Stop voices, unregister every loaded clip, and clear the registry. */
void smw_falcon_audio_reset(void);

/* Play the bounded controller event list. Unknown cue values are ignored. */
void smw_falcon_audio_play_events(const ForeignAudioEvents *events);

int smw_falcon_audio_is_active(void);

#endif
