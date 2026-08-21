/****************************************************************************
 * contest2026_408_signbridge/app/signbridge/signbridge_anim.c
 *
 * SPDX-License-Identifier: Apache-2.0
 *
 * Sign language animation player.
 *
 * Generates procedural hand animations for each recognized sign.
 * Each animation shows a hand performing the sign gesture using
 * simple geometric shapes (circles for joints, lines for bones).
 *
 * The animations are rendered into a LVGL canvas widget in the
 * preview area of the UI.
 *
 ****************************************************************************/

/****************************************************************************
 * Included Files
 ****************************************************************************/

#include <nuttx/config.h>

#include <math.h>
#include <stdlib.h>
#include <string.h>
#include <syslog.h>

#include <lvgl/lvgl.h>

#include "signbridge.h"
#include "signbridge_anim.h"

/****************************************************************************
 * Pre-processor Definitions
 ****************************************************************************/

/* Animation canvas size (fits in the preview area) */

#define ANIM_W      320
#define ANIM_H      320

/* Hand rendering parameters */

#define HAND_COLOR      0xF7BEu
#define BONE_COLOR      0xDEFBu
#define JOINT_COLOR      0xF800u
#define BG_COLOR        0x0841u

#define JOINT_RADIUS    4
#define BONE_WIDTH      3

#define ANIM_FPS        15
#define ANIM_FRAME_MS   (1000 / ANIM_FPS)

/****************************************************************************
 * Private Types
 ****************************************************************************/

/* 2D point */

struct point_s
{
  float x;
  float y;
};

/* Hand pose: 21 keypoints in normalized [0,1] coordinates */

struct hand_pose_s
{
  struct point_s kp[21];
};

/****************************************************************************
 * Private Data
 ****************************************************************************/

static uint16_t g_anim_buf[ANIM_W * ANIM_H];
static lv_obj_t *g_anim_canvas;
static bool g_anim_playing;
static int g_anim_frame;
static int g_anim_total_frames;
static uint32_t g_anim_last_tick;

/* Current animation keyframes (interpolated between poses) */

static struct hand_pose_s g_pose_current;

/* Vocabulary animation definitions */

static const char *g_sign_names[] =
{
  [0] = "hello",    [1] = "thank you", [2] = "yes",    [3] = "no",
  [4] = "please",   [5] = "sorry",     [6] = "help",   [7] = "water",
  [8] = "food",     [9] = "stop",      [10] = "go",    [11] = "good",
  [12] = "bad",     [13] = "love",     [14] = "friend",
};

#define SIGN_NAMES_COUNT (sizeof(g_sign_names) / sizeof(g_sign_names[0]))

/****************************************************************************
 * Private Functions: Procedural Pose Generation
 ****************************************************************************/

static void generate_open_hand(struct hand_pose_s *pose)
{
  /* Open hand: fingers spread, palm facing forward */

  /* Wrist */

  pose->kp[0].x = 0.50f; pose->kp[0].y = 0.90f;

  /* Thumb */

  pose->kp[1].x = 0.38f; pose->kp[1].y = 0.75f;
  pose->kp[2].x = 0.28f; pose->kp[2].y = 0.60f;
  pose->kp[3].x = 0.22f; pose->kp[3].y = 0.50f;
  pose->kp[4].x = 0.18f; pose->kp[4].y = 0.42f;

  /* Index */

  pose->kp[5].x = 0.42f; pose->kp[5].y = 0.65f;
  pose->kp[6].x = 0.42f; pose->kp[6].y = 0.48f;
  pose->kp[7].x = 0.42f; pose->kp[7].y = 0.35f;
  pose->kp[8].x = 0.42f; pose->kp[8].y = 0.25f;

  /* Middle */

  pose->kp[9].x  = 0.50f; pose->kp[9].y  = 0.63f;
  pose->kp[10].x = 0.50f; pose->kp[10].y = 0.44f;
  pose->kp[11].x = 0.50f; pose->kp[11].y = 0.30f;
  pose->kp[12].x = 0.50f; pose->kp[12].y = 0.18f;

  /* Ring */

  pose->kp[13].x = 0.58f; pose->kp[13].y = 0.65f;
  pose->kp[14].x = 0.58f; pose->kp[14].y = 0.48f;
  pose->kp[15].x = 0.58f; pose->kp[15].y = 0.36f;
  pose->kp[16].x = 0.58f; pose->kp[16].y = 0.28f;

  /* Pinky */

  pose->kp[17].x = 0.65f; pose->kp[17].y = 0.68f;
  pose->kp[18].x = 0.65f; pose->kp[18].y = 0.55f;
  pose->kp[19].x = 0.65f; pose->kp[19].y = 0.46f;
  pose->kp[20].x = 0.65f; pose->kp[20].y = 0.40f;
}

static void generate_fist(struct hand_pose_s *pose)
{
  /* Closed fist: all fingers curled */

  pose->kp[0].x = 0.50f; pose->kp[0].y = 0.85f;

  /* Thumb curled across palm */

  pose->kp[1].x = 0.40f; pose->kp[1].y = 0.72f;
  pose->kp[2].x = 0.45f; pose->kp[2].y = 0.65f;
  pose->kp[3].x = 0.50f; pose->kp[3].y = 0.60f;
  pose->kp[4].x = 0.52f; pose->kp[4].y = 0.58f;

  /* Index curled */

  pose->kp[5].x = 0.43f; pose->kp[5].y = 0.65f;
  pose->kp[6].x = 0.45f; pose->kp[6].y = 0.58f;
  pose->kp[7].x = 0.47f; pose->kp[7].y = 0.55f;
  pose->kp[8].x = 0.48f; pose->kp[8].y = 0.56f;

  /* Middle curled */

  pose->kp[9].x  = 0.50f; pose->kp[9].y  = 0.63f;
  pose->kp[10].x = 0.50f; pose->kp[10].y = 0.56f;
  pose->kp[11].x = 0.50f; pose->kp[11].y = 0.53f;
  pose->kp[12].x = 0.50f; pose->kp[12].y = 0.54f;

  /* Ring curled */

  pose->kp[13].x = 0.57f; pose->kp[13].y = 0.65f;
  pose->kp[14].x = 0.56f; pose->kp[14].y = 0.58f;
  pose->kp[15].x = 0.55f; pose->kp[15].y = 0.55f;
  pose->kp[16].x = 0.54f; pose->kp[16].y = 0.56f;

  /* Pinky curled */

  pose->kp[17].x = 0.62f; pose->kp[17].y = 0.68f;
  pose->kp[18].x = 0.60f; pose->kp[18].y = 0.62f;
  pose->kp[19].x = 0.58f; pose->kp[19].y = 0.58f;
  pose->kp[20].x = 0.57f; pose->kp[20].y = 0.57f;
}

static void generate_wave(struct hand_pose_s *pose, float phase)
{
  /* Waving hand: open hand with lateral oscillation */

  generate_open_hand(pose);

  float offset = sinf(phase * 6.2832f) * 0.08f;
  for (int i = 0; i < 21; i++)
    {
      pose->kp[i].x += offset;
    }
}

static void generate_point_up(struct hand_pose_s *pose)
{
  /* Point up: index finger extended, others curled */

  generate_fist(pose);

  /* Extend index finger */

  pose->kp[5].x = 0.45f; pose->kp[5].y = 0.60f;
  pose->kp[6].x = 0.44f; pose->kp[6].y = 0.42f;
  pose->kp[7].x = 0.43f; pose->kp[7].y = 0.28f;
  pose->kp[8].x = 0.42f; pose->kp[8].y = 0.15f;
}

static void generate_thumbs_up(struct hand_pose_s *pose)
{
  /* Thumbs up: thumb extended upward, fist */

  generate_fist(pose);

  /* Extend thumb upward */

  pose->kp[1].x = 0.45f; pose->kp[1].y = 0.70f;
  pose->kp[2].x = 0.44f; pose->kp[2].y = 0.55f;
  pose->kp[3].x = 0.43f; pose->kp[3].y = 0.42f;
  pose->kp[4].x = 0.42f; pose->kp[4].y = 0.30f;
}

/****************************************************************************
 * Private Functions: Pose Interpolation
 ****************************************************************************/

static void interpolate_pose(const struct hand_pose_s *a,
                             const struct hand_pose_s *b,
                             float t,
                             struct hand_pose_s *out)
{
  int i;
  for (i = 0; i < 21; i++)
    {
      out->kp[i].x = a->kp[i].x + (b->kp[i].x - a->kp[i].x) * t;
      out->kp[i].y = a->kp[i].y + (b->kp[i].y - a->kp[i].y) * t;
    }
}

/****************************************************************************
 * Private Functions: Rendering
 ****************************************************************************/

static void draw_circle(uint16_t *buf, int cx, int cy, int r,
                        uint16_t color)
{
  int x, y;
  for (y = cy - r; y <= cy + r; y++)
    {
      for (x = cx - r; x <= cx + r; x++)
        {
          if (x >= 0 && x < ANIM_W && y >= 0 && y < ANIM_H)
            {
              int dx = x - cx;
              int dy = y - cy;
              if (dx * dx + dy * dy <= r * r)
                {
                  buf[y * ANIM_W + x] = color;
                }
            }
        }
    }
}

static void draw_line(uint16_t *buf, int x0, int y0, int x1, int y1,
                      uint16_t color, int width)
{
  /* Bresenham's line algorithm with width */

  int dx = abs(x1 - x0);
  int dy = abs(y1 - y0);
  int sx = x0 < x1 ? 1 : -1;
  int sy = y0 < y1 ? 1 : -1;
  int err = dx - dy;
  int hw = width / 2;

  for (;;)
    {
      int wx, wy;
      for (wy = -hw; wy <= hw; wy++)
        {
          for (wx = -hw; wx <= hw; wx++)
            {
              int px = x0 + wx;
              int py = y0 + wy;
              if (px >= 0 && px < ANIM_W && py >= 0 && py < ANIM_H)
                {
                  buf[py * ANIM_W + px] = color;
                }
            }
        }

      if (x0 == x1 && y0 == y1)
        {
          break;
        }

      int e2 = 2 * err;
      if (e2 > -dy)
        {
          err -= dy;
          x0 += sx;
        }
      if (e2 < dx)
        {
          err += dx;
          y0 += sy;
        }
    }
}

static void render_hand(uint16_t *buf, const struct hand_pose_s *pose)
{
  int i;
  int sx, sy;

  /* Draw bones (connections between joints) */

  static const int bone_pairs[][2] =
  {
    {0,1}, {1,2}, {2,3}, {3,4},       /* Thumb */
    {0,5}, {5,6}, {6,7}, {7,8},       /* Index */
    {0,9}, {9,10}, {10,11}, {11,12},  /* Middle */
    {0,13}, {13,14}, {14,15}, {15,16},/* Ring */
    {0,17}, {17,18}, {18,19}, {19,20},/* Pinky */
    {5,9}, {9,13}, {13,17},           /* Palm */
  };

  for (i = 0; i < (int)(sizeof(bone_pairs) / sizeof(bone_pairs[0])); i++)
    {
      int a = bone_pairs[i][0];
      int b = bone_pairs[i][1];
      sx = (int)(pose->kp[a].x * ANIM_W);
      sy = (int)(pose->kp[a].y * ANIM_H);
      int ex = (int)(pose->kp[b].x * ANIM_W);
      int ey = (int)(pose->kp[b].y * ANIM_H);
      draw_line(buf, sx, sy, ex, ey, BONE_COLOR, BONE_WIDTH);
    }

  /* Draw joints */

  for (i = 0; i < 21; i++)
    {
      sx = (int)(pose->kp[i].x * ANIM_W);
      sy = (int)(pose->kp[i].y * ANIM_H);
      uint16_t jc = (i == 0 || i == 4 || i == 8 || i == 12 || i == 16 || i == 20)
                      ? 0xF800 : 0xF7BE;
      draw_circle(buf, sx, sy, JOINT_RADIUS, jc);
    }
}

/****************************************************************************
 * Private Functions: Animation State Machine
 ****************************************************************************/

static void get_pose_for_sign(int class_id, float progress,
                              struct hand_pose_s *out)
{
  /* Generate animation based on sign class */

  struct hand_pose_s p1, p2;

  switch (class_id % 15)
    {
      case 0: /* hello - wave */
        generate_wave(&p1, progress);
        *out = p1;
        return;

      case 1: /* thank you - open→fist */
        generate_open_hand(&p1);
        generate_fist(&p2);
        break;

      case 2: /* yes - thumbs up */
        generate_thumbs_up(out);
        return;

      case 3: /* no - fist shake */
        generate_fist(&p1);
        generate_fist(&p2);
        for (int i = 0; i < 21; i++)
          {
            p2.kp[i].x += sinf(progress * 12.0f) * 0.05f;
          }
        interpolate_pose(&p1, &p2, 0.5f, out);
        return;

      case 4: /* please - open hand circular */
        generate_open_hand(&p1);
        for (int i = 0; i < 21; i++)
          {
            float cx = 0.50f;
            float cy = 0.55f;
            float dx = p1.kp[i].x - cx;
            float dy = p1.kp[i].y - cy;
            float angle = progress * 6.2832f;
            p1.kp[i].x = cx + dx * cosf(angle) - dy * sinf(angle);
            p1.kp[i].y = cy + dx * sinf(angle) + dy * cosf(angle);
          }
        *out = p1;
        return;

      case 5: /* sorry - fist to chest */
        generate_fist(&p1);
        for (int i = 0; i < 21; i++)
          {
            p1.kp[i].y -= progress * 0.15f;
          }
        *out = p1;
        return;

      case 6: /* help - point up */
        generate_point_up(out);
        return;

      case 13: /* love - thumb+pink extended */
        generate_fist(&p1);
        p1.kp[1].x = 0.38f; p1.kp[1].y = 0.60f;
        p1.kp[2].x = 0.30f; p1.kp[2].y = 0.48f;
        p1.kp[3].x = 0.25f; p1.kp[3].y = 0.38f;
        p1.kp[4].x = 0.22f; p1.kp[4].y = 0.30f;
        p1.kp[17].x = 0.65f; p1.kp[17].y = 0.62f;
        p1.kp[18].x = 0.68f; p1.kp[18].y = 0.48f;
        p1.kp[19].x = 0.70f; p1.kp[19].y = 0.38f;
        p1.kp[20].x = 0.72f; p1.kp[20].y = 0.30f;
        *out = p1;
        return;

      default: /* Generic: open→fist→open */
        generate_open_hand(&p1);
        generate_fist(&p2);
        break;
    }

  /* Ping-pong interpolation */

  float t = (progress < 0.5f)
            ? (progress * 2.0f)
            : (2.0f - progress * 2.0f);
  interpolate_pose(&p1, &p2, t, out);
}

/****************************************************************************
 * Public Functions
 ****************************************************************************/

int signbridge_anim_init(void)
{
  g_anim_playing = false;
  g_anim_frame = 0;
  g_anim_last_tick = 0;

  syslog(LOG_INFO, "anim: animation player ready (%dx%d)\n", ANIM_W, ANIM_H);
  return OK;
}

void signbridge_anim_set_canvas(lv_obj_t *canvas)
{
  g_anim_canvas = canvas;
  if (canvas != NULL)
    {
      lv_canvas_set_buffer(canvas, (lv_color_t *)g_anim_buf, ANIM_W, ANIM_H,
                           LV_COLOR_FORMAT_RGB565);
      lv_canvas_fill_bg(canvas, lv_color_hex(BG_COLOR), LV_OPA_COVER);
    }
}

int signbridge_anim_play(int class_id)
{
  if (class_id < 0)
    {
      return -EINVAL;
    }

  g_anim_playing = true;
  g_anim_frame = 0;
  g_anim_total_frames = ANIM_FPS * 2; /* 2 seconds per animation */
  g_anim_last_tick = 0;

  syslog(LOG_INFO, "anim: playing sign '%s' (class %d)\n",
         class_id < (int)SIGN_NAMES_COUNT ? g_sign_names[class_id] : "???",
         class_id);

  return OK;
}

bool signbridge_anim_is_playing(void)
{
  return g_anim_playing;
}

void signbridge_anim_stop(void)
{
  g_anim_playing = false;
}

void signbridge_anim_tick(void)
{
  if (!g_anim_playing || g_anim_canvas == NULL)
    {
      return;
    }

  float progress = (float)g_anim_frame / (float)g_anim_total_frames;

  /* Clear canvas */

  memset(g_anim_buf, 0, sizeof(g_anim_buf));

  /* Generate and render current pose */

  get_pose_for_sign(0, progress, &g_pose_current);
  render_hand(g_anim_buf, &g_pose_current);

  /* Invalidate canvas to trigger LVGL redraw */

  lv_obj_invalidate(g_anim_canvas);

  g_anim_frame++;
  if (g_anim_frame >= g_anim_total_frames)
    {
      g_anim_playing = false;
      syslog(LOG_INFO, "anim: animation complete\n");
    }
}
