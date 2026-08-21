/****************************************************************************
 * contest2026_408_signbridge/app/signbridge/signbridge_audio_in.h
 *
 * SPDX-License-Identifier: Apache-2.0
 *
 * Voice input module for SignBridge.
 *
 * Captures audio from the on-board microphone (ES8311 ADC via I2S RX),
 * runs a lightweight energy-based VAD (voice activity detection) and
 * provides the wake-word interface.
 *
 * Wake word (contest requirement): "你好，openvela" / "Hello, openvela"
 *   - The wake-word recognizer itself is a small self-trained model
 *     (MFCC + tiny CNN/TCN, PC-trained and exported like the sign
 *     classifier).  The hooks are in place; until the model is loaded,
 *     a VAD-triggered fallback reports "wake word heard" on speech
 *     activity so the pipeline can be demoed end-to-end.
 *
 ****************************************************************************/

#ifndef __APP_SIGNBRIDGE_SIGNBRIDGE_AUDIO_IN_H
#define __APP_SIGNBRIDGE_SIGNBRIDGE_AUDIO_IN_H

/****************************************************************************
 * Included Files
 ****************************************************************************/

#include <nuttx/config.h>
#include <stdbool.h>

/****************************************************************************
 * Pre-processor Definitions
 ****************************************************************************/

/* Contest wake word */

#define SIGNBRIDGE_WAKE_WORD_ZH   "你好，openvela"
#define SIGNBRIDGE_WAKE_WORD_EN   "Hello, openvela"

/* Capture configuration (16 kHz / 16 bit / mono, standard for ASR).
 * The board glue (esp32p4_es8311.c) registers the ES8311 record path as
 * /dev/audio/pcm_in0; /dev/audio/pcm0 is the playback (pcm decode) device.
 */

#define SIGNBRIDGE_AUDIO_IN_DEV       "/dev/audio/pcm_in0"
#define SIGNBRIDGE_AUDIO_IN_RATE      16000
#define SIGNBRIDGE_AUDIO_IN_CHANNELS  1
#define SIGNBRIDGE_AUDIO_IN_BPS       16
#define SIGNBRIDGE_AUDIO_IN_FRAME_MS  100   /* 100 ms per buffer */
#define SIGNBRIDGE_AUDIO_IN_FRAME_BYTES \
  (SIGNBRIDGE_AUDIO_IN_RATE * SIGNBRIDGE_AUDIO_IN_BPS / 8 * \
   SIGNBRIDGE_AUDIO_IN_CHANNELS / (1000 / SIGNBRIDGE_AUDIO_IN_FRAME_MS))
#define SIGNBRIDGE_AUDIO_IN_NBUFS     4

/****************************************************************************
 * Public Types
 ****************************************************************************/

enum signbridge_vad_state_e
{
  SIGNBRIDGE_VAD_SILENCE = 0,  /* No voice activity                */
  SIGNBRIDGE_VAD_SPEECH        /* Voice activity detected          */
};

#ifdef __cplusplus
extern "C"
{
#endif

/****************************************************************************
 * Public Function Prototypes
 ****************************************************************************/

/****************************************************************************
 * Name: signbridge_audio_in_init
 *
 * Description:
 *   Initialize the voice input module (state only; the capture thread is
 *   started by signbridge_audio_in_start()).
 *
 ****************************************************************************/

int signbridge_audio_in_init(void);

/****************************************************************************
 * Name: signbridge_audio_in_start / signbridge_audio_in_stop
 *
 * Description:
 *   Start / stop the capture thread and the ES8311 ADC streaming.
 *
 ****************************************************************************/

int signbridge_audio_in_start(void);
int signbridge_audio_in_stop(void);

/****************************************************************************
 * Name: signbridge_audio_in_vad_state
 *
 * Description:
 *   Current voice activity state.
 *
 ****************************************************************************/

enum signbridge_vad_state_e signbridge_audio_in_vad_state(void);

/****************************************************************************
 * Name: signbridge_audio_in_level
 *
 * Description:
 *   Smoothed input level in percent (0..100), for UI metering.
 *
 ****************************************************************************/

int signbridge_audio_in_level(void);

/****************************************************************************
 * Name: signbridge_audio_in_wakeword_heard
 *
 * Description:
 *   Non-zero if the wake word ("你好，openvela" / "Hello, openvela")
 *   was detected since the last clear.
 *
 * Notes:
 *   Real detection is a self-trained MFCC+CNN model (PC-trained, weights
 *   exported like the sign classifier).  Until the model is integrated,
 *   the first voice activity after idle is reported as a wake-up so the
 *   full loop can be demonstrated.
 *
 ****************************************************************************/

bool signbridge_audio_in_wakeword_heard(void);

/****************************************************************************
 * Name: signbridge_audio_in_clear_wakeword
 *
 * Description:
 *   Clear the wake-word latch (called after the state machine consumes it).
 *
 ****************************************************************************/

void signbridge_audio_in_clear_wakeword(void);

#ifdef __cplusplus
}
#endif

#endif /* __APP_SIGNBRIDGE_SIGNBRIDGE_AUDIO_IN_H */
