/****************************************************************************
 * app/signbridge/signbridge_main.c
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

/* SignBridge main entry point.
 *
 * Initialises the state machine, LVGL user interface, and inference
 * pipeline, then enters the main event loop which alternates between:
 *   - state machine stepping (camera / inference / result posting)
 *   - LVGL timer processing (display refresh)
 */

/****************************************************************************
 * Included Files
 ****************************************************************************/

#include <nuttx/config.h>

#include <stdio.h>
#include <syslog.h>
#include <unistd.h>

#include "signbridge.h"
#include "signbridge_ui.h"
#include "signbridge_infer.h"

/****************************************************************************
 * Public Functions
 ****************************************************************************/

int signbridge_main(int argc, char *argv[])
{
  uint32_t sleep_ms;
  int ret;

  syslog(LOG_INFO, "signbridge: starting on-device sign language terminal\n");

  /* 1. Initialize state machine (also calls signbridge_infer_init) */

  ret = signbridge_sm_init();
  if (ret < 0)
    {
      syslog(LOG_ERR, "signbridge: sm_init failed: %d\n", ret);
      return ret;
    }

  /* 2. Initialize LVGL UI (requires CONFIG_GRAPHICS_LVGL) */

#ifdef CONFIG_GRAPHICS_LVGL
  ret = signbridge_ui_init();
  if (ret < 0)
    {
      syslog(LOG_ERR, "signbridge: ui_init failed: %d\n", ret);
      /* Continue without UI - serial output still works */
    }
#endif

  /* 3. Main event loop */

  for (; ; )
    {
      /* State machine: advance detection / recognition logic */

      signbridge_sm_step();

      /* UI: refresh display, process touch input */

#ifdef CONFIG_GRAPHICS_LVGL
      signbridge_ui_update(signbridge_sm_state(),
                           signbridge_sm_last_result());
      sleep_ms = signbridge_ui_tick();
#else
      sleep_ms = 50;
#endif

      /* Sleep for the shorter of UI-requested time or 50 ms */

      if (sleep_ms > 50)
        {
          sleep_ms = 50;
        }

      usleep(sleep_ms * 1000);
    }

  /* Unreachable in normal operation */

#ifdef CONFIG_GRAPHICS_LVGL
  signbridge_ui_deinit();
#endif
  signbridge_infer_deinit();

  return 0;
}
