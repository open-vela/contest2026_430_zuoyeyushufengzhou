/****************************************************************************
 * contest2026_408_signbridge/app/signbridge/signbridge_ui.c
 *
 * SPDX-License-Identifier: Apache-2.0
 *
 * LVGL user interface for the SignBridge sign language recognition
 * terminal on the ESP32-P4-Function-EV-Board (7" 800x1280 display).
 *
 * UI layout (portrait 800x1280):
 *
 *   ┌─────────────────────────────────────┐
 *   │         SIGNBRIDGE TITLE             │ y=0..60
 *   ├─────────────────────────────────────┤
 *   │                                     │
 *   │         SIGN RESULT TEXT             │ y=60..200
 *   │         (large, centered)            │
 *   │                                     │
 *   ├─────────────────────────────────────┤
 *   │         [confidence bar]             │ y=200..230
 *   ├─────────────────────────────────────┤
 *   │                                     │
 *   │                                     │
 *   │       CAMERA PREVIEW AREA           │ y=230..930
 *   │       (700x700 placeholder)          │
 *   │                                     │
 *   │                                     │
 *   ├─────────────────────────────────────┤
 *   │     STATUS: IDLE / DETECTING / ...   │ y=930..970
 *   ├─────────────────────────────────────┤
 *   │         FPS / DEBUG INFO             │ y=970..1000
 *   └─────────────────────────────────────┘
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
#include "signbridge_camera.h"
#include "signbridge_ui.h"

/****************************************************************************
 * Pre-processor Definitions
 ****************************************************************************/

/* UI layout constants (800x1280 portrait) */

#define UI_SCREEN_W         800
#define UI_SCREEN_H         1280

#define UI_TITLE_Y          0
#define UI_TITLE_H          60

#define UI_RESULT_Y         70
#define UI_RESULT_H         120

#define UI_BAR_Y            200
#define UI_BAR_H            25

#define UI_PREVIEW_Y        240
#define UI_PREVIEW_H        680

#define UI_STATUS_Y         930
#define UI_STATUS_H         40

#define UI_INFO_Y           975
#define UI_INFO_H           30

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
static lv_color_t g_canvas_buf[480 * 240];

static enum signbridge_state_e g_last_state = SIGNBRIDGE_STATE_IDLE;
static uint32_t g_cam_frame_seq;
static bool g_ui_initialized;

/* Vocabulary table (stubs - populated during training) */

static const char *g_vocab_table[] =
{
  [0]  = "hello",
  [1]  = "thank you",
  [2]  = "yes",
  [3]  = "no",
  [4]  = "please",
  [5]  = "sorry",
  [6]  = "help",
  [7]  = "water",
  [8]  = "food",
  [9]  = "stop",
  [10] = "go",
  [11] = "good",
  [12] = "bad",
  [13] = "love",
  [14] = "friend",
};

#define VOCAB_TABLE_SIZE \
  (sizeof(g_vocab_table) / sizeof(g_vocab_table[0]))

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
  lv_obj_set_width(g_result_label, UI_SCREEN_W - 40);
  lv_obj_align(g_result_label, LV_ALIGN_TOP_MID, 0, UI_RESULT_Y);
}

static void create_confidence_bar(lv_obj_t *parent)
{
  g_conf_bar = lv_bar_create(parent);
  lv_bar_set_range(g_conf_bar, 0, 100);
  lv_bar_set_value(g_conf_bar, 0, LV_ANIM_OFF);
  lv_obj_set_size(g_conf_bar, UI_SCREEN_W - 80, UI_BAR_H);
  lv_obj_align(g_conf_bar, LV_ALIGN_TOP_MID, 0, UI_BAR_Y);
  lv_obj_set_style_bg_color(g_conf_bar, UI_COLOR_BAR_BG, 0);
  lv_obj_set_style_bg_color(g_conf_bar, UI_COLOR_BAR_IND,
                             LV_PART_INDICATOR);
}

static void create_preview_area(lv_obj_t *parent)
{
  g_preview_panel = lv_obj_create(parent);
  lv_obj_set_size(g_preview_panel, UI_SCREEN_W - 40, UI_PREVIEW_H);
  lv_obj_align(g_preview_panel, LV_ALIGN_TOP_MID, 0, UI_PREVIEW_Y);
  lv_obj_set_style_bg_color(g_preview_panel, UI_COLOR_PREVIEW, 0);
  lv_obj_set_style_bg_opa(g_preview_panel, LV_OPA_COVER, 0);
  lv_obj_set_style_border_width(g_preview_panel, 2, 0);
  lv_obj_set_style_border_color(g_preview_panel,
                                lv_color_hex(0x333355), 0);
  lv_obj_set_style_radius(g_preview_panel, 12, 0);

  /* Camera preview canvas (reduced resolution 480x240) */

  g_preview_canvas = lv_canvas_create(g_preview_panel);
  lv_canvas_set_buffer(g_preview_canvas, g_canvas_buf, 480, 240, LV_COLOR_FORMAT_RGB565);
  lv_obj_center(g_preview_canvas);
  lv_canvas_fill_bg(g_preview_canvas, lv_color_hex(0x0f0f23), LV_OPA_COVER);

  /* Placeholder text (hidden when camera starts) */

  /* Placeholder text inside preview area */

  lv_obj_t *placeholder = lv_label_create(g_preview_panel);
  lv_label_set_text(placeholder,
                    LV_SYMBOL_IMAGE "\n"
                    "Camera Preview\n"
                    "(800x700 area)\n\n"
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
  lv_obj_align(g_status_label, LV_ALIGN_TOP_MID, 0, UI_STATUS_Y);
}

static void create_info_bar(lv_obj_t *parent)
{
  g_info_label = lv_label_create(parent);
  lv_label_set_text(g_info_label,
                    "ESP32-P4 | openvela | Contest #408");
  lv_obj_set_style_text_color(g_info_label, lv_color_hex(0x444466), 0);
  lv_obj_set_style_text_font(g_info_label, &lv_font_montserrat_14, 0);
  lv_obj_align(g_info_label, LV_ALIGN_TOP_MID, 0, UI_INFO_Y);
}

/****************************************************************************
 * Name: blit_frame_to_canvas
 *
 * Description:
 *   Blit a RGB565 camera frame (640x480) to the LVGL canvas (480x240)
 *   using simple nearest-neighbor downscaling.
 *
 ****************************************************************************/

static void blit_frame_to_canvas(const uint8_t *frame_data,
                                 uint16_t frame_w, uint16_t frame_h)
{
  if (g_preview_canvas == NULL || frame_data == NULL)
    {
      return;
    }

  const uint16_t *src = (const uint16_t *)frame_data;
  lv_color_t *dst = g_canvas_buf;
  int canvas_w = 480;
  int canvas_h = 240;
  int x, y;

  for (y = 0; y < canvas_h; y++)
    {
      int src_y = (y * frame_h) / canvas_h;
      for (x = 0; x < canvas_w; x++)
        {
          int src_x = (x * frame_w) / canvas_w;
          uint16_t pixel = src[src_y * frame_w + src_x];

          /* RGB565 → LVGL color */

          dst[y * canvas_w + x].blue  = (pixel & 0x1F) << 3;
          dst[y * canvas_w + x].green = ((pixel >> 5) & 0x3F) << 2;
          dst[y * canvas_w + x].red   = ((pixel >> 11) & 0x1F) << 3;
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

  syslog(LOG_INFO, "signbridge_ui: UI initialized (800x1280)\n");
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
    extern int signbridge_camera_get_frame(struct signbridge_frame_s *f, uint32_t t);
    extern void signbridge_camera_release_frame(struct signbridge_frame_s *f);
    struct signbridge_frame_s cam_frame;
    if (signbridge_camera_get_frame(&cam_frame, 50) == 0)
      {
        blit_frame_to_canvas(cam_frame.data, cam_frame.width, cam_frame.height);
        signbridge_camera_release_frame(&cam_frame);
      }
  }

  /* Update camera frame counter in info bar */

  {
    char info_buf[64];
    snprintf(info_buf, sizeof(info_buf),
             "ESP32-P4 | Frame #%lu | Contest #408",
             (unsigned long)g_cam_frame_seq);
    lv_label_set_text(g_info_label, info_buf);
    g_cam_frame_seq++;
  }

  /* Update result display when in RESULT state */

  if (state == SIGNBRIDGE_STATE_RESULT && result != NULL)
    {
      const char *label;

      if (result->class_id >= 0 &&
          result->class_id < (int)VOCAB_TABLE_SIZE)
        {
          label = g_vocab_table[result->class_id];
        }
      else
        {
          label = "???";
        }

      lv_label_set_text(g_result_label, label);
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
