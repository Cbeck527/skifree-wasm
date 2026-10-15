/* Camera-independent simulation for the public multiplayer mountain.
 * No SDL, browser, wall clock, or transport is required. */
#ifndef SKI_WORLD_H
#define SKI_WORLD_H

#include <stdint.h>

#define SKI_WORLD_MAX_PLAYERS 128
#define SKI_WORLD_HALF_WIDTH 1200
#define SKI_WORLD_CHASE_Y 32000 /* 2000 m, in the original's world units */
#define SKI_WORLD_END_Y 32480

typedef struct SkiWorld SkiWorld;

enum SkiWorldPhase {
    SKI_WORLD_SKIING,
    SKI_WORLD_CHASED,
    SKI_WORLD_EATEN,
};

typedef struct SkiWorldObject {
    int sprite;
    int x, y;
} SkiWorldObject;

/* Value snapshots: callers never retain pointers into the simulation. */
typedef struct SkiWorldPlayer {
    uint32_t id;
    unsigned runs; /* completed eating/respawn cycles, not manual F2 resets */
    int x, y, z;
    int dx, dy, dz;
    int state, sprite, style;
    enum SkiWorldPhase phase;
    int yeti_x, yeti_y, yeti_sprite; /* sprite is zero outside chase/eating */
} SkiWorldPlayer;

/* Worlds own their terrain, random stream, players and tick counters. A seed
 * reproduces the same terrain regardless of cameras or player activity. */
SkiWorld *ski_world_create(uint32_t seed);
void ski_world_destroy(SkiWorld *world);

/* IDs remain valid until leave; a full world returns zero. */
uint32_t ski_world_join(SkiWorld *world);
void ski_world_leave(SkiWorld *world, uint32_t id);

/* Gameplay keys use ski.h's VK codes; F2 resets only this skier. Other
 * process/UI/debug commands are rejected. Returns 1 for an accepted command. */
int ski_world_key(SkiWorld *world, uint32_t id, int key);
/* Mouse direction relative to the skier, not absolute screen coordinates.
 * Components must be between -2048 and 2048. Click hops/flips as in the original. */
int ski_world_aim(SkiWorld *world, uint32_t id, int dx, int dy);
int ski_world_click(SkiWorld *world, uint32_t id);

/* Exactly one SKI_TICK_MS tick, independent of focus, resize and wall time. */
void ski_world_step(SkiWorld *world);
/* Single-player lookup returns 0 for an absent ID. The list query returns
 * the total population and writes at most capacity snapshots; NULL/0 counts. */
int ski_world_player(const SkiWorld *world, uint32_t id, SkiWorldPlayer *out);
int ski_world_players(const SkiWorld *world, SkiWorldPlayer *out, int capacity);
int ski_world_object_count(const SkiWorld *world);
int ski_world_object(const SkiWorld *world, int index, SkiWorldObject *out);

#endif
