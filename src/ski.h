/*
 * SkiFree game core: a port of the game logic in ski32.exe (ski2.c).
 *
 * Names follow analysis/ski32/symbols.tsv and types follow
 * analysis/ski32/types.h, so this file can be read side by side with
 * analysis/ski32/decompiled-game.c. Behaviour (including two original bugs)
 * is kept; see "Porting" in analysis/ski32/NOTES.md.
 */
#ifndef SKI_H
#define SKI_H

#include <stdint.h>

#define SKI_TICK_MS 40       /* the original's SetTimer interval */
#define SKI_MIN_WIDTH 320    /* WM_GETMINMAXINFO */
#define SKI_MIN_HEIGHT 300

/* Windows virtual-key codes, as OnKeyDown expects them. */
enum {
    SKI_VK_RETURN = 0x0d,
    SKI_VK_ESCAPE = 0x1b,
    SKI_VK_PRIOR = 0x21, /* Page Up */
    SKI_VK_NEXT = 0x22,  /* Page Down */
    SKI_VK_END = 0x23,
    SKI_VK_HOME = 0x24,
    SKI_VK_LEFT = 0x25,
    SKI_VK_UP = 0x26,
    SKI_VK_RIGHT = 0x27,
    SKI_VK_DOWN = 0x28,
    SKI_VK_INSERT = 0x2d,
    SKI_VK_NUMPAD0 = 0x60, /* NUMPAD1..9 follow */
    SKI_VK_F2 = 0x71,
    SKI_VK_F3 = 0x72,
};

/* Sound slots (WAVE resource ids in the original). */
enum {
    SND_CRASH = 1,
    SND_JUMP = 2,
    SND_DOG_HIT = 3,
    SND_LAND = 4,
    SND_BOARDER_JUMP = 5,
    SND_SKIER_HIT = 6,
    SND_YETI_EAT = 7,
    SND_WOOF = 8,
    SND_YETI_CHASE = 9,
};

typedef struct SkiRect {
    int left, top, right, bottom;
} SkiRect;

/* Status panel text (SkiStatus window), refreshed about 3 times a second
 * like the original. */
typedef struct SkiStatus {
    const char *label[4]; /* "Time:", "Dist:", "Speed:", "Style:" */
    char value[4][24];
} SkiStatus;

/* Lifecycle. ski_init does AllocGlobals, InitGameState, the window setup
 * from InitInstance/WM_CREATE/WM_SIZE, and NewGame. */
int ski_init(int clientWidth, int clientHeight);
void ski_resize(int clientWidth, int clientHeight);
void ski_set_window_active(int active); /* WM_ACTIVATE */
void ski_set_minimized(int minimized);  /* WM_SIZE SIZE_MINIMIZED */

/* Call every SKI_TICK_MS (the WM_TIMER). Runs a tick if the game is active
 * and not paused. */
void ski_timer(void);

/* Input, as the original window procedure received it. */
void ski_key_down(int vk);         /* WM_KEYDOWN */
void ski_char(int ch);             /* WM_CHAR */
void ski_mouse_move(int x, int y); /* WM_MOUSEMOVE, client coordinates */
void ski_mouse_click(void);        /* WM_LBUTTONDOWN / WM_LBUTTONDBLCLK */

/* Rendering: calls draw() for every visible sprite, back to front, with the
 * top-left corner in client coordinates. */
typedef void (*SkiDrawFn)(void *ctx, int spriteId, int x, int y);
void ski_draw(SkiDrawFn draw, void *ctx);
const SkiStatus *ski_status(void);

/* Testing. */
int ski_selftest(void);                  /* checks rand() and sprite data; 1 = ok */
void ski_seed(uint32_t seed);            /* reseed rand() after ski_init */
uint32_t ski_state_hash(void);           /* hash of every live actor */
void ski_debug_player(int *x, int *y, int *state);
void ski_debug_warp(int x, int y);      /* move the player (and the view) */

#endif
