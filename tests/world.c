/* Public multiplayer simulation contract, also compiled to wasm by web-test. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "platform.h"
#include "ski.h"
#include "ski_world.h"

#define CHECK(c) do { if (!(c)) { \
    fprintf(stderr, "FAIL: %s (line %d)\n", #c, __LINE__); exit(1); \
} } while (0)

static uint32_t now;
uint32_t plat_ticks(void) { return now; }
void plat_set_title(const char *title) { (void)title; }
void plat_minimize(void) {}
void plat_play_sound(int slot) { (void)slot; }
void plat_load_scores(const char *key, char *buf, int size)
{
    (void)key; (void)size; buf[0] = '\0';
}
void plat_save_scores(const char *key, const char *value) { (void)key; (void)value; }
void plat_show_scores(const char *title, const char *text) { (void)title; (void)text; }

static SkiWorldPlayer player(const SkiWorld *w, uint32_t id)
{
    SkiWorldPlayer p;
    CHECK(ski_world_player(w, id, &p));
    return p;
}

static uint32_t terrain_hash(const SkiWorld *w)
{
    uint32_t h = 2166136261u;
    int i;
    SkiWorldObject o;
    for (i = 0; i < ski_world_object_count(w); i++) {
        CHECK(ski_world_object(w, i, &o));
        CHECK(abs(o.x) <= SKI_WORLD_HALF_WIDTH);
        CHECK(o.y < SKI_WORLD_CHASE_Y);
        h = (h ^ (uint32_t)o.x) * 16777619u;
        h = (h ^ (uint32_t)o.y) * 16777619u;
        h = (h ^ (uint32_t)o.sprite) * 16777619u;
    }
    return h;
}

static void terrain_and_movement(void)
{
    SkiWorld *a = ski_world_create(42), *b;
    SkiWorldPlayer p, q, start;
    SkiWorldObject object;
    uint32_t id, other, h;
    int i;
    CHECK(a != NULL);
    h = terrain_hash(a);
    CHECK(ski_world_object_count(a) > 500);
    CHECK(!ski_world_object(a, -1, &object));
    CHECK(!ski_world_object(a, ski_world_object_count(a), &object));

    /* World creation and inputs must not depend on the singleton's seed,
     * camera, dimensions, pause/focus, fast mode or platform clock. */
    CHECK(ski_init(320, 900));
    ski_debug_warp(-400, 31000);
    ski_seed(999);
    ski_char('f');
    ski_set_window_active(0);
    b = ski_world_create(42);
    CHECK(b != NULL && terrain_hash(b) == h);
    CHECK(ski_world_object_count(a) == ski_world_object_count(b));
    id = ski_world_join(a);
    other = ski_world_join(b);
    CHECK(id != 0 && id == other);
    start = player(a, id);
    CHECK(ski_world_key(a, id, SKI_VK_DOWN));
    CHECK(ski_world_key(b, other, SKI_VK_DOWN));
    CHECK(!ski_world_key(a, id, SKI_VK_F3));
    CHECK(!ski_world_key(a, 0, SKI_VK_DOWN));
    CHECK(!ski_world_key(a, id, 't'));
    for (i = 0; i < 10; i++) {
        now += 417; /* deliberately not the simulation tick duration */
        ski_resize(900 + i, 320 + i);
        ski_world_step(a);
        ski_world_step(b);
    }
    p = player(a, id);
    q = player(b, other);
    CHECK(memcmp(&p, &q, sizeof p) == 0);
    CHECK(p.x == start.x && p.y == 45 && p.dy == 10 && p.z == 0);
    /* Ten movements at speeds 0..9: 45 units. This catches accidental fast
     * mode, wall-clock scaling or using the singleton's player for inputs. */
    CHECK(ski_world_key(a, id, SKI_VK_INSERT));
    ski_world_step(a);
    p = player(a, id);
    /* Hopping spends four units of forward speed: 45 + (10 - 4). */
    CHECK(p.z == 2 && p.y == 51 && p.dz == 1);
    CHECK(player(b, other).z == 0);
    CHECK(ski_world_key(a, id, SKI_VK_F2));
    p = player(a, id);
    CHECK(p.x == start.x && p.y == 0 && p.dy == 0 && p.style == 0);
    CHECK(!ski_world_aim(a, id, 2049, 0));
    CHECK(!ski_world_aim(a, id, 0, -2049));
    CHECK(!ski_world_click(a, 0));
    CHECK(ski_world_aim(a, id, 0, 800));
    for (i = 0; i < 10; i++) ski_world_step(a);
    CHECK(player(a, id).y == 45);
    CHECK(ski_world_click(a, id));
    ski_world_step(a);
    p = player(a, id);
    /* Mouse hops start at dz=4, unlike the keyboard's smaller dz=2 hop. */
    CHECK(p.z == 4 && p.dz == 3 && p.y == 55);
    CHECK(player(b, other).z == 0 && player(b, other).y == 45);
    CHECK(terrain_hash(a) == h);
    ski_world_destroy(b);
    b = ski_world_create(43);
    CHECK(b != NULL && terrain_hash(b) != h);
    printf("ok: fixed terrain, isolated input and original motion (terrain %08x)\n", h);
    ski_world_destroy(a);
    ski_world_destroy(b);
}

static void capacity_and_boundaries(void)
{
    SkiWorld *w = ski_world_create(7);
    SkiWorldPlayer p, q;
    uint32_t ids[SKI_WORLD_MAX_PLAYERS], replacement;
    int i;
    CHECK(w != NULL);
    for (i = 0; i < SKI_WORLD_MAX_PLAYERS; i++) {
        ids[i] = ski_world_join(w);
        CHECK(ids[i] != 0);
    }
    CHECK(ski_world_join(w) == 0);
    CHECK(ski_world_players(w, NULL, 0) == SKI_WORLD_MAX_PLAYERS);
    /* These two spawns overlap exactly. They must not collide or block one
     * another, even while all slots are occupied. */
    CHECK(player(w, ids[0]).x == player(w, ids[17]).x);
    CHECK(ski_world_key(w, ids[0], SKI_VK_DOWN));
    CHECK(ski_world_key(w, ids[17], SKI_VK_DOWN));
    CHECK(ski_world_key(w, ids[1], SKI_VK_NEXT));
    CHECK(ski_world_key(w, ids[2], SKI_VK_UP));
    for (i = 0; i < 1000; i++) {
        /* Jumps land pointing downhill, and crashes need recovery. Keep
         * steering toward the edges rather than assuming a persistent pose. */
        ski_world_key(w, ids[1], SKI_VK_NEXT);
        ski_world_key(w, ids[3], SKI_VK_END);
        ski_world_step(w);
    }
    p = player(w, ids[0]);
    q = player(w, ids[17]);
    CHECK(p.x == q.x && p.y == q.y && p.dy == q.dy && p.state == q.state);
    CHECK(player(w, ids[1]).x == SKI_WORLD_HALF_WIDTH);
    CHECK(player(w, ids[3]).x == -SKI_WORLD_HALF_WIDTH);
    CHECK(player(w, ids[2]).y == 0);
    ski_world_leave(w, ids[1]);
    replacement = ski_world_join(w);
    CHECK(replacement != 0 && replacement != ids[1]);
    CHECK(!ski_world_player(w, ids[1], &p));
    CHECK(!ski_world_key(w, ids[1], SKI_VK_F2));
    CHECK(player(w, replacement).y == 0);
    CHECK(ski_world_players(w, &p, 1) == SKI_WORLD_MAX_PLAYERS);
    puts("ok: 128 players, no skier collisions, bounded slope and stale IDs");
    ski_world_destroy(w);
}

static void eating_and_respawn(void)
{
    SkiWorld *w = ski_world_create(7), *control = ski_world_create(7);
    uint32_t first, second, dummy, peer, hash;
    uint32_t trajectory = 2166136261u;
    SkiWorldPlayer p, q;
    int tick, chase = 0, eaten = 0, sawCrash = 0;
    CHECK(w != NULL && control != NULL);
    hash = terrain_hash(w);
    first = ski_world_join(w);
    CHECK(first != 0);
    for (tick = 0; tick < 15000; tick++) {
        ski_world_key(w, first, SKI_VK_DOWN); /* get up again after crashes */
        ski_world_step(w);
        p = player(w, first);
        CHECK(p.y >= 0 && p.y <= SKI_WORLD_END_Y);
        CHECK(abs(p.x) <= SKI_WORLD_HALF_WIDTH);
        trajectory = (trajectory ^ (uint32_t)p.y) * 16777619u;
        trajectory = (trajectory ^ (uint32_t)p.z) * 16777619u;
        trajectory = (trajectory ^ (uint32_t)p.state) * 16777619u;
        trajectory = (trajectory ^ (uint32_t)p.style) * 16777619u;
        sawCrash |= p.style < 0;
        if (p.phase == SKI_WORLD_CHASED)
            break;
        CHECK(p.phase == SKI_WORLD_SKIING && p.y <= SKI_WORLD_CHASE_Y);
    }
    CHECK(tick < 15000 && sawCrash);
    CHECK(p.y > SKI_WORLD_CHASE_Y && p.yeti_sprite != 0);
    /* Add an independently moving skier to both worlds in the same slot.
     * One world has an active yeti; the other does not. */
    second = ski_world_join(w);
    dummy = ski_world_join(control);
    peer = ski_world_join(control);
    ski_world_leave(control, dummy);
    CHECK(second != 0 && second == peer);
    CHECK(ski_world_key(w, second, SKI_VK_DOWN));
    CHECK(ski_world_key(control, peer, SKI_VK_DOWN));
    for (tick = 0; tick < 250; tick++) {
        ski_world_step(w);
        ski_world_step(control);
        p = player(w, second);
        q = player(control, peer);
        CHECK(memcmp(&p, &q, sizeof p) == 0);
        p = player(w, first);
        if (p.phase == SKI_WORLD_CHASED)
            chase++;
        if (p.phase == SKI_WORLD_EATEN) {
            eaten++;
            CHECK(p.sprite == 0 && p.yeti_sprite != 0);
            CHECK(!ski_world_key(w, first, SKI_VK_DOWN));
        }
        if (p.runs == 1)
            break;
    }
    CHECK(tick < 250 && chase < 125 && eaten == 75);
    CHECK(p.phase == SKI_WORLD_SKIING && p.y == 0 && p.z == 0);
    CHECK(p.sprite != 0 && p.yeti_sprite == 0 && p.style == 0);
    CHECK(player(w, second).y > 0 && player(w, second).runs == 0);
    CHECK(terrain_hash(w) == hash);
    printf("ok: obstacles, 2000 m chase, 75-tick eating and independent respawn (%d ticks, run %08x)\n",
           tick, trajectory);
    ski_world_destroy(w);
    ski_world_destroy(control);
}

static uint32_t legacy_game(int withWorld)
{
    SkiWorld *w = ski_world_create(42);
    uint32_t id, hash;
    int i;
    CHECK(w != NULL);
    id = ski_world_join(w);
    CHECK(id != 0);
    now = 0;
    CHECK(ski_init(640, 640));
    ski_seed(1);
    ski_key_down(SKI_VK_DOWN);
    for (i = 0; i < 3000; i++) {
        if (withWorld) {
            if (i % 25 == 0)
                ski_world_key(w, id, SKI_VK_DOWN);
            ski_world_step(w);
        }
        now += SKI_TICK_MS;
        ski_timer();
    }
    hash = ski_state_hash();
    ski_world_destroy(w);
    return hash;
}

int main(void)
{
    terrain_and_movement();
    capacity_and_boundaries();
    eating_and_respawn();
    CHECK(legacy_game(0) == legacy_game(1));
    puts("ok: multiplayer leaves legacy simulation unchanged");
    puts("all multiplayer world tests passed");
    return 0;
}
