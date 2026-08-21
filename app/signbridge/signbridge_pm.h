/****************************************************************************
 * contest2026_408_signbridge/app/signbridge/signbridge_pm.h
 *
 * SPDX-License-Identifier: Apache-2.0
 *
 * Power management module for SignBridge.
 *
 * Tiered strategy (per project plan):
 *   active  -> idle (screen off after timeout) -> sleep (future)
 *
 *  - Idle:  backlight off after SIGNBRIDGE_PM_SCREENOFF_MS of no
 *           activity; any activity (touch / wake word / recognition
 *           result) turns it back on.
 *  - Sleep: light-sleep hook reserved (timer wakeup only until the
 *           touch INT is wired); NOT enabled automatically until
 *           verified on real hardware.
 *  - CPU:   frequency scaling hook reserved (esp-clk HAL is not yet
 *           wired into this NuttX port).
 *
 ****************************************************************************/

#ifndef __APP_SIGNBRIDGE_SIGNBRIDGE_PM_H
#define __APP_SIGNBRIDGE_SIGNBRIDGE_PM_H

/****************************************************************************
 * Included Files
 ****************************************************************************/

#include <nuttx/config.h>
#include <stdbool.h>
#include <stdint.h>

/****************************************************************************
 * Pre-processor Definitions
 ****************************************************************************/

#define SIGNBRIDGE_PM_SCREENOFF_MS   30000   /* 30 s idle -> screen off */

/****************************************************************************
 * Public Types
 ****************************************************************************/

enum signbridge_pm_level_e
{
  SIGNBRIDGE_PM_ACTIVE = 0,   /* 活跃：屏幕亮                    */
  SIGNBRIDGE_PM_IDLE          /* 空闲：屏幕灭（等待唤醒）         */
};

#ifdef __cplusplus
extern "C"
{
#endif

/****************************************************************************
 * Public Function Prototypes
 ****************************************************************************/

/****************************************************************************
 * Name: signbridge_pm_init
 ****************************************************************************/

int signbridge_pm_init(void);

/****************************************************************************
 * Name: signbridge_pm_step
 *
 * Description:
 *   Periodic tick (call from the state machine worker, ~50 ms).
 *   Tracks the idle timer and applies the tier transitions.
 *
 ****************************************************************************/

void signbridge_pm_step(void);

/****************************************************************************
 * Name: signbridge_pm_activity
 *
 * Description:
 *   Report user activity (touch, wake word, recognition result).
 *   Resets the idle timer and wakes the screen if needed.
 *
 ****************************************************************************/

void signbridge_pm_activity(void);

/****************************************************************************
 * Name: signbridge_pm_level
 *
 * Description:
 *   Current power level (for the UI status bar).
 *
 ****************************************************************************/

enum signbridge_pm_level_e signbridge_pm_level(void);

#ifdef __cplusplus
}
#endif

#endif /* __APP_SIGNBRIDGE_SIGNBRIDGE_PM_H */
