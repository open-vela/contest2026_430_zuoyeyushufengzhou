/****************************************************************************
 * contest2026_408_signbridge/app/signbridge/signbridge_anim.h
 *
 * SPDX-License-Identifier: Apache-2.0
 *
 * Sign language animation player for LVGL.
 * Plays pre-rendered animation sequences on the display.
 *
 ****************************************************************************/

#ifndef __APP_SIGNBRIDGE_SIGNBRIDGE_ANIM_H
#define __APP_SIGNBRIDGE_SIGNBRIDGE_ANIM_H

#include <nuttx/config.h>
#include <stdint.h>
#include <stdbool.h>

/* Animation frame descriptor */

struct signbridge_anim_frame_s
{
  const uint16_t *pixels;   /* RGB565 pixel data */
  uint16_t width;
  uint16_t height;
  uint16_t duration_ms;     /* Display duration */
};

/* Animation descriptor */

struct signbridge_anim_s
{
  const char *name;
  const struct signbridge_anim_frame_s *frames;
  uint16_t frame_count;
  uint16_t total_duration_ms;
};

/* API */

int  signbridge_anim_init(void);
int  signbridge_anim_play(int class_id);
bool signbridge_anim_is_playing(void);
void signbridge_anim_stop(void);
void signbridge_anim_tick(void);

#endif /* __APP_SIGNBRIDGE_SIGNBRIDGE_ANIM_H */
