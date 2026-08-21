/****************************************************************************
 * contest2026_408_signbridge/app/signbridge/signbridge_infer.h
 *
 * SPDX-License-Identifier: Apache-2.0
 *
 * Hand landmark inference wrapper for TFLite Micro.
 *
 ****************************************************************************/

#ifndef __APP_SIGNBRIDGE_SIGNBRIDGE_INFER_H
#define __APP_SIGNBRIDGE_SIGNBRIDGE_INFER_H

/****************************************************************************
 * Included Files
 ****************************************************************************/

#include <nuttx/config.h>
#include <stdint.h>

#include "signbridge.h"

/* Hand landmark model: 21 keypoints x 3 coordinates (x, y, z) */

/* SIGNBRIDGE_NUM_LANDMARKS and SIGNBRIDGE_LANDMARK_DIM are in signbridge.h */

/* Input image size (cropped hand region) */

#define SIGNBRIDGE_INPUT_WIDTH      192
#define SIGNBRIDGE_INPUT_HEIGHT     192

/* Max number of sign classes in the temporal classifier */

#define SIGNBRIDGE_MAX_CLASSES      CONFIG_DEMOS_SIGNBRIDGE_VOCAB_SIZE

/* Model arena size (must be large enough for the hand_landmark model) */

#define SIGNBRIDGE_INFER_ARENA_SIZE (256 * 1024)

/* Recognition result (in signbridge.h) */

/****************************************************************************
 * Public Function Prototypes
 ****************************************************************************/

#ifdef __cplusplus
extern "C"
{
#endif

/****************************************************************************
 * Name: signbridge_infer_init
 *
 * Description:
 *   Initialize the TFLite Micro interpreter and load models.
 *
 * Returned Value:
 *   0 on success, negative errno on failure.
 *
 ****************************************************************************/

int signbridge_infer_init(void);

/****************************************************************************
 * Name: signbridge_run_hand_landmark
 *
 * Description:
 *   Run hand landmark detection on a grayscale/cropped hand image.
 *
 * Input Parameters:
 *   image   - Pointer to RGB888 or grayscale image data
 *   width   - Image width in pixels
 *   height  - Image height in pixels
 *
 * Output Parameters:
 *   landmarks - Array of SIGNBRIDGE_NUM_LANDMARKS landmark positions
 *
 * Returned Value:
 *   0 on success, negative errno on failure.
 *
 ****************************************************************************/

int signbridge_run_hand_landmark(const uint8_t *image,
                                 int width, int height,
                                 struct signbridge_landmark_s *landmarks);

/****************************************************************************
 * Name: signbridge_infer_push_landmarks
 *
 * Description:
 *   Append one landmark frame to the internal sliding window consumed by
 *   the temporal classifier.  Called once per camera frame while a hand
 *   is tracked (DETECTING / RECOGNIZING states).
 *
 * Input Parameters:
 *   frame - Array of SIGNBRIDGE_NUM_LANDMARKS landmark positions
 *
 ****************************************************************************/

void signbridge_infer_push_landmarks(
    const struct signbridge_landmark_s *frame);

/****************************************************************************
 * Name: signbridge_infer_reset_window
 *
 * Description:
 *   Clear the landmark sliding window (new gesture starts).
 *
 ****************************************************************************/

void signbridge_infer_reset_window(void);

/****************************************************************************
 * Name: signbridge_infer_get_window
 *
 * Description:
 *   Get the accumulated landmark window in chronological order for the
 *   temporal classifier.
 *
 * Output Parameters:
 *   window - Receives a pointer to the contiguous frame array
 *   frames - Receives the number of accumulated frames
 *            (<= SIGNBRIDGE_WINDOW_FRAMES)
 *
 ****************************************************************************/

void signbridge_infer_get_window(
    const struct signbridge_landmark_s **window, int *frames);

/****************************************************************************
 * Name: signbridge_run_classify
 *
 * Description:
 *   Run temporal classification on a sliding window of landmarks.
 *
 * Input Parameters:
 *   window   - Array of landmark frames (window_size x 21 x 3)
 *   frames   - Number of frames in the window
 *
 * Output Parameters:
 *   result   - Classification result
 *
 * Returned Value:
 *   0 on success, negative errno on failure.
 *
 ****************************************************************************/

int signbridge_run_classify(const struct signbridge_landmark_s *window,
                            int frames,
                            struct signbridge_result_s *result);

/****************************************************************************
 * Name: signbridge_infer_deinit
 *
 * Description:
 *   Release inference resources.
 *
 ****************************************************************************/

void signbridge_infer_deinit(void);

#ifdef __cplusplus
}
#endif

#endif /* __APP_SIGNBRIDGE_SIGNBRIDGE_INFER_H */
