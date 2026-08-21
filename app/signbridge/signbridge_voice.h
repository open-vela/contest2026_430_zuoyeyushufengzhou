/****************************************************************************
 * contest2026_408_signbridge/app/signbridge/signbridge_voice.h
 *
 * SPDX-License-Identifier: Apache-2.0
 *
 * Voice announcement module for SignBridge.
 * Plays pre-recorded WAV files for recognized sign language words.
 *
 ****************************************************************************/

#ifndef __APP_SIGNBRIDGE_SIGNBRIDGE_VOICE_H
#define __APP_SIGNBRIDGE_SIGNBRIDGE_VOICE_H

#include <nuttx/config.h>
#include <stdbool.h>

/* Audio device and media paths */

#define SIGNBRIDGE_AUDIO_DEV   "/dev/audio/pcm0"
#define SIGNBRIDGE_MEDIA_DIR   "/media/signs"

/* Maximum path length for a voice file */

#define SIGNBRIDGE_VOICE_PATH_MAX  64

#ifdef __cplusplus
extern "C"
{
#endif

/****************************************************************************
 * Name: signbridge_voice_init
 *
 * Description:
 *   Initialize the voice announcement module (creates nxplayer context).
 *
 * Returned Value:
 *   0 on success, negative errno on failure.
 *
 ****************************************************************************/

int signbridge_voice_init(void);

/****************************************************************************
 * Name: signbridge_voice_play
 *
 * Description:
 *   Play the voice announcement for a recognized sign class.
 *   Looks up /media/signs/<class_id>.wav and plays it.
 *
 * Input Parameters:
 *   class_id - The recognized sign language class index.
 *
 * Returned Value:
 *   0 on success, negative errno on failure.
 *
 ****************************************************************************/

int signbridge_voice_play(int class_id);

/****************************************************************************
 * Name: signbridge_voice_is_playing
 *
 * Description:
 *   Check if a voice announcement is currently playing.
 *
 ****************************************************************************/

bool signbridge_voice_is_playing(void);

/****************************************************************************
 * Name: signbridge_voice_stop
 *
 * Description:
 *   Stop any current voice playback.
 *
 ****************************************************************************/

void signbridge_voice_stop(void);

/****************************************************************************
 * Name: signbridge_voice_deinit
 *
 * Description:
 *   Release the voice module resources.
 *
 ****************************************************************************/

void signbridge_voice_deinit(void);

#ifdef __cplusplus
}
#endif

#endif /* __APP_SIGNBRIDGE_SIGNBRIDGE_VOICE_H */
