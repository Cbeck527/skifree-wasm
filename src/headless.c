/*
 * Headless runner. SCRIPT is a comma-separated list of tick:action, where
 * action is a key name (left right up down home end pgup pgdn insert f2 f3
 * enter), "click", "char=X" for a typed character, or "warp=X/Y" to move the
 * player. Time advances exactly tickMs per tick, so a seed and a script
 * always give the same game.
 */
#include "headless.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "ski.h"

uint32_t headless_time;

typedef struct ScriptStep {
    int tick;
    char action[32];
} ScriptStep;

int headless_parse_args(int argc, char **argv, HeadlessOptions *o)
{
    int i;
    memset(o, 0, sizeof *o);
    o->width = 640;
    o->height = 640;
    o->tickMs = SKI_TICK_MS;
    for (i = 1; i < argc; i++) {
        const char *a = argv[i], *v = i + 1 < argc ? argv[i + 1] : NULL;
        if (strcmp(a, "--headless") == 0) o->headless = 1;
        else if (strcmp(a, "--hash") == 0) o->hash = 1;
        else if (strcmp(a, "--size") == 0 && v && sscanf(v, "%dx%d", &o->width, &o->height) == 2) i++;
        else if (strcmp(a, "--ticks") == 0 && v) o->ticks = atoi(argv[++i]);
        else if (strcmp(a, "--tick-ms") == 0 && v) o->tickMs = atoi(argv[++i]);
        else if (strcmp(a, "--seed") == 0 && v) {
            o->seed = (uint32_t)strtoul(argv[++i], NULL, 0);
            o->seedSet = 1;
        }
        else if (strcmp(a, "--script") == 0 && v) o->script = argv[++i];
        else if (strcmp(a, "--screenshot") == 0 && v) o->screenshot = argv[++i];
        else if (strcmp(a, "--trace") == 0 && v) o->trace = atoi(argv[++i]);
        else {
            fprintf(stderr,
                    "usage: %s [--size WxH] [--tick-ms N]\n"
                    "       %s --headless --ticks N [--seed S] [--script S] [--screenshot F.bmp]\n"
                    "                [--hash] [--trace N] [--size WxH] [--tick-ms N]\n",
                    argv[0], argv[0]);
            return 2;
        }
    }
    if (o->width < SKI_MIN_WIDTH) o->width = SKI_MIN_WIDTH;
    if (o->height < SKI_MIN_HEIGHT) o->height = SKI_MIN_HEIGHT;
    if (o->tickMs < 1) o->tickMs = SKI_TICK_MS;
    return 0;
}

/* Parse "tick:action,..." once. Returns the step count, or -1 on error. */
static int parse_script(const char *script, ScriptStep **out)
{
    int cap = 64, n = 0;
    ScriptStep *steps = malloc(cap * sizeof *steps);
    const char *p = script;
    while (p && *p) {
        int len = 0;
        if (n == cap) {
            cap *= 2;
            steps = realloc(steps, cap * sizeof *steps);
        }
        if (sscanf(p, "%d:%31[^,]%n", &steps[n].tick, steps[n].action, &len) < 2) {
            fprintf(stderr, "skifree: bad script near '%.20s'\n", p);
            free(steps);
            return -1;
        }
        n++;
        p += len;
        if (*p == ',')
            p++;
    }
    *out = steps;
    return n;
}

static int script_action(const char *name)
{
    static const struct { const char *name; int vk; } keys[] = {
        {"left", SKI_VK_LEFT}, {"right", SKI_VK_RIGHT}, {"up", SKI_VK_UP}, {"down", SKI_VK_DOWN},
        {"home", SKI_VK_HOME}, {"end", SKI_VK_END}, {"pgup", SKI_VK_PRIOR}, {"pgdn", SKI_VK_NEXT},
        {"insert", SKI_VK_INSERT}, {"f2", SKI_VK_F2}, {"f3", SKI_VK_F3}, {"enter", SKI_VK_RETURN},
    };
    unsigned i;
    int x, y;
    if (strcmp(name, "click") == 0) {
        ski_mouse_click();
        return 1;
    }
    if (strncmp(name, "warp=", 5) == 0 && sscanf(name + 5, "%d/%d", &x, &y) == 2) {
        ski_debug_warp(x, y);
        return 1;
    }
    if (strncmp(name, "char=", 5) == 0 && name[5]) {
        ski_char((unsigned char)name[5]);
        return 1;
    }
    for (i = 0; i < sizeof keys / sizeof keys[0]; i++) {
        if (strcmp(name, keys[i].name) == 0) {
            ski_key_down(keys[i].vk);
            return 1;
        }
    }
    fprintf(stderr, "skifree: unknown script action '%s'\n", name);
    return 0;
}

int headless_run(const HeadlessOptions *o)
{
    ScriptStep *steps = NULL;
    int nsteps = 0, next = 0, i, x, y, state;

    if (o->script && (nsteps = parse_script(o->script, &steps)) < 0)
        return 1;
    if (!ski_init(o->width, o->height)) {
        free(steps);
        return 1;
    }
    if (o->seedSet)
        ski_seed(o->seed);
    for (i = 0; i < o->ticks; i++) {
        /* Steps are expected in tick order. */
        while (next < nsteps && steps[next].tick <= i) {
            if (steps[next].tick == i && !script_action(steps[next].action)) {
                free(steps);
                return 1;
            }
            next++;
        }
        headless_time += o->tickMs;
        ski_timer();
        if (o->trace && (i + 1) % o->trace == 0) {
            ski_debug_player(&x, &y, &state);
            printf("tick %d: player x=%d y=%d state=%d\n", i + 1, x, y, state);
        }
    }
    if (o->hash)
        printf("hash %08x\n", ski_state_hash());
    free(steps);
    return 0;
}
