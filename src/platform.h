/*
 * What the game core needs from the host. Implemented by each frontend
 * (main_sdl.c for now). These replace the Win32 calls in the original.
 */
#ifndef SKI_PLATFORM_H
#define SKI_PLATFORM_H

#include <stdint.h>

/* Milliseconds since some fixed point (GetTickCount). */
uint32_t plat_ticks(void);

/* Main window title (SetWindowTextA). */
void plat_set_title(const char *title);

/* Esc: minimize the window (ShowWindow(SW_MINIMIZE)). */
void plat_minimize(void);

/* High-score storage: a string per key (the [Ski] section of entpack.ini).
 * plat_load_scores writes "" if the key is missing. */
void plat_load_scores(const char *key, char *buf, int size);
void plat_save_scores(const char *key, const char *value);

/* Show the high-score list (MessageBoxA titled "High Scores"). */
void plat_show_scores(const char *title, const char *text);

/* Sound slot 1-9 (SND_*). The original's WAVE resources don't exist, so
 * frontends may ignore this or supply their own sounds. */
void plat_play_sound(int slot);

#endif
