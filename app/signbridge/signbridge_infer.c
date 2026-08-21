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

/* Landmark history ring buffer for temporal classifier */

static struct signbridge_landmark_s
    g_landmark_window[SIGNBRIDGE_WINDOW_FRAMES]
                     [SIGNBRIDGE_NUM_LANDMARKS];
static int g_window_head;
static int g_window_count;

/****************************************************************************
 * Private Functions
 ****************************************************************************/

/****************************************************************************
 * Name: landmarks_stable
 *
 * Description:
 *   Check if the landmark positions are stable (hand is present and
 *   not moving too fast) by computing the mean squared displacement
 *   between consecutive frames.
 *
 ****************************************************************************/

static bool landmarks_stable(const struct signbridge_landmark_s *cur,
                             const struct signbridge_landmark_s *prev,
                             float threshold)
{
  float sum = 0.0f;
  int i;

  for (i = 0; i < SIGNBRIDGE_NUM_LANDMARKS; i++)
    {
      float dx = cur[i].x - prev[i].x;
      float dy = cur[i].y - prev[i].y;
      float dz = cur[i].z - prev[i].z;
      sum += dx * dx + dy * dy + dz * dz;
    }

  return (sum / SIGNBRIDGE_NUM_LANDMARKS) < threshold;
}

/****************************************************************************
 * Public Functions
 ****************************************************************************/

int signbridge_infer_init(void)
{
#ifdef CONFIG_TFLITEMICRO
  syslog(LOG_INFO, "signbridge: TFLM inference init\n");
#else
  syslog(LOG_INFO, "signbridge: inference stub mode (no TFLM)\n");
#endif

  /* Initialize the MLP sign language classifier */

  extern void signbridge_cls_init(void);
  signbridge_cls_init();

  g_window_head  = 0;
  g_window_count = 0;
  return OK;
}

int signbridge_run_hand_landmark(const uint8_t *image,
                                 int width, int height,
                                 struct signbridge_landmark_s *landmarks)
{
#ifdef CONFIG_TFLITEMICRO
  /* TODO: Pre-process image → quantize to INT8 → invoke hand_landmark model
   * → dequantize output → fill landmarks[]
   */

  memset(landmarks, 0,
         SIGNBRIDGE_NUM_LANDMARKS * sizeof(struct signbridge_landmark_s));
  return -ENOSYS;
#else
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
#endif
}

int signbridge_run_classify(const struct signbridge_landmark_s *window,
                            int frames,
                            struct signbridge_result_s *result)
{
  /* Run the MLP temporal classifier on the landmark window.
   * Works in both stub and TFLM modes.
   */

  extern int signbridge_cls_run(const struct signbridge_landmark_s *window,
                                 int frames,
                                 struct signbridge_result_s *result);
  return signbridge_cls_run(window, frames, result);
}

void signbridge_infer_deinit(void)
{
#ifdef CONFIG_TFLITEMICRO
  /* TODO: Free interpreter instances and arena memory */
#endif

  g_window_head  = 0;
  g_window_count = 0;
}
