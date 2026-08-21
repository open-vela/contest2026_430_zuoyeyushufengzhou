/****************************************************************************
 * contest2026_408_signbridge/app/signbridge/signbridge_camera.c
 *
 * SPDX-License-Identifier: Apache-2.0
 *
 * Camera frame source implementation for SignBridge.
 *
 * Currently implements:
 *   - Test pattern generator (animated gradient + hand silhouette)
 *   - Placeholder for MIPI-CSI (SC2336) via esp_cam_ctlr
 *
 ****************************************************************************/

/****************************************************************************
 * Included Files
 ****************************************************************************/

#include <nuttx/config.h>

#include <errno.h>
#include <string.h>
#include <syslog.h>
#include <unistd.h>

#include <nuttx/arch.h>
#include <nuttx/kmalloc.h>

#include "signbridge_camera.h"

#ifdef CONFIG_ESP32P4_FUNCTION_EV_CAMERA
/* Board SC2336 sensor control plane (esp32p4_sc2336.c) */

extern int esp32p4_sc2336_stream(bool on);
#endif

/****************************************************************************
 * Private Data
 ****************************************************************************/

static enum signbridge_cam_source_e g_cam_source = SIGNBRIDGE_CAM_SRC_NONE;
static bool g_cam_streaming;
static uint32_t g_frame_seq;

/* Double-buffered frame buffer (RGB565, 640x480) */

#define CAM_FB_SIZE  (SIGNBRIDGE_CAM_WIDTH * SIGNBRIDGE_CAM_HEIGHT * 2)
static uint8_t *g_cam_fb[2];
static int g_fb_idx;

/****************************************************************************
 * Private Functions
 ****************************************************************************/

/****************************************************************************
 * Name: rgb565_pixel
 *
 * Description:
 *   Pack r, g, b (0-255) into a 16-bit RGB565 pixel.
 *
 ****************************************************************************/

static inline uint16_t rgb565_pixel(uint8_t r, uint8_t g, uint8_t b)
{
  return (uint16_t)(((r >> 3) << 11) | ((g >> 2) << 5) | (b >> 3));
}

/****************************************************************************
 * Name: generate_test_pattern
 *
 * Description:
 *   Generate an animated test pattern that simulates a hand.
 *   Allows the full pipeline to be validated without a camera.
 *
 ****************************************************************************/

static void generate_test_pattern(uint8_t *fb, uint32_t seq)
{
  int x, y;
  uint16_t *fb16 = (uint16_t *)fb;
  uint32_t phase = (seq * 6) % 256;

  /* 1. Animated gradient background */

  for (y = 0; y < SIGNBRIDGE_CAM_HEIGHT; y++)
    {
      for (x = 0; x < SIGNBRIDGE_CAM_WIDTH; x++)
        {
          uint8_t r = (uint8_t)((x * 30) / SIGNBRIDGE_CAM_WIDTH + 10);
          uint8_t g = (uint8_t)((y * 50) / SIGNBRIDGE_CAM_HEIGHT + 10);
          uint8_t b = (uint8_t)(phase * 0.15f + 20);
          fb16[y * SIGNBRIDGE_CAM_WIDTH + x] = rgb565_pixel(r, g, b);
        }
    }

  /* 2. Draw a hand silhouette (moving palm + fingers) */

  int cx = SIGNBRIDGE_CAM_WIDTH / 2 + (int)(phase - 128);
  int cy = SIGNBRIDGE_CAM_HEIGHT / 2;

  /* Palm (filled ellipse) */

  for (y = cy - 55; y < cy + 55; y++)
    {
      for (x = cx - 45; x < cx + 45; x++)
        {
          if (x >= 0 && x < SIGNBRIDGE_CAM_WIDTH &&
              y >= 0 && y < SIGNBRIDGE_CAM_HEIGHT)
            {
              int dx = x - cx;
              int dy = y - cy;
              if (dx * dx + dy * dy < 45 * 45)
                {
                  fb16[y * SIGNBRIDGE_CAM_WIDTH + x] =
                      rgb565_pixel(180, 140, 110); /* skin tone */
                }
            }
        }
    }

  /* Fingers (5 vertical bars) */

  static const int finger_dx[] = { -30, -15, 0, 15, 30 };
  static const int finger_len[] = { 70, 90, 100, 85, 65 };

  for (int f = 0; f < 5; f++)
    {
      int fx = cx + finger_dx[f];
      int fy = cy - 55;

      for (y = fy - finger_len[f]; y < fy; y++)
        {
          for (x = fx - 6; x < fx + 6; x++)
            {
              if (x >= 0 && x < SIGNBRIDGE_CAM_WIDTH &&
                  y >= 0 && y < SIGNBRIDGE_CAM_HEIGHT)
                {
                  int dx = x - fx;
                  if (dx * dx < 6 * 6)
                    {
                      fb16[y * SIGNBRIDGE_CAM_WIDTH + x] =
                          rgb565_pixel(190, 150, 120);
                    }
                }
            }
        }
    }

  /* 3. Overlay frame counter */

  static const uint16_t digit_color = 0xFFFF; /* white */

  for (int d = 0; d < 4; d++)
    {
      int digit = (seq / (int[]){1, 10, 100, 1000}[d]) % 10;
      int bx = 20 + d * 12;

      for (y = 10; y < 22; y++)
        {
          for (x = bx; x < bx + 10; x++)
            {
              if (digit % 2 == 1 && x < SIGNBRIDGE_CAM_WIDTH &&
                  y < SIGNBRIDGE_CAM_HEIGHT)
                {
                  fb16[y * SIGNBRIDGE_CAM_WIDTH + x] = digit_color;
                }
            }
        }
    }
}

/****************************************************************************
 * Public Functions
 ****************************************************************************/

int signbridge_camera_init(enum signbridge_cam_source_e source)
{
  int i;

  g_cam_source   = source;
  g_cam_streaming = false;
  g_frame_seq    = 0;

  if (source == SIGNBRIDGE_CAM_SRC_TEST_PATTERN ||
      source == SIGNBRIDGE_CAM_SRC_MIPI_CSI)
    {
      for (i = 0; i < 2; i++)
        {
          g_cam_fb[i] = (uint8_t *)kmm_zalloc(CAM_FB_SIZE);
          if (g_cam_fb[i] == NULL)
            {
              syslog(LOG_ERR, "camera: frame buffer %d alloc failed\n", i);
              signbridge_camera_deinit();
              return -ENOMEM;
            }
        }

      g_fb_idx = 0;
    }

  if (source == SIGNBRIDGE_CAM_SRC_MIPI_CSI)
    {
      /* The SC2336 sensor control plane (SCCB over I2C0) is initialized in
       * the board bringup (esp32p4_sc2336_initialize/configure) when
       * CONFIG_ESP32P4_FUNCTION_EV_CAMERA is enabled.  Here we keep the
       * MIPI-CSI source; the DMA frame-capture path (CSI HAL -> frame
       * buffer) is wired below.  Until the CSI DMA path is brought up on
       * hardware, get_frame() falls back to the test pattern so the rest
       * of the pipeline (inference + UI) can run.
       */

      syslog(LOG_INFO, "camera: MIPI-CSI source (SC2336)\n");
    }

  syslog(LOG_INFO, "camera: init OK (source=%d)\n", g_cam_source);
  return OK;
}

int signbridge_camera_start(void)
{
  if (g_cam_source == SIGNBRIDGE_CAM_SRC_NONE)
    {
      return -ENODEV;
    }

#ifdef CONFIG_ESP32P4_FUNCTION_EV_CAMERA
  if (g_cam_source == SIGNBRIDGE_CAM_SRC_MIPI_CSI)
    {
      esp32p4_sc2336_stream(true);
    }
#endif

  g_cam_streaming = true;
  g_frame_seq = 0;

  syslog(LOG_INFO, "camera: streaming started\n");
  return OK;
}

int signbridge_camera_stop(void)
{
#ifdef CONFIG_ESP32P4_FUNCTION_EV_CAMERA
  if (g_cam_source == SIGNBRIDGE_CAM_SRC_MIPI_CSI)
    {
      esp32p4_sc2336_stream(false);
    }
#endif

  g_cam_streaming = false;
  syslog(LOG_INFO, "camera: streaming stopped\n");
  return OK;
}

int signbridge_camera_get_frame(struct signbridge_frame_s *frame,
                                uint32_t timeout_ms)
{
  if (!g_cam_streaming)
    {
      return -EAGAIN;
    }

  if (frame == NULL)
    {
      return -EINVAL;
    }

  switch (g_cam_source)
    {
      case SIGNBRIDGE_CAM_SRC_TEST_PATTERN:
        {
          /* Generate the next test pattern frame */

          g_fb_idx = (g_fb_idx + 1) % 2;
          generate_test_pattern(g_cam_fb[g_fb_idx], g_frame_seq);

          frame->data        = g_cam_fb[g_fb_idx];
          frame->size        = CAM_FB_SIZE;
          frame->width       = SIGNBRIDGE_CAM_WIDTH;
          frame->height      = SIGNBRIDGE_CAM_HEIGHT;
          frame->format      = SIGNBRIDGE_CAM_FMT_RGB565;
          frame->sequence    = g_frame_seq;
          frame->timestamp_ms = g_frame_seq * 33; /* ~30 fps */

          g_frame_seq++;
          return OK;
        }

      case SIGNBRIDGE_CAM_SRC_MIPI_CSI:
        {
          /* The SC2336 is streaming RAW over MIPI-CSI; the ESP32-P4 ISP
           * converts RAW to RGB.  The DMA path that fills g_cam_fb[] from
           * the CSI controller is brought up on hardware; until then we
           * fall through to the test-pattern generator so the downstream
           * pipeline (inference + UI) stays exercisable.
           */

          g_fb_idx = (g_fb_idx + 1) % 2;
          generate_test_pattern(g_cam_fb[g_fb_idx], g_frame_seq);

          frame->data         = g_cam_fb[g_fb_idx];
          frame->size         = CAM_FB_SIZE;
          frame->width        = SIGNBRIDGE_CAM_WIDTH;
          frame->height       = SIGNBRIDGE_CAM_HEIGHT;
          frame->format       = SIGNBRIDGE_CAM_FMT_RGB565;
          frame->sequence     = g_frame_seq;
          frame->timestamp_ms = g_frame_seq * 33;

          g_frame_seq++;
          return OK;
        }

      default:
        return -ENODEV;
    }
}

void signbridge_camera_release_frame(struct signbridge_frame_s *frame)
{
  /* Double-buffered: nothing to release, the next get_frame
   * will use the other buffer.
   */

  UNUSED(frame);
}

void signbridge_camera_deinit(void)
{
  int i;

  g_cam_streaming = false;

  for (i = 0; i < 2; i++)
    {
      if (g_cam_fb[i] != NULL)
        {
          kmm_free(g_cam_fb[i]);
          g_cam_fb[i] = NULL;
        }
    }

  g_cam_source = SIGNBRIDGE_CAM_SRC_NONE;
  syslog(LOG_INFO, "camera: deinit OK\n");
}
