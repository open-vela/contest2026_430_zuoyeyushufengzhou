/****************************************************************************
 * contest2026_408_signbridge/app/signbridge/signbridge_pm.c
 *
 * SPDX-License-Identifier: Apache-2.0
 *
 * Power management module (see header).
 *
 ****************************************************************************/

/****************************************************************************
 * Included Files
 ****************************************************************************/

#include <nuttx/config.h>

#include <errno.h>
#include <syslog.h>
#include <time.h>

#include <nuttx/arch.h>

#include "signbridge_pm.h"

/****************************************************************************
 * Private Data
 ****************************************************************************/

static enum signbridge_pm_level_e g_level = SIGNBRIDGE_PM_ACTIVE;
static uint32_t g_idle_ms;

/****************************************************************************
 * Private Functions
 ****************************************************************************/

#ifdef CONFIG_ESP32P4_FUNCTION_EV_LCD
/* Board backlight control (esp32p4_display.c).  Declared weak so the
 * module also links on targets without a board backlight (e.g. sim).
 */

extern int funev_lcd_backlight(bool on) __attribute__((weak));

static void signbridge_pm_backlight(bool on)
{
  if (funev_lcd_backlight != NULL)
    {
      funev_lcd_backlight(on);
    }
}
#else
static void signbridge_pm_backlight(bool on)
{
  UNUSED(on);
}
#endif

/****************************************************************************
 * Name: signbridge_pm_now_ms
 *
 * Description:
 *   Monotonic milliseconds (tick based).
 *
 ****************************************************************************/

static uint32_t signbridge_pm_now_ms(void)
{
  return (uint32_t)((uint64_t)clock() * 1000 / CLOCKS_PER_SEC);
}

/****************************************************************************
 * Public Functions
 ****************************************************************************/

/****************************************************************************
 * Name: signbridge_pm_init
 ****************************************************************************/

int signbridge_pm_init(void)
{
  g_level = SIGNBRIDGE_PM_ACTIVE;
  g_idle_ms = 0;
  return OK;
}

/****************************************************************************
 * Name: signbridge_pm_step
 ****************************************************************************/

void signbridge_pm_step(void)
{
  static uint32_t last_tick_ms;
  uint32_t now_ms = signbridge_pm_now_ms();
  uint32_t delta;

  /* Guard against first call / clock wraparound */

  if (last_tick_ms == 0)
    {
      last_tick_ms = now_ms;
      return;
    }

  delta = now_ms - last_tick_ms;
  last_tick_ms = now_ms;

  if (delta > 1000)
    {
      delta = 1000;   /* clamp spurious gaps */
    }

  g_idle_ms += delta;

  /* Active -> idle: screen off after the idle timeout */

  if (g_level == SIGNBRIDGE_PM_ACTIVE &&
      g_idle_ms >= SIGNBRIDGE_PM_SCREENOFF_MS)
    {
      g_level = SIGNBRIDGE_PM_IDLE;
      syslog(LOG_INFO, "pm: idle - screen off\n");

      signbridge_pm_backlight(false);

      /* TODO(real hw): CPU frequency scaling - the esp-clk HAL is not
       * wired into this NuttX port yet (no esp_set_cpu_freq API).
       *
       * TODO(real hw): light sleep - only timer wakeup is available
       * until the GT911 INT is wired (BSP_LCD_TOUCH_INT = NC), so it
       * is intentionally not enabled automatically.
       */
    }
}

/****************************************************************************
 * Name: signbridge_pm_activity
 ****************************************************************************/

void signbridge_pm_activity(void)
{
  g_idle_ms = 0;

  if (g_level == SIGNBRIDGE_PM_IDLE)
    {
      g_level = SIGNBRIDGE_PM_ACTIVE;
      syslog(LOG_INFO, "pm: active - screen on\n");

      signbridge_pm_backlight(true);
    }
}

/****************************************************************************
 * Name: signbridge_pm_level
 ****************************************************************************/

enum signbridge_pm_level_e signbridge_pm_level(void)
{
  return g_level;
}
