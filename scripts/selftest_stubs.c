/* Minimal host stubs for the touch overlay selftest (TOUCH_SELFTEST build):
 * the pure overlay logic + vinput run against these instead of the real
 * user32.c / rt.c. user32_key_event records every WM it would have posted so
 * the selftest can assert on them. */
#include <stdint.h>
#include <stdlib.h>

int g_log_level = 1;
char g_save_dir[1024] = "/tmp";

int g_wm_n;
uint32_t g_wm_vk[64], g_wm_sc[64];
int g_wm_down[64];

void user32_key_event(uint32_t vk, uint32_t sc, int down) {
    if (g_wm_n < 64) {
        g_wm_vk[g_wm_n] = vk;
        g_wm_sc[g_wm_n] = sc;
        g_wm_down[g_wm_n] = down;
    }
    g_wm_n++;
}

void rt_fatal(const char *fmt, ...) { (void)fmt; abort(); }
