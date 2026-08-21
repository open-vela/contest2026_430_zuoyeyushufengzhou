/****************************************************************************
 * contest2026_408_signbridge/app/signbridge/signbridge_ui.h
 *
 * SPDX-License-Identifier: Apache-2.0
 *
 * LVGL user interface for the sign language recognition terminal.
 *
 ****************************************************************************/

#ifndef __APP_SIGNBRIDGE_SIGNBRIDGE_UI_H
#define __APP_SIGNBRIDGE_SIGNBRIDGE_UI_H

/****************************************************************************
 * Included Files
 ****************************************************************************/

#include <nuttx/config.h>
#include <stdbool.h>

#include "signbridge.h"

/****************************************************************************
 * Public Function Prototypes
 ****************************************************************************/

#ifdef __cplusplus
extern "C"
{
#endif

/****************************************************************************
 * Name: signbridge_ui_init
 *
 * Description:
 *   Initialize LVGL and create the signbridge UI screens.
 *   Must be called before signbridge_ui_update().
 *
 * Returned Value:
 *   0 on success, negative errno on failure.
 *
 ****************************************************************************/

int signbridge_ui_init(void);

/****************************************************************************
 * Name: signbridge_ui_update
 *
 * Description:
 *   Update the UI to reflect the current application state.
 *   Called from the main loop after each sm_step().
 *
 * Input Parameters:
 *   state  - Current state machine state
 *   result - Latest recognition result (may be NULL if no result yet)
 *
 ****************************************************************************/

void signbridge_ui_update(enum signbridge_state_e state,
                          const struct signbridge_result_s *result);

/****************************************************************************
 * Name: signbridge_ui_tick
 *
 * Description:
 *   Process LVGL timer events.  Must be called frequently from the
 *   main loop (every 5-33 ms depending on LV_DISP_DEF_REFR_PERIOD).
 *
 * Returned Value:
 *   Suggested sleep time in milliseconds before next call.
 *
 ****************************************************************************/

uint32_t signbridge_ui_tick(void);

/****************************************************************************
 * Name: signbridge_ui_deinit
 *
 * Description:
 *   Destroy UI and release LVGL resources.
 *
 ****************************************************************************/

void signbridge_ui_deinit(void);

#ifdef __cplusplus
}
#endif

#endif /* __APP_SIGNBRIDGE_SIGNBRIDGE_UI_H */
