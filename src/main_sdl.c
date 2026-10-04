/*
 * SDL2 frontend for the SkiFree port: window, input, timer, rendering, and
 * high-score storage.
 *
 *   skifree [--size WxH] [--tick-ms N]
 *   skifree --headless --ticks N [--seed S] [--script S] [--screenshot out.bmp] ...
 *
 * Headless mode (see headless.c) renders the final frame with SDL's software
 * renderer, so it needs no display.
 *
 * The same file builds for the browser with emscripten (make web): there the
 * window is the page's canvas, the browser drives frame(), and high scores
 * go to localStorage.
 */
#include <SDL.h>
#ifdef __EMSCRIPTEN__
#include <emscripten.h>
#endif
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "assets.h"
#include "font5x7.h"
#include "headless.h"
#include "platform.h"
#include "ski.h"

static SDL_Window *g_window;
static SDL_Renderer *g_renderer;
static SDL_Texture *g_sprites[SKI_NUM_SPRITES];
static int g_running = 1;
static int g_headless;
static uint32_t g_lastTimer;
static int g_tickMs = SKI_TICK_MS;
static int g_vsync;

/* ---- platform.h --------------------------------------------------------- */

uint32_t plat_ticks(void) { return g_headless ? headless_time : SDL_GetTicks(); }

void plat_set_title(const char *title)
{
    if (g_window)
        SDL_SetWindowTitle(g_window, title);
}

/* A no-op in the browser: a page can't minimize itself. */
void plat_minimize(void)
{
    if (g_window)
        SDL_MinimizeWindow(g_window);
}

void plat_play_sound(int slot) { (void)slot; }

#ifdef __EMSCRIPTEN__

/* In the browser, each key is a localStorage item ("SkiFree.SS" and so on).
 * Values are also kept in memory, so scores last for the visit when storage
 * is blocked (private windows, disabled site data). */
EM_JS(void, web_load_scores, (const char *key, char *buf, int size), {
    var k = "SkiFree." + UTF8ToString(key);
    var v = (Module.skiScores || {})[k];
    if (v == null) {
        try { v = localStorage.getItem(k); } catch (e) {}
    }
    stringToUTF8(v || "", buf, size);
});

EM_JS(void, web_save_scores, (const char *key, const char *value), {
    var k = "SkiFree." + UTF8ToString(key), v = UTF8ToString(value);
    (Module.skiScores = Module.skiScores || {})[k] = v;
    try { localStorage.setItem(k, v); } catch (e) {}
});

static void scores_path_init(void) {}

void plat_load_scores(const char *key, char *buf, int size) { web_load_scores(key, buf, size); }

void plat_save_scores(const char *key, const char *value) { web_save_scores(key, value); }

#else

static char g_scoresPath[1024];

/* Scores live in entpack.ini under the SDL pref path, as "[Ski]" and
 * "KEY=value" lines, like the original's file. */
static void scores_path_init(void)
{
    char *dir;
    const char *env = getenv("SKIFREE_SCORES");
    if (env) {
        snprintf(g_scoresPath, sizeof g_scoresPath, "%s", env);
        return;
    }
    if (g_headless) {
        snprintf(g_scoresPath, sizeof g_scoresPath, "build/port/test-entpack.ini");
        remove(g_scoresPath); /* each headless run starts with no scores */
        return;
    }
    dir = SDL_GetPrefPath("ihoc", "SkiFree");
    snprintf(g_scoresPath, sizeof g_scoresPath, "%sentpack.ini", dir ? dir : "");
    SDL_free(dir);
}

void plat_load_scores(const char *key, char *buf, int size)
{
    char line[512];
    size_t klen = strlen(key);
    FILE *f = fopen(g_scoresPath, "r");
    buf[0] = '\0';
    if (!f)
        return;
    while (fgets(line, sizeof line, f)) {
        if (strncmp(line, key, klen) == 0 && line[klen] == '=') {
            snprintf(buf, size, "%s", line + klen + 1);
            buf[strcspn(buf, "\r\n")] = '\0';
            break;
        }
    }
    fclose(f);
}

void plat_save_scores(const char *key, const char *value)
{
    static const char *keys[] = {"SS", "GS", "FS"};
    char vals[3][256];
    FILE *f;
    int i;
    for (i = 0; i < 3; i++) {
        if (strcmp(keys[i], key) == 0)
            snprintf(vals[i], sizeof vals[i], "%s", value);
        else
            plat_load_scores(keys[i], vals[i], sizeof vals[i]);
    }
    f = fopen(g_scoresPath, "w");
    if (!f)
        return;
    fprintf(f, "[Ski]\n");
    for (i = 0; i < 3; i++)
        if (vals[i][0])
            fprintf(f, "%s=%s\n", keys[i], vals[i]);
    fclose(f);
}

#endif

/* In the browser, SDL shows this with alert(), which blocks like the
 * original's MessageBox. */
void plat_show_scores(const char *title, const char *text)
{
    if (g_headless) {
        printf("%s\n%s\n", title, text);
        return;
    }
    SDL_ShowSimpleMessageBox(SDL_MESSAGEBOX_INFORMATION, title, text, g_window);
}

/* ---- Rendering ---------------------------------------------------------- */

static int load_sprites(void)
{
    int i;
    for (i = 1; i < SKI_NUM_SPRITES; i++) {
        SDL_RWops *rw = SDL_RWFromConstMem(ski_bitmaps[i].bmp, (int)ski_bitmaps[i].bmpSize);
        SDL_Surface *s = SDL_LoadBMP_RW(rw, 1);
        if (!s) {
            fprintf(stderr, "skifree: bitmap %d: %s\n", i, SDL_GetError());
            return 0;
        }
        /* The original masks out white: it's transparent everywhere. */
        SDL_SetColorKey(s, SDL_TRUE, SDL_MapRGB(s->format, 255, 255, 255));
        g_sprites[i] = SDL_CreateTextureFromSurface(g_renderer, s);
        SDL_FreeSurface(s);
        if (!g_sprites[i])
            return 0;
    }
    return 1;
}

static void draw_sprite(void *ctx, int id, int x, int y)
{
    SDL_Rect dst;
    (void)ctx;
    dst.x = x;
    dst.y = y;
    dst.w = ski_bitmaps[id].width;
    dst.h = ski_bitmaps[id].height;
    SDL_RenderCopy(g_renderer, g_sprites[id], NULL, &dst);
}

#define CELL_W (FONT_W + 1)
#define LINE_H (FONT_H + 3)

static void draw_text(int x, int y, const char *s)
{
    int r, c;
    for (; *s; s++, x += CELL_W) {
        const Glyph *g = font_glyph(*s);
        if (!g)
            continue;
        for (r = 0; r < FONT_H; r++)
            for (c = 0; c < FONT_W; c++)
                if (g->rows[r] & (0x10 >> c))
                    SDL_RenderDrawPoint(g_renderer, x + c, y + r);
    }
}

/* The SkiStatus child window: top right, framed, labels then values. */
static void draw_status(int clientWidth)
{
    const SkiStatus *st = ski_status();
    int i, labelW = 0, valueW = 0, w, h, x0;
    SDL_Rect box;
    for (i = 0; i < 4; i++) {
        int lw = (int)strlen(st->label[i]) * CELL_W;
        int vw = (int)strlen(st->value[i]) * CELL_W;
        labelW = lw > labelW ? lw : labelW;
        valueW = vw > valueW ? vw : valueW;
    }
    labelW += CELL_W;
    w = labelW + valueW + 4;
    h = 4 * LINE_H + 4;
    x0 = clientWidth - w;
    box.x = x0;
    box.y = 0;
    box.w = w;
    box.h = h;
    SDL_SetRenderDrawColor(g_renderer, 255, 255, 255, 255);
    SDL_RenderFillRect(g_renderer, &box);
    SDL_SetRenderDrawColor(g_renderer, 0, 0, 0, 255);
    SDL_RenderDrawRect(g_renderer, &box);
    for (i = 0; i < 4; i++) {
        draw_text(x0 + 3, 3 + i * LINE_H, st->label[i]);
        draw_text(x0 + 3 + labelW, 3 + i * LINE_H, st->value[i]);
    }
}

static void render(int width, int height)
{
    (void)height;
    SDL_SetRenderDrawColor(g_renderer, 255, 255, 255, 255);
    SDL_RenderClear(g_renderer);
    ski_draw(draw_sprite, NULL);
    draw_status(width);
}

/* ---- Input -------------------------------------------------------------- */

static int vk_from_key(SDL_Keycode k)
{
    switch (k) {
    case SDLK_LEFT: return SKI_VK_LEFT;
    case SDLK_RIGHT: return SKI_VK_RIGHT;
    case SDLK_UP: return SKI_VK_UP;
    case SDLK_DOWN: return SKI_VK_DOWN;
    case SDLK_HOME: return SKI_VK_HOME;
    case SDLK_END: return SKI_VK_END;
    case SDLK_PAGEUP: return SKI_VK_PRIOR;
    case SDLK_PAGEDOWN: return SKI_VK_NEXT;
    case SDLK_INSERT: return SKI_VK_INSERT;
    case SDLK_SPACE: return SKI_VK_INSERT; /* Macs have no Insert key: Space hops too */
    case SDLK_RETURN: return SKI_VK_RETURN;
    case SDLK_ESCAPE: return SKI_VK_ESCAPE;
    case SDLK_F2: return SKI_VK_F2;
    case SDLK_F3: return SKI_VK_F3;
    case SDLK_KP_0: return SKI_VK_NUMPAD0;
    case SDLK_KP_1: return SKI_VK_NUMPAD0 + 1;
    case SDLK_KP_2: return SKI_VK_NUMPAD0 + 2;
    case SDLK_KP_3: return SKI_VK_NUMPAD0 + 3;
    case SDLK_KP_4: return SKI_VK_NUMPAD0 + 4;
    case SDLK_KP_5: return SKI_VK_NUMPAD0 + 5;
    case SDLK_KP_6: return SKI_VK_NUMPAD0 + 6;
    case SDLK_KP_7: return SKI_VK_NUMPAD0 + 7;
    case SDLK_KP_8: return SKI_VK_NUMPAD0 + 8;
    case SDLK_KP_9: return SKI_VK_NUMPAD0 + 9;
    }
    return 0;
}

static void handle_event(const SDL_Event *e)
{
    int vk, w, h;
    const char *c;
    switch (e->type) {
    case SDL_QUIT:
        g_running = 0;
        break;
    case SDL_WINDOWEVENT:
        switch (e->window.event) {
        case SDL_WINDOWEVENT_FOCUS_GAINED: ski_set_window_active(1); break;
        case SDL_WINDOWEVENT_FOCUS_LOST: ski_set_window_active(0); break;
        case SDL_WINDOWEVENT_MINIMIZED: ski_set_minimized(1); break;
        case SDL_WINDOWEVENT_RESTORED: ski_set_minimized(0); break;
        case SDL_WINDOWEVENT_SIZE_CHANGED:
            SDL_GetWindowSize(g_window, &w, &h);
            ski_resize(w, h);
            break;
        }
        break;
    case SDL_KEYDOWN:
        vk = vk_from_key(e->key.keysym.sym);
        if (vk)
            ski_key_down(vk);
        break;
    case SDL_TEXTINPUT:
        for (c = e->text.text; *c; c++)
            if (*c != ' ')
                ski_char((unsigned char)*c);
        break;
    case SDL_MOUSEMOTION:
        ski_mouse_move(e->motion.x, e->motion.y);
        break;
    case SDL_MOUSEBUTTONDOWN:
        if (e->button.button == SDL_BUTTON_LEFT)
            ski_mouse_click();
        break;
    }
}

/* Debugging aid: with SKIFREE_SNAPSHOT=path.bmp set, save the window's
 * pixels once, 2 seconds after startup. */
static void snapshot_if_requested(int ow, int oh)
{
    static int done;
    const char *path = getenv("SKIFREE_SNAPSHOT");
    SDL_Surface *s;
    if (done || !path || SDL_GetTicks() < 2000)
        return;
    done = 1;
    s = SDL_CreateRGBSurfaceWithFormat(0, ow, oh, 32, SDL_PIXELFORMAT_ARGB8888);
    if (s && SDL_RenderReadPixels(g_renderer, NULL, SDL_PIXELFORMAT_ARGB8888, s->pixels, s->pitch) == 0)
        SDL_SaveBMP(s, path);
    else
        fprintf(stderr, "skifree: snapshot: %s\n", SDL_GetError());
    SDL_FreeSurface(s);
}

/* One iteration of the main loop (emscripten_set_main_loop-shaped). */
static void frame(void)
{
    SDL_Event e;
    uint32_t now;
    int w, h, ow, oh;

    while (SDL_PollEvent(&e))
        handle_event(&e);

    /* Like WM_TIMER: at most one tick per interval, no catching up. */
    now = SDL_GetTicks();
    if (now - g_lastTimer >= (uint32_t)g_tickMs) {
        g_lastTimer = now - g_lastTimer >= 2u * g_tickMs ? now : g_lastTimer + g_tickMs;
        ski_timer();
    }

    /* Draw in window points; on a Retina display each point is 2x2 pixels.
     * Rechecked every frame in case the window moves to another display. */
    SDL_GetRendererOutputSize(g_renderer, &ow, &oh);
    SDL_GetWindowSize(g_window, &w, &h);
    SDL_RenderSetScale(g_renderer, (float)ow / w, (float)oh / h);
    render(w, h);
    snapshot_if_requested(ow, oh);
    SDL_RenderPresent(g_renderer);
#ifndef __EMSCRIPTEN__ /* the browser paces frames itself */
    if (!g_vsync)
        SDL_Delay(1); /* no vsync to pace us */
#endif
}

/* ---- Headless ----------------------------------------------------------- */

static int run_headless(const HeadlessOptions *o)
{
    SDL_Surface *surface;
    surface = SDL_CreateRGBSurfaceWithFormat(0, o->width, o->height, 32, SDL_PIXELFORMAT_RGB888);
    g_renderer = surface ? SDL_CreateSoftwareRenderer(surface) : NULL;
    if (!g_renderer || !load_sprites()) {
        fprintf(stderr, "skifree: %s\n", SDL_GetError());
        return 1;
    }
    if (headless_run(o) != 0)
        return 1;
    if (o->screenshot) {
        render(o->width, o->height);
        SDL_RenderPresent(g_renderer);
        if (SDL_SaveBMP(surface, o->screenshot) != 0) {
            fprintf(stderr, "skifree: %s\n", SDL_GetError());
            return 1;
        }
    }
    return 0;
}

/* ---- main --------------------------------------------------------------- */

int main(int argc, char **argv)
{
    HeadlessOptions o;
    int ww, wh, rc;

    if ((rc = headless_parse_args(argc, argv, &o)) != 0)
        return rc;
    g_headless = o.headless;
    g_tickMs = o.tickMs;
    if (!ski_selftest()) {
        fprintf(stderr, "skifree: self-test failed (rand or asset data)\n");
        return 1;
    }

    if (g_headless) {
        if (SDL_Init(0) != 0)
            return 1;
        scores_path_init();
        rc = run_headless(&o);
        SDL_Quit();
        return rc;
    }

    if (SDL_Init(SDL_INIT_VIDEO | SDL_INIT_TIMER) != 0) {
        fprintf(stderr, "skifree: %s\n", SDL_GetError());
        return 1;
    }
    scores_path_init();
    SDL_SetHint(SDL_HINT_RENDER_SCALE_QUALITY, "0");
    SDL_SetHint(SDL_HINT_MOUSE_FOCUS_CLICKTHROUGH, "1");
    g_window = SDL_CreateWindow("SkiFree", SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED, o.width,
                                o.height, SDL_WINDOW_RESIZABLE | SDL_WINDOW_ALLOW_HIGHDPI);
    if (!g_window) {
        fprintf(stderr, "skifree: %s\n", SDL_GetError());
        return 1;
    }
#ifndef __EMSCRIPTEN__ /* the page's CSS sizes the canvas; ski_resize clamps */
    SDL_SetWindowMinimumSize(g_window, SKI_MIN_WIDTH, SKI_MIN_HEIGHT);
#endif
    SDL_StartTextInput(); /* WM_CHAR keys ('f', debug keys); off by default under SDL3 */
    g_renderer = SDL_CreateRenderer(g_window, -1, SDL_RENDERER_ACCELERATED | SDL_RENDERER_PRESENTVSYNC);
    if (g_renderer) {
        SDL_RendererInfo info;
        g_vsync = SDL_GetRendererInfo(g_renderer, &info) == 0 && (info.flags & SDL_RENDERER_PRESENTVSYNC);
    } else {
        g_renderer = SDL_CreateRenderer(g_window, -1, 0);
    }
    if (!g_renderer || !load_sprites()) {
        fprintf(stderr, "skifree: %s\n", SDL_GetError());
        return 1;
    }
    SDL_GetWindowSize(g_window, &ww, &wh);
    if (!ski_init(ww, wh))
        return 1;
    g_lastTimer = SDL_GetTicks();
#ifdef __EMSCRIPTEN__
    /* The browser calls frame() before each repaint. This doesn't return. */
    emscripten_set_main_loop(frame, 0, 1);
#else
    while (g_running)
        frame();
#endif
    SDL_Quit();
    return 0;
}
