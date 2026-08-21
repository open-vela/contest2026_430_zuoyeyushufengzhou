/****************************************************************************
 * contest2026_408_signbridge/app/signbridge/signbridge_camera.h
 *
 * SPDX-License-Identifier: Apache-2.0
 *
 * Camera frame source abstraction for SignBridge.
 *
 * Provides a unified interface for acquiring camera frames, regardless
 * of the underlying source:
 *   - MIPI-CSI (SC2336 sensor via esp_cam_ctlr CSI driver)
 *   - Test pattern generator (for pipeline validation without camera)
 *   - File-based frame source (for offline testing)
 *
 * The frame format is RGB565 (matching the EK79007 display) or
 * grayscale (for inference input).
 *
 ****************************************************************************/

#ifndef __APP_SIGNBRIDGE_SIGNBRIDGE_CAMERA_H
#define __APP_SIGNBRIDGE_SIGNBRIDGE_CAMERA_H

/****************************************************************************
 * Included Files
 ****************************************************************************/

#include <nuttx/config.h>
#include <stdint.h>
#include <stdbool.h>

/****************************************************************************
 * Pre-processor Definitions
 ****************************************************************************/

/* Camera frame dimensions (SC2336 sensor: 1920x1080 native,
 * we crop/scale to the hand region for inference)
 */

#define SIGNBRIDGE_CAM_WIDTH     640
#define SIGNBRIDGE_CAM_HEIGHT    480

/* Inference input size (cropped hand region) */

#define SIGNBRIDGE_INFER_WIDTH   192
#define SIGNBRIDGE_INFER_HEIGHT  192

/* Frame buffer format */

#define SIGNBRIDGE_CAM_FMT_RGB565   0
#define SIGNBRIDGE_CAM_FMT_GRAY     1
#define SIGNBRIDGE_CAM_FMT_RGB888   2

/****************************************************************************
 * Public Types
 ****************************************************************************/

/* Camera frame descriptor */

struct signbridge_frame_s
{
  uint8_t *data;          /* Frame buffer pointer */
  uint32_t size;          /* Frame buffer size in bytes */
  uint16_t width;         /* Frame width in pixels */
  uint16_t height;        /* Frame height in pixels */
  uint8_t  format;        /* SIGNBRIDGE_CAM_FMT_* */
  uint32_t timestamp_ms;  /* Capture timestamp */
  uint32_t sequence;      /* Frame sequence number */
};

/* Camera source type */

enum signbridge_cam_source_e
{
  SIGNBRIDGE_CAM_SRC_NONE = 0,     /* No camera */
  SIGNBRIDGE_CAM_SRC_TEST_PATTERN, /* Test pattern generator */
  SIGNBRIDGE_CAM_SRC_MIPI_CSI,     /* MIPI-CSI (SC2336) */
  SIGNBRIDGE_CAM_SRC_FILE,         /* File-based frames */
};

/****************************************************************************
 * Public Function Prototypes
 ****************************************************************************/

#ifdef __cplusplus
extern "C"
{
#endif

/****************************************************************************
 * Name: signbridge_camera_init
 *
 * Description:
 *   Initialize the camera frame source.
 *
 * Input Parameters:
 *   source - Camera source type
 *
 * Returned Value:
 *   0 on success, negative errno on failure.
 *
 ****************************************************************************/

int signbridge_camera_init(enum signbridge_cam_source_e source);

/****************************************************************************
 * Name: signbridge_camera_start
 *
 * Description:
 *   Start camera streaming.
 *
 ****************************************************************************/

int signbridge_camera_start(void);

/****************************************************************************
 * Name: signbridge_camera_stop
 *
 * Description:
 *   Stop camera streaming.
 *
 ****************************************************************************/

int signbridge_camera_stop(void);

/****************************************************************************
 * Name: signbridge_camera_get_frame
 *
 * Description:
 *   Acquire the next camera frame (blocking with timeout).
 *
 * Output Parameters:
 *   frame - Filled with frame data pointer and metadata
 *
 * Returned Value:
 *   0 on success, negative errno on failure/timeout.
 *
 ****************************************************************************/

int signbridge_camera_get_frame(struct signbridge_frame_s *frame,
                                uint32_t timeout_ms);

/****************************************************************************
 * Name: signbridge_camera_release_frame
 *
 * Description:
 *   Release a previously acquired frame buffer back to the camera driver.
 *
 ****************************************************************************/

void signbridge_camera_release_frame(struct signbridge_frame_s *frame);

/****************************************************************************
 * Name: signbridge_camera_deinit
 *
 * Description:
 *   Release camera resources.
 *
 ****************************************************************************/

void signbridge_camera_deinit(void);

#ifdef __cplusplus
}
#endif

#endif /* __APP_SIGNBRIDGE_SIGNBRIDGE_CAMERA_H */
