/*
 * skifree-sim: the game core with no SDL, for deterministic tests and
 * sanitizer builds. Same options as `skifree --headless`, minus screenshots.
 * High scores go to a file named by $SKI_SCORES (default: none kept).
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "headless.h"
#include "platform.h"
#include "ski.h"

static char g_scores[3][256];
static const char *const g_keys[3] = {"SS", "GS", "FS"};

uint32_t plat_ticks(void) { return headless_time; }
void plat_set_title(const char *title) { (void)title; }
void plat_minimize(void) {}
void plat_play_sound(int slot) { (void)slot; }

void plat_load_scores(const char *key, char *buf, int size)
{
    int i;
    buf[0] = '\0';
    for (i = 0; i < 3; i++)
        if (strcmp(key, g_keys[i]) == 0)
            snprintf(buf, size, "%s", g_scores[i]);
}

void plat_save_scores(const char *key, const char *value)
{
    int i;
    for (i = 0; i < 3; i++)
        if (strcmp(key, g_keys[i]) == 0)
            snprintf(g_scores[i], sizeof g_scores[i], "%s", value);
}

void plat_show_scores(const char *title, const char *text) { printf("%s\n%s\n", title, text); }

int main(int argc, char **argv)
{
    HeadlessOptions o;
    int rc = headless_parse_args(argc, argv, &o);
    if (rc != 0)
        return rc;
    if (o.screenshot) {
        fprintf(stderr, "skifree-sim: no screenshots; use skifree --headless\n");
        return 2;
    }
    if (!ski_selftest()) {
        fprintf(stderr, "skifree-sim: self-test failed (rand or asset data)\n");
        return 1;
    }
    return headless_run(&o);
}
