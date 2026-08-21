/****************************************************************************
 * contest2026_408_signbridge/app/signbridge/signbridge_voice.c
 *
 * SPDX-License-Identifier: Apache-2.0
 *
 * Voice announcement module for SignBridge.
 * Uses nxplayer to play pre-recorded WAV files for recognized signs.
 *
 ****************************************************************************/

/****************************************************************************
 * Included Files
 ****************************************************************************/

#include <nuttx/config.h>

#include <stdio.h>
#include <string.h>
#include <errno.h>
#include <syslog.h>
#include <stdbool.h>

#include <system/nxplayer.h>
#include <nuttx/audio/audio.h>

#include "signbridge_voice.h"

#ifdef CONFIG_SYSTEM_NXPLAYER

/****************************************************************************
 * Private Data
 ****************************************************************************/

static FAR struct nxplayer_s *g_player;
static bool g_voice_playing;

/****************************************************************************
 * Public Functions
 ****************************************************************************/

int signbridge_voice_init(void)
{
  int ret;

  g_voice_playing = false;

  g_player = nxplayer_create();
  if (g_player == NULL)
    {
      syslog(LOG_ERR, "voice: nxplayer_create failed\n");
      return -ENOMEM;
    }

  /* Set the preferred audio device (ES8311 codec playback path) */

  ret = nxplayer_setdevice(g_player, SIGNBRIDGE_AUDIO_DEV);
  if (ret != OK)
    {
      syslog(LOG_WARNING,
             "voice: setdevice %s failed: %d (will auto-search)\n",
             SIGNBRIDGE_AUDIO_DEV, ret);
    }

  syslog(LOG_INFO, "voice: announcement module ready (dev=%s)\n",
         SIGNBRIDGE_AUDIO_DEV);
  return OK;
}

int signbridge_voice_play(int class_id)
{
  char path[SIGNBRIDGE_VOICE_PATH_MAX];
  int ret;

  if (g_player == NULL)
    {
      return -ENODEV;
    }

  if (class_id < 0)
    {
      return -EINVAL;
    }

  /* Build the WAV path: /media/signs/<class_id>.wav */

  snprintf(path, sizeof(path), "%s/%d.wav",
           SIGNBRIDGE_MEDIA_DIR, class_id);

  /* Stop any current playback before starting a new one */

  if (g_voice_playing)
    {
      nxplayer_stop(g_player);
      g_voice_playing = false;
    }

  ret = nxplayer_playfile(g_player, path, AUDIO_FMT_UNDEF, AUDIO_FMT_UNDEF);
  if (ret != OK)
    {
      syslog(LOG_WARNING, "voice: play %s failed: %d\n", path, ret);
      return ret;
    }

  g_voice_playing = true;
  syslog(LOG_INFO, "voice: playing %s\n", path);
  return OK;
}

bool signbridge_voice_is_playing(void)
{
  return g_voice_playing;
}

void signbridge_voice_stop(void)
{
  if (g_player != NULL && g_voice_playing)
    {
      nxplayer_stop(g_player);
      g_voice_playing = false;
    }
}

void signbridge_voice_deinit(void)
{
  if (g_player != NULL)
    {
      nxplayer_stop(g_player);
      nxplayer_release(g_player);
      g_player = NULL;
    }

  g_voice_playing = false;
}

#else /* !CONFIG_SYSTEM_NXPLAYER */

/* Stub implementations when nxplayer is not available */

int signbridge_voice_init(void)
{
  syslog(LOG_WARNING, "voice: nxplayer disabled, voice output stubbed\n");
  return OK;
}

int signbridge_voice_play(int class_id)
{
  syslog(LOG_INFO, "voice: (stub) would announce class %d\n", class_id);
  return OK;
}

bool signbridge_voice_is_playing(void)
{
  return false;
}

void signbridge_voice_stop(void)
{
}

void signbridge_voice_deinit(void)
{
}

#endif /* CONFIG_SYSTEM_NXPLAYER */
