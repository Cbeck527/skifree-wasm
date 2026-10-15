/* Node/WASM boundary: own one world and serialize value snapshots. */
#include <stdio.h>
#include <stdint.h>
#include <emscripten.h>

#include "assets.h"
#include "platform.h"
#include "ski_world.h"

static SkiWorld *world;
/* 2048 terrain triples or 128 player objects, never both at once. */
static char json[131072];

uint32_t plat_ticks(void) { return 0; }
void plat_set_title(const char *title) { (void)title; }
void plat_minimize(void) {}
void plat_play_sound(int slot) { (void)slot; }
void plat_load_scores(const char *key, char *buf, int size)
{
    (void)key; (void)size; buf[0] = '\0';
}
void plat_save_scores(const char *key, const char *value) { (void)key; (void)value; }
void plat_show_scores(const char *title, const char *text) { (void)title; (void)text; }

EMSCRIPTEN_KEEPALIVE int server_init(uint32_t seed)
{
    ski_world_destroy(world);
    world = ski_world_create(seed);
    return world != NULL;
}

EMSCRIPTEN_KEEPALIVE void server_destroy(void)
{
    ski_world_destroy(world);
    world = NULL;
}

EMSCRIPTEN_KEEPALIVE uint32_t server_join(void) { return ski_world_join(world); }
EMSCRIPTEN_KEEPALIVE void server_leave(uint32_t id) { ski_world_leave(world, id); }
EMSCRIPTEN_KEEPALIVE int server_key(uint32_t id, int key) { return ski_world_key(world, id, key); }
EMSCRIPTEN_KEEPALIVE int server_aim(uint32_t id, int dx, int dy) { return ski_world_aim(world, id, dx, dy); }
EMSCRIPTEN_KEEPALIVE int server_click(uint32_t id) { return ski_world_click(world, id); }
EMSCRIPTEN_KEEPALIVE void server_step(void) { ski_world_step(world); }

EMSCRIPTEN_KEEPALIVE const char *server_terrain(void)
{
    SkiWorldObject o;
    int i, n = ski_world_object_count(world);
    size_t used = 0;
    used += snprintf(json + used, sizeof json - used, "{\"terrain\":[");
    for (i = 0; i < n; i++) {
        ski_world_object(world, i, &o);
        used += snprintf(json + used, sizeof json - used, "%s[%d,%d,%d]",
                         i ? "," : "", o.sprite, o.x, o.y);
    }
    used += snprintf(json + used, sizeof json - used, "],\"sprites\":[");
    for (i = 0; i < SKI_NUM_SPRITES; i++)
        used += snprintf(json + used, sizeof json - used, "%s[%d,%d]",
                         i ? "," : "", ski_bitmaps[i].width, ski_bitmaps[i].height);
    snprintf(json + used, sizeof json - used, "]}");
    return json;
}

EMSCRIPTEN_KEEPALIVE const char *server_players(void)
{
    SkiWorldPlayer players[SKI_WORLD_MAX_PLAYERS];
    int i, n = ski_world_players(world, players, SKI_WORLD_MAX_PLAYERS);
    size_t used = 0;
    used += snprintf(json + used, sizeof json - used, "[");
    for (i = 0; i < n; i++) {
        const SkiWorldPlayer *p = &players[i];
        used += snprintf(json + used, sizeof json - used,
            "%s{\"id\":%u,\"x\":%d,\"y\":%d,\"z\":%d,\"sprite\":%d,"
            "\"phase\":%d,\"runs\":%u,\"style\":%d,\"dy\":%d,"
            "\"yeti\":{\"x\":%d,\"y\":%d,\"sprite\":%d}}",
            i ? "," : "", p->id, p->x, p->y, p->z, p->sprite,
            p->phase, p->runs, p->style, p->dy, p->yeti_x, p->yeti_y, p->yeti_sprite);
    }
    snprintf(json + used, sizeof json - used, "]");
    return json;
}
