/****************************************************************************
 * contest2026_408_signbridge/app/signbridge/signbridge_infer.c
 *
 * SPDX-License-Identifier: Apache-2.0
 *
 * Inference pipeline wrapper for sign language recognition.
 *
 * Architecture:
 *   Camera frame → Hand detection (ROI crop) → Hand landmark model →
 *   Landmark window → Temporal classifier → Sign class + confidence
 *
 * This file implements the C wrapper that bridges the signbridge state
 * machine to the TFLite Micro inference engine.  The actual TFLM
 * integration is gated on CONFIG_TFLITEMICRO; when disabled, the
 * inference functions operate in stub mode (return dummy landmarks
 * and fixed-class results) to allow end-to-end pipeline testing
 * without a real model or camera.
 *
 ****************************************************************************/

/****************************************************************************
 * Included Files
 ****************************************************************************/

#include <nuttx/config.h>

#include <errno.h>
#include <math.h>
#include <string.h>
#include <syslog.h>

#include "signbridge_infer.h"

/****************************************************************************
 * Private Data
 ****************************************************************************/

#ifdef CONFIG_TFLITEMICRO
/* TODO: TFLite Micro interpreter instances, model buffers, arena */
#endif

/* Landmark history ring buffer for the temporal classifier.
 * g_landmark_window stores frames in chronological order while the ring
 * is not yet full, so it can be handed to the classifier directly.
 */

static struct signbridge_landmark_s
    g_landmark_window[SIGNBRIDGE_WINDOW_FRAMES]
                     [SIGNBRIDGE_NUM_LANDMARKS];
static int g_window_head;
static int g_window_count;

/* Staging buffer used to present a wrapped ring in chronological order */

static struct signbridge_landmark_s
    g_window_staging[SIGNBRIDGE_WINDOW_FRAMES]
                    [SIGNBRIDGE_NUM_LANDMARKS];

/****************************************************************************
 * Public Functions: landmark window (shared by all backends)
 ****************************************************************************/

void signbridge_infer_push_landmarks(
    const struct signbridge_landmark_s *frame)
{
  if (frame == NULL)
    {
      return;
    }

  memcpy(g_landmark_window[g_window_head], frame,
         SIGNBRIDGE_NUM_LANDMARKS * sizeof(*frame));

  g_window_head = (g_window_head + 1) % SIGNBRIDGE_WINDOW_FRAMES;
  if (g_window_count < SIGNBRIDGE_WINDOW_FRAMES)
    {
      g_window_count++;
    }
}

void signbridge_infer_reset_window(void)
{
  g_window_head  = 0;
  g_window_count = 0;
}

void signbridge_infer_get_window(
    const struct signbridge_landmark_s **window, int *frames)
{
  if (window == NULL || frames == NULL)
    {
      return;
    }

  if (g_window_count < SIGNBRIDGE_WINDOW_FRAMES)
    {
      /* The ring has not wrapped yet: frames sit in chronological order
       * starting at index 0.
       */

      *window = &g_landmark_window[0][0];
      *frames = g_window_count;
    }
  else
    {
      /* The ring wrapped: unwind it into the staging buffer so the
       * classifier always sees the window in chronological order.
       * g_window_head points at the oldest frame in that case.
       */

      int first = g_window_head;
      int n1 = SIGNBRIDGE_WINDOW_FRAMES - first;

      memcpy(g_window_staging[0], g_landmark_window[first],
             n1 * sizeof(g_landmark_window[0]));
      memcpy(g_window_staging[n1], g_landmark_window[0],
             first * sizeof(g_landmark_window[0]));

      *window = &g_window_staging[0][0];
      *frames = SIGNBRIDGE_WINDOW_FRAMES;
    }
}

/****************************************************************************
 * Public Functions
 ****************************************************************************/

#ifdef CONFIG_TFLITEMICRO
/* When TFLite Micro is enabled, signbridge_infer_init() and
 * signbridge_run_hand_landmark() are implemented in
 * signbridge_infer_tflm.cc; nothing here may define them again
 * (duplicate C-linkage symbols would silently shadow the TFLM backend
 * in the archive).  The temporal classifier below (INT8 MLP) is shared
 * by both backends.
 */
#else
int signbridge_infer_init(void)
{
  syslog(LOG_INFO, "signbridge: inference stub mode (no TFLM)\n");

  /* Initialize the MLP sign language classifier */

  extern void signbridge_cls_init(void);
  signbridge_cls_init();

  g_window_head  = 0;
  g_window_count = 0;
  return OK;
}
#endif /* !CONFIG_TFLITEMICRO */

#ifndef CONFIG_TFLITEMICRO
int signbridge_run_hand_landmark(const uint8_t *image,
                                 int width, int height,
                                 struct signbridge_landmark_s *landmarks)
{
  /* Stub: generate a fixed "open hand" landmark set for pipeline testing.
   * The 21 MediaPipe hand keypoints arranged in a rough hand shape at the
   * center of the image.
   */

  static const float stub_landmarks[SIGNBRIDGE_NUM_LANDMARKS][3] =
  {
    {0.50f, 0.85f, 0.0f},  /* 0  WRIST          */
    {0.42f, 0.70f, 0.0f},  /* 1  THUMB_CMC      */
    {0.35f, 0.55f, 0.0f},  /* 2  THUMB_MCP      */
    {0.30f, 0.45f, 0.0f},  /* 3  THUMB_IP       */
    {0.28f, 0.38f, 0.0f},  /* 4  THUMB_TIP      */
    {0.50f, 0.65f, 0.0f},  /* 5  INDEX_FINGER_MCP */
    {0.50f, 0.50f, 0.0f},  /* 6  INDEX_FINGER_PIP */
    {0.50f, 0.40f, 0.0f},  /* 7  INDEX_FINGER_DIP */
    {0.50f, 0.32f, 0.0f},  /* 8  INDEX_FINGER_TIP */
    {0.55f, 0.65f, 0.0f},  /* 9  MIDDLE_FINGER_MCP */
    {0.55f, 0.48f, 0.0f},  /* 10 MIDDLE_FINGER_PIP */
    {0.55f, 0.38f, 0.0f},  /* 11 MIDDLE_FINGER_DIP */
    {0.55f, 0.30f, 0.0f},  /* 12 MIDDLE_FINGER_TIP */
    {0.60f, 0.67f, 0.0f},  /* 13 RING_FINGER_MCP */
    {0.60f, 0.52f, 0.0f},  /* 14 RING_FINGER_PIP */
    {0.60f, 0.42f, 0.0f},  /* 15 RING_FINGER_DIP */
    {0.60f, 0.35f, 0.0f},  /* 16 RING_FINGER_TIP */
    {0.65f, 0.70f, 0.0f},  /* 17 PINKY_MCP       */
    {0.65f, 0.58f, 0.0f},  /* 18 PINKY_PIP       */
    {0.65f, 0.50f, 0.0f},  /* 19 PINKY_DIP       */
    {0.65f, 0.44f, 0.0f},  /* 20 PINKY_TIP       */
  };

  int i;
  UNUSED(image);
  UNUSED(width);
  UNUSED(height);

  for (i = 0; i < SIGNBRIDGE_NUM_LANDMARKS; i++)
    {
      landmarks[i].x = stub_landmarks[i][0];
      landmarks[i].y = stub_landmarks[i][1];
      landmarks[i].z = stub_landmarks[i][2];
    }

  return OK;
}
#endif /* !CONFIG_TFLITEMICRO */

/****************************************************************************
 * Public Functions
 ****************************************************************************/

int signbridge_run_classify(const struct signbridge_landmark_s *window,
                            int frames,
                            struct signbridge_result_s *result)
{
  /* Run the INT8 MLP temporal classifier on the landmark window (shared
   * by the stub and the TFLM backends; the TFLM variant of the temporal
   * classifier is not exported yet).  A NULL window falls back to the
   * internally accumulated one.
   */

  extern int signbridge_cls_run(const struct signbridge_landmark_s *window,
                                 int frames,
                                 struct signbridge_result_s *result);

  if (window == NULL || frames <= 0)
    {
      signbridge_infer_get_window(&window, &frames);
    }

  return signbridge_cls_run(window, frames, result);
}

#ifndef CONFIG_TFLITEMICRO
void signbridge_infer_deinit(void)
{
  g_window_head  = 0;
  g_window_count = 0;
}
#endif /* !CONFIG_TFLITEMICRO */
