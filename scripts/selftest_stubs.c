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

/* Recorded menu-mouse messages (WM_MOUSEMOVE / WM_LBUTTONDOWN / UP). */
int g_mm_n;
uint32_t g_mm_msg[96], g_mm_wp[96], g_mm_lp[96];

void user32_mouse_msg(uint32_t msg, uint32_t wp, uint32_t lp) {
    if (g_mm_n < 96) {
        g_mm_msg[g_mm_n] = msg;
        g_mm_wp[g_mm_n] = wp;
        g_mm_lp[g_mm_n] = lp;
    }
    g_mm_n++;
}

void rt_fatal(const char *fmt, ...) { (void)fmt; abort(); }
