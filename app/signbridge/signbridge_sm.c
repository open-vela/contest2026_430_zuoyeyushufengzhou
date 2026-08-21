/****************************************************************************
 * app/signbridge/signbridge_sm.c
 *
 * Licensed to the Apache Software Foundation (ASF) under one or more
 * contributor license agreements.  See the NOTICE file distributed with
 * this work for additional information regarding copyright ownership.  The
 * ASF licenses this file to you under the Apache License, Version 2.0 (the
 * "License"); you may not use this file except in compliance with the
 * License.  You may obtain a copy of the License at
 *
 *   http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS, WITHOUT
 * WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.  See the
 * License for the specific language governing permissions and limitations
 * under the License.
 *
 ****************************************************************************/

/* SignBridge application state machine skeleton.
 *
 * Phase 4 fills in the real camera/inference/UI hooks; this skeleton keeps
 * the state transition table and public interfaces stable so that the
 * display (phase 1), camera (phase 2) and inference (phase 3) work can be
 * integrated independently.
 */

/****************************************************************************
 * Included Files
 ****************************************************************************/

#include <nuttx/config.h>

#include <debug.h>
#include <errno.h>
#include <syslog.h>

#include "signbridge.h"
#include "signbridge_infer.h"
#include "signbridge_voice.h"
#include "signbridge_camera.h"
#include "signbridge_audio_in.h"
#include "signbridge_correct.h"
#include "signbridge_pm.h"

/****************************************************************************
 * Private Data
 ****************************************************************************/

static enum signbridge_state_e g_state = SIGNBRIDGE_STATE_IDLE;
static struct signbridge_result_s g_last_result;
static bool g_result_pending;
static char g_utterance_text[SIGNBRIDGE_CORRECT_MAX_WORDS * 8 + 1];

/* Frame counter for stub-mode state transitions */

static int g_detect_frames;
static int g_classify_frames;
static int g_result_hold_frames;

/* Stub: auto-transition from IDLE to DETECTING after 2 seconds */

#define STUB_IDLE_TO_DETECT_FRAMES    40   /* 40 x 50 ms = 2 s */
#define STUB_DETECT_TO_RECOGNIZE  60   /* 3 s */
#define STUB_RECOGNIZE_TO_RESULT  40   /* 2 s */
#define STUB_RESULT_HOLD_FRAMES   100  /* 5 s */

/****************************************************************************
 * Private Functions
 ****************************************************************************/

static void signbridge_enter_idle(void)
{
  /* TODO(phase 4): stop camera stream, dim backlight, request PM idle */
}

static void signbridge_enter_detecting(void)
{
  /* TODO(phase 2): start camera stream
   * TODO(phase 3): run palm detection / fixed guide-frame check
   */
}

static void signbridge_enter_recognizing(void)
{
  /* TODO(phase 3): feed landmark window into the TCN classifier */
}

static void signbridge_enter_result(void)
{
  /* TODO(phase 1/4): push g_last_result to the LVGL result screen */
}

/****************************************************************************
 * Public Functions
 ****************************************************************************/

int signbridge_sm_init(void)
{
  g_state = SIGNBRIDGE_STATE_IDLE;
  g_result_pending = false;
  g_detect_frames = 0;
  g_classify_frames = 0;
  g_result_hold_frames = 0;
  memset(&g_last_result, 0, sizeof(g_last_result));

  /* Initialize the inference pipeline */

  int ret = signbridge_infer_init();
  if (ret < 0)
    {
      syslog(LOG_ERR, "signbridge: infer_init failed: %d\n", ret);
      return ret;
    }

  /* Initialize camera (test pattern for pipeline validation) */

  signbridge_camera_init(SIGNBRIDGE_CAM_SRC_TEST_PATTERN);
  signbridge_camera_start();

  /* Initialize voice announcement module */

  signbridge_voice_init();

  /* Initialize voice input (mic capture + VAD + wake word) */

  signbridge_audio_in_init();
  signbridge_correct_init();
  signbridge_pm_init();

  /* Start listening for the wake word ("你好，openvela") */

  signbridge_audio_in_start();

  signbridge_enter_idle();
  return OK;
}

enum signbridge_state_e signbridge_sm_state(void)
{
  return g_state;
}

const struct signbridge_result_s *signbridge_sm_last_result(void)
{
  if (g_result_pending)
    {
      return &g_last_result;
    }

  return NULL;
}

const char *signbridge_sm_utterance_text(void)
{
  return g_utterance_text;
}

/* Called periodically from the signbridge worker task (~50 ms).
 *
 * State machine flow (stub mode - automatic transitions for demo):
 *   IDLE -> DETECTING -> RECOGNIZING -> RESULT -> DETECTING (loop)
 *
 * When real camera/inference are integrated, the transitions are
 * triggered by actual hand detection and classification results.
 */

void signbridge_sm_step(void)
{
  struct signbridge_landmark_s landmarks[SIGNBRIDGE_NUM_LANDMARKS];
  struct signbridge_result_s result;
  int ret;

  /* Power management tick + wake word check */

  signbridge_pm_step();

  if (signbridge_audio_in_wakeword_heard())
    {
      signbridge_audio_in_clear_wakeword();
      signbridge_pm_activity();
      syslog(LOG_INFO, "signbridge: wake word detected - entering DETECTING\n");

      if (g_state == SIGNBRIDGE_STATE_IDLE)
        {
          g_state = SIGNBRIDGE_STATE_DETECTING;
          signbridge_enter_detecting();
        }
    }

  switch (g_state)
    {
      case SIGNBRIDGE_STATE_IDLE:
        /* Stub: auto-transition to DETECTING after a delay.
         * Real impl: wait for touch event or motion sensor trigger.
         */

        g_detect_frames++;
        if (g_detect_frames >= STUB_IDLE_TO_DETECT_FRAMES)
          {
            g_detect_frames = 0;
            g_state = SIGNBRIDGE_STATE_DETECTING;
            signbridge_enter_detecting();
          }

        break;

      case SIGNBRIDGE_STATE_DETECTING:
        /* Get a camera frame and run hand landmark detection.
         * Stub: auto-transition after N frames.
         * Real impl: check if hand landmark confidence > threshold.
         */

        {
          struct signbridge_frame_s cam_frame;
          ret = signbridge_camera_get_frame(&cam_frame, 100);
          if (ret == OK)
            {
              ret = signbridge_run_hand_landmark(
                  cam_frame.data, cam_frame.width, cam_frame.height,
                  landmarks);
              signbridge_camera_release_frame(&cam_frame);
            }
        }

        if (ret == OK)
          {
            g_detect_frames++;
            if (g_detect_frames >= STUB_DETECT_TO_RECOGNIZE)
              {
                g_detect_frames = 0;
                g_classify_frames = 0;
                g_state = SIGNBRIDGE_STATE_RECOGNIZING;
                signbridge_enter_recognizing();
              }
          }

        break;

      case SIGNBRIDGE_STATE_RECOGNIZING:
        /* Get camera frame, run hand landmark + temporal classification.
         * Stub: auto-transition after N frames.
         * Real impl: check classifier confidence > threshold.
         */

        {
          struct signbridge_frame_s cam_frame;
          ret = signbridge_camera_get_frame(&cam_frame, 100);
          if (ret == OK)
            {
              ret = signbridge_run_hand_landmark(
                  cam_frame.data, cam_frame.width, cam_frame.height,
                  landmarks);
              signbridge_camera_release_frame(&cam_frame);
            }
        }

        if (ret == OK)
          {
            g_classify_frames++;
            if (g_classify_frames >= STUB_RECOGNIZE_TO_RESULT)
              {
                /* Run classifier */

                ret = signbridge_run_classify(NULL, 0, &result);
                if (ret == OK)
                  {
                    signbridge_sm_post_result(&result);
                  }
              }
          }

        break;

      case SIGNBRIDGE_STATE_RESULT:
        /* Display result for a fixed duration, then return to DETECTING.
         * Real impl: wait for user gesture (touch/swipe) or timeout.
         */

        g_result_hold_frames++;
        if (g_result_hold_frames >= STUB_RESULT_HOLD_FRAMES)
          {
            struct signbridge_utterance_s utterance;

            g_result_hold_frames = 0;
            g_result_pending = false;
            g_detect_frames = 0;

            /* Finalize the utterance with the offline correction rules */

            if (signbridge_correct_flush(&utterance) == OK)
              {
                strlcpy(g_utterance_text, utterance.text,
                        sizeof(g_utterance_text));
              }

            g_state = SIGNBRIDGE_STATE_DETECTING;
            signbridge_enter_detecting();
          }

        break;
    }
}

int signbridge_sm_post_result(const struct signbridge_result_s *result)
{
  if (result == NULL)
    {
      return -EINVAL;
    }

  g_last_result = *result;
  g_result_pending = true;

  /* Feed the semantic correction module (offline fragment assembly) */

  {
    struct signbridge_fragment_s frag;

    frag.class_id   = result->class_id;
    frag.confidence = result->confidence;
    frag.ts_ms      = (uint32_t)((uint64_t)clock() * 1000 / CLOCKS_PER_SEC);
    signbridge_correct_add(&frag);
  }

  /* Any recognition is user activity (keep the screen on) */

  signbridge_pm_activity();

  if (g_state == SIGNBRIDGE_STATE_RECOGNIZING)
    {
      g_state = SIGNBRIDGE_STATE_RESULT;
      signbridge_enter_result();

      /* Announce the recognized sign by voice */

      signbridge_voice_play(result->class_id);
    }

  return OK;
}
