/****************************************************************************
 * contest2026_408_signbridge/app/signbridge/signbridge_ui.c
 *
 * SPDX-License-Identifier: Apache-2.0
 *
 * LVGL user interface for the SignBridge sign language recognition
 * terminal on the ESP32-P4-Function-EV-Board (7" 1024x600 landscape
 * display, EK79007AD).
 *
 * UI layout (landscape 1024x600): left column holds the result text,
 * confidence bar, status and info bars; the right column holds the
 * 500x500 camera preview canvas.
 *
 ****************************************************************************/

/****************************************************************************
 * Included Files
 ****************************************************************************/

#include <nuttx/config.h>

#include <stdio.h>
#include <string.h>
#include <syslog.h>
#include <unistd.h>

#include <lvgl/lvgl.h>

#include "signbridge.h"
#include "signbridge_audio_in.h"
#include "signbridge_camera.h"
#include "signbridge_pm.h"
#include "signbridge_ui.h"
#include "signbridge_vocab.h"

/****************************************************************************
 * Pre-processor Definitions
 ****************************************************************************/

/* UI layout constants (1024x600 landscape, EK79007 panel) */

#define UI_SCREEN_W         1024
#define UI_SCREEN_H         600

#define UI_TITLE_Y          0
#define UI_TITLE_H          56

/* Left column (result / confidence / status / info) */

#define UI_LEFT_W           480
#define UI_RESULT_Y         80
#define UI_RESULT_H         160
#define UI_BAR_Y            260
#define UI_BAR_H            24
#define UI_STATUS_Y         320
#define UI_STATUS_H         40
#define UI_INFO_Y           540
#define UI_INFO_H           40

/* Right column (camera preview) */

#define UI_PREVIEW_X        504
#define UI_PREVIEW_Y        72
#define UI_PREVIEW_W        500
#define UI_PREVIEW_H        512
#define UI_CANVAS_W         500
#define UI_CANVAS_H         500

/* Colors */

#define UI_COLOR_BG         lv_color_hex(0x1a1a2e)
#define UI_COLOR_TITLE      lv_color_hex(0x00d4aa)
#define UI_COLOR_RESULT     lv_color_hex(0xffffff)
#define UI_COLOR_BAR_BG     lv_color_hex(0x333355)
#define UI_COLOR_BAR_IND    lv_color_hex(0x00d4aa)
#define UI_COLOR_PREVIEW    lv_color_hex(0x0f0f23)
#define UI_COLOR_STATUS     lv_color_hex(0x8888aa)

/****************************************************************************
 * Private Data
 ****************************************************************************/

static lv_obj_t *g_title_label;
static lv_obj_t *g_result_label;
static lv_obj_t *g_conf_bar;
static lv_obj_t *g_preview_panel;
static lv_obj_t *g_status_label;
static lv_obj_t *g_info_label;
static lv_obj_t *g_preview_canvas;
static uint16_t g_canvas_buf[UI_CANVAS_W * UI_CANVAS_H];

static enum signbridge_state_e g_last_state = SIGNBRIDGE_STATE_IDLE;
static uint32_t g_cam_frame_seq;
static bool g_ui_initialized;

/****************************************************************************
 * Private Functions
 ****************************************************************************/

static const char *state_to_string(enum signbridge_state_e state)
{
  switch (state)
    {
      case SIGNBRIDGE_STATE_IDLE:
        return "IDLE - waiting for hand";
      case SIGNBRIDGE_STATE_DETECTING:
        return "DETECTING - looking for hand...";
      case SIGNBRIDGE_STATE_RECOGNIZING:
        return "RECOGNIZING - tracking sign...";
      case SIGNBRIDGE_STATE_RESULT:
        return "RESULT";
      default:
        return "UNKNOWN";
    }
}

static void create_title(lv_obj_t *parent)
{
  g_title_label = lv_label_create(parent);
  lv_label_set_text(g_title_label, "SignBridge");
  lv_obj_set_style_text_font(g_title_label, &lv_font_montserrat_14, 0);
  lv_obj_set_style_text_color(g_title_label, UI_COLOR_TITLE, 0);
  lv_obj_set_style_bg_color(g_title_label, UI_COLOR_BG, 0);
  lv_obj_set_style_bg_opa(g_title_label, LV_OPA_COVER, 0);
  lv_obj_set_style_pad_all(g_title_label, 10, 0);
  lv_obj_set_size(g_title_label, UI_SCREEN_W, UI_TITLE_H);
  lv_obj_align(g_title_label, LV_ALIGN_TOP_LEFT, 0, UI_TITLE_Y);
}

static void create_result_area(lv_obj_t *parent)
{
  g_result_label = lv_label_create(parent);
  lv_label_set_text(g_result_label, "---");
  lv_obj_set_style_text_font(g_result_label, &lv_font_montserrat_14, 0);
  lv_obj_set_style_text_color(g_result_label, UI_COLOR_RESULT, 0);
  lv_obj_set_style_text_align(g_result_label, LV_TEXT_ALIGN_CENTER, 0);
  lv_label_set_long_mode(g_result_label, LV_LABEL_LONG_WRAP);
  lv_obj_set_width(g_result_label, UI_LEFT_W - 40);
  lv_obj_align(g_result_label, LV_ALIGN_TOP_LEFT, 20, UI_RESULT_Y);
}

static void create_confidence_bar(lv_obj_t *parent)
{
  g_conf_bar = lv_bar_create(parent);
  lv_bar_set_range(g_conf_bar, 0, 100);
  lv_bar_set_value(g_conf_bar, 0, LV_ANIM_OFF);
  lv_obj_set_size(g_conf_bar, UI_LEFT_W - 80, UI_BAR_H);
  lv_obj_align(g_conf_bar, LV_ALIGN_TOP_LEFT, 40, UI_BAR_Y);
  lv_obj_set_style_bg_color(g_conf_bar, UI_COLOR_BAR_BG, 0);
  lv_obj_set_style_bg_color(g_conf_bar, UI_COLOR_BAR_IND,
                             LV_PART_INDICATOR);
}

static void create_preview_area(lv_obj_t *parent)
{
  g_preview_panel = lv_obj_create(parent);
  lv_obj_set_size(g_preview_panel, UI_PREVIEW_W, UI_PREVIEW_H);
  lv_obj_align(g_preview_panel, LV_ALIGN_TOP_LEFT,
               UI_PREVIEW_X, UI_PREVIEW_Y);
  lv_obj_set_style_bg_color(g_preview_panel, UI_COLOR_PREVIEW, 0);
  lv_obj_set_style_bg_opa(g_preview_panel, LV_OPA_COVER, 0);
  lv_obj_set_style_border_width(g_preview_panel, 2, 0);
  lv_obj_set_style_border_color(g_preview_panel,
                                lv_color_hex(0x333355), 0);
  lv_obj_set_style_radius(g_preview_panel, 12, 0);

  /* Camera preview canvas (RGB565, UI_CANVAS_W x UI_CANVAS_H) */

  g_preview_canvas = lv_canvas_create(g_preview_panel);
  lv_canvas_set_buffer(g_preview_canvas, g_canvas_buf,
                       UI_CANVAS_W, UI_CANVAS_H,
                       LV_COLOR_FORMAT_RGB565);
  lv_obj_center(g_preview_canvas);
  lv_canvas_fill_bg(g_preview_canvas, lv_color_hex(0x0f0f23),
                    LV_OPA_COVER);

  /* Placeholder text inside preview area */

  lv_obj_t *placeholder = lv_label_create(g_preview_panel);
  lv_label_set_text(placeholder,
                    LV_SYMBOL_IMAGE "\n"
                    "Camera Preview\n\n"
                    "Connect MIPI-CSI\ncamera to display\nlive feed here");
  lv_obj_set_style_text_color(placeholder, lv_color_hex(0x555577), 0);
  lv_obj_set_style_text_align(placeholder, LV_TEXT_ALIGN_CENTER, 0);
  lv_obj_set_style_text_font(placeholder, &lv_font_montserrat_14, 0);
  lv_obj_center(placeholder);
}

static void create_status_bar(lv_obj_t *parent)
{
  g_status_label = lv_label_create(parent);
  lv_label_set_text(g_status_label, state_to_string(SIGNBRIDGE_STATE_IDLE));
  lv_obj_set_style_text_color(g_status_label, UI_COLOR_STATUS, 0);
  lv_obj_set_style_text_font(g_status_label, &lv_font_montserrat_14, 0);
  lv_obj_align(g_status_label, LV_ALIGN_TOP_LEFT, 20, UI_STATUS_Y);
}

static void create_info_bar(lv_obj_t *parent)
{
  g_info_label = lv_label_create(parent);
  lv_label_set_text(g_info_label,
                    "ESP32-P4 | openvela | SignBridge");
  lv_obj_set_style_text_color(g_info_label, lv_color_hex(0x444466), 0);
  lv_obj_set_style_text_font(g_info_label, &lv_font_montserrat_14, 0);
  lv_obj_align(g_info_label, LV_ALIGN_TOP_LEFT, 20, UI_INFO_Y);
}

/****************************************************************************
 * Name: blit_frame_to_canvas
 *
 * Description:
 *   Blit a RGB565 camera frame (640x480) to the LVGL canvas using simple
 *   nearest-neighbor scaling.  The canvas buffer is declared RGB565, so
 *   pixels are copied as raw 16-bit values.
 *
 ****************************************************************************/

static void blit_frame_to_canvas(const uint8_t *frame_data,
                                 uint16_t frame_w, uint16_t frame_h)
{
  const uint16_t *src;
  uint16_t *dst;
  int canvas_w = UI_CANVAS_W;
  int canvas_h = UI_CANVAS_H;
  int x;
  int y;

  if (g_preview_canvas == NULL || frame_data == NULL)
    {
      return;
    }

  src = (const uint16_t *)frame_data;
  dst = g_canvas_buf;

  for (y = 0; y < canvas_h; y++)
    {
      int src_y = (y * frame_h) / canvas_h;
      for (x = 0; x < canvas_w; x++)
        {
          int src_x = (x * frame_w) / canvas_w;

          dst[y * canvas_w + x] = src[src_y * frame_w + src_x];
        }
    }

  lv_obj_invalidate(g_preview_canvas);
}

/****************************************************************************
 * Public Functions
 ****************************************************************************/

int signbridge_ui_init(void)
{
  lv_nuttx_dsc_t info;
  lv_nuttx_result_t result;

  if (lv_is_initialized())
    {
      syslog(LOG_WARNING, "signbridge_ui: LVGL already initialized\n");
      return -EBUSY;
    }

  /* 1. Initialize LVGL core */

  lv_init();

  /* 2. Initialize NuttX display/input backend */

  lv_nuttx_dsc_init(&info);

#ifdef CONFIG_LV_USE_NUTTX_FBDEV
  info.fb_path = "/dev/fb0";
#endif

#ifdef CONFIG_LV_USE_NUTTX_TOUCHSCREEN
  info.input_path = "/dev/input0";
#endif

  lv_nuttx_init(&info, &result);

  if (result.disp == NULL)
    {
      syslog(LOG_ERR, "signbridge_ui: display init failed\n");
      lv_deinit();
      return -ENODEV;
    }

  /* 3. Set display background color */

  lv_obj_set_style_bg_color(lv_screen_active(), UI_COLOR_BG, 0);

  /* 4. Create UI elements */

  create_title(lv_screen_active());
  create_result_area(lv_screen_active());
  create_confidence_bar(lv_screen_active());
  create_preview_area(lv_screen_active());
  create_status_bar(lv_screen_active());
  create_info_bar(lv_screen_active());

  g_ui_initialized = true;
  g_last_state = SIGNBRIDGE_STATE_IDLE;

  syslog(LOG_INFO, "signbridge_ui: UI initialized (1024x600)\n");
  return OK;
}

void signbridge_ui_update(enum signbridge_state_e state,
                          const struct signbridge_result_s *result)
{
  if (!g_ui_initialized)
    {
      return;
    }

  /* Update status text if state changed */

  if (state != g_last_state)
    {
      lv_label_set_text(g_status_label, state_to_string(state));
      g_last_state = state;

      /* State-specific visual cues */

      switch (state)
        {
          case SIGNBRIDGE_STATE_IDLE:
            lv_label_set_text(g_result_label, "---");
            lv_bar_set_value(g_conf_bar, 0, LV_ANIM_OFF);
            break;

          case SIGNBRIDGE_STATE_DETECTING:
            lv_label_set_text(g_result_label, "...");
            lv_bar_set_value(g_conf_bar, 0, LV_ANIM_OFF);
            break;

          case SIGNBRIDGE_STATE_RECOGNIZING:
            lv_label_set_text(g_result_label, LV_SYMBOL_REFRESH " ...");
            break;

          case SIGNBRIDGE_STATE_RESULT:
            break;

          default:
            break;
        }
    }

  /* Update camera preview canvas with latest frame */

  {
    struct signbridge_frame_s cam_frame;

    if (signbridge_camera_get_frame(&cam_frame, 50) == 0)
      {
        blit_frame_to_canvas(cam_frame.data, cam_frame.width,
                             cam_frame.height);
        signbridge_camera_release_frame(&cam_frame);
      }
  }

  /* Update camera frame counter in info bar */

  {
    char info_buf[96];
    int mic_level = signbridge_audio_in_level();
    const char *pm_str = (signbridge_pm_level() == SIGNBRIDGE_PM_IDLE) ?
                         "IDLE" : "ACTIVE";

    snprintf(info_buf, sizeof(info_buf),
             "PM:%s MIC:%d%% Frame #%lu",
             pm_str, mic_level, (unsigned long)g_cam_frame_seq);
    lv_label_set_text(g_info_label, info_buf);
    g_cam_frame_seq++;
  }

  /* Update result display when in RESULT state */

  if (state == SIGNBRIDGE_STATE_RESULT && result != NULL)
    {
      const char *label = signbridge_vocab_get(result->class_id);
      const char *utterance = signbridge_sm_utterance_text();
      char result_buf[160];

      if (label == NULL)
        {
          label = "...";   /* No confident result for this cycle */
        }

      /* Show the word plus the assembled utterance */

      if (utterance != NULL && utterance[0] != '\0')
        {
          snprintf(result_buf, sizeof(result_buf), "%s | %s",
                   label, utterance);
        }
      else
        {
          snprintf(result_buf, sizeof(result_buf), "%s", label);
        }

      lv_label_set_text(g_result_label, result_buf);
      lv_bar_set_value(g_conf_bar, result->confidence, LV_ANIM_ON);
    }
}

uint32_t signbridge_ui_tick(void)
{
  uint32_t idle;

  if (!g_ui_initialized)
    {
      return 100;
    }

  idle = lv_timer_handler();
  return idle ? idle : 1;
}

void signbridge_ui_deinit(void)
{
  if (!g_ui_initialized)
    {
      return;
    }

  /* LVGL NuttX backend handles display/input cleanup */

  g_ui_initialized = false;
}
