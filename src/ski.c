/*
 * SkiFree game core, ported from ski32.exe (SkiFree 1.04, ski2.c).
 *
 * Every function here corresponds to one in analysis/ski32/decompiled-game.c
 * and keeps its name. Arithmetic on positions and velocities is done the way
 * the original does it, in 16 bits (S16), including the wrap past y = 32767.
 *
 * Differences from the original, all invisible in play:
 *  - Rendering is a full redraw by the frontend (ski_draw), so the dirty
 *    rectangle machinery (erase "ghost" actors, draw groups, cached screen
 *    rects, AF_DRAWN/AF_GHOST/AF_REDRAW) is gone. The one thing the game
 *    logic used ghosts for, an actor's position at the last draw
 *    (ActorOrGhost), is kept as wasDrawn/drawnY.
 *  - Win32 calls go through platform.h.
 */
#include "ski.h"

#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "assets.h"
#include "platform.h"

typedef int16_t s16;
#define S16(v) ((int16_t)(v))

#define NUM_ACTORS 100
#define NUM_PLACEMENTS 256

/* ---- Types (analysis/ski32/types.h) ------------------------------------ */

enum ActorType {
    AT_PLAYER = 0,
    AT_SKIER = 1,
    AT_DOG = 2,
    AT_SNOWBOARDER = 3,
    AT_CHAIRLIFT = 4,
    AT_YETI_UPHILL = 5,
    AT_YETI_DOWNHILL = 6,
    AT_YETI_LEFT = 7,
    AT_YETI_RIGHT = 8,
    AT_BURNING_TREE = 9,
    AT_WALKING_TREE = 10,
    AT_MOGUL = 11,
    AT_FLAG = 12,
    AT_TREE = 13,
    AT_ROCK = 14,
    AT_BUMP = 15,
    AT_JUMP = 16,
    AT_DECOR = 17,
    AT_NONE = 18,
};

enum ActorState {
    PS_DOWN = 0,
    PS_DOWN_LEFT = 1,
    PS_LEFT_DOWN = 2,
    PS_LEFT = 3,
    PS_DOWN_RIGHT = 4,
    PS_RIGHT_DOWN = 5,
    PS_RIGHT = 6,
    PS_WALK_LEFT = 7,
    PS_WALK_RIGHT = 8,
    PS_CLIMB_LEFT = 9,
    PS_CLIMB_RIGHT = 10,
    PS_CRASHED = 11,
    PS_SITTING = 12,
    PS_JUMP = 13,
    PS_JUMP_LEFT = 14,
    PS_JUMP_RIGHT = 15,
    PS_JUMP_BACK = 16,
    PS_TUMBLE = 17,
    PS_FLIP1 = 18,
    PS_FLIP2 = 19,
    PS_FLIP_LEFT = 20,
    PS_FLIP_RIGHT = 21,
    SK_DOWN = 22,
    SK_LEFT = 23,
    SK_RIGHT = 24,
    SK_CRASHED = 25,
    SK_CRASHED_AIR = 26,
    DOG_WALK1 = 27,
    DOG_WALK2 = 28,
    DOG_WOOF1 = 29,
    DOG_WOOF2 = 30,
    SB_LEFT = 31,
    SB_RIGHT = 32,
    SB_JUMP = 33,
    SB_CRASH1 = 34,
    SB_CRASH5 = 38,
    LIFT_UP_FULL = 39,
    LIFT_UP_EMPTY = 40,
    LIFT_DOWN = 41,
    YETI_STAND = 42,
    YETI_JUMP = 43,
    YETI_RUN_LEFT1 = 44,
    YETI_RUN_RIGHT1 = 46,
    YETI_RUN_UP1 = 48,
    YETI_EAT1 = 50,
    YETI_EAT2 = 51,
    YETI_EAT3 = 52,
    YETI_EAT4 = 53,
    YETI_EAT5 = 54,
    YETI_EAT6 = 55,
    FIRE1 = 56,
    FIRE4 = 59,
    WTREE_STILL = 60,
    WTREE_START = 61,
    WTREE_WALK_LEFT = 62,
    WTREE_WALK_RIGHT = 63,
};

/* Sprite ids used directly by the code. */
enum {
    SPR_GATE_LEFT = 23,  /* pass on its left */
    SPR_GATE_RIGHT = 24, /* pass on its right */
    SPR_GATE_PASSED = 25,
    SPR_GATE_MISSED = 26,
    SPR_MOGUL = 27,
    SPR_ROCK = 45,
    SPR_STUMP = 46,
    SPR_BUMP_SMALL = 47,
    SPR_BUMP_LARGE = 48,
    SPR_TREE = 49,
    SPR_DEAD_TREE = 50,
    SPR_BIG_TREE = 51,
    SPR_JUMP = 52,
    SPR_TITLE = 53,
    SPR_VERSION = 54,
    SPR_NUMPAD_SIGN = 55,
    SPR_KEYS_SIGN = 56,
    SPR_START_LEFT = 57,
    SPR_START_RIGHT = 58,
    SPR_FINISH_LEFT = 59,
    SPR_FINISH_RIGHT = 60,
    SPR_SIGN_SLALOM = 61,
    SPR_SIGN_TREE_SLALOM = 62,
    SPR_SIGN_FREESTYLE = 63,
    SPR_LIFT_TOWER = 64,
    SPR_DOG_MESS = 82,
    SPR_ITEM = 86,
};

/* Actor flags still meaningful without the dirty-rect renderer. */
enum {
    AF_DELETE = 0x08, /* freed by FreeDeletedActors */
    AF_MOVED = 0x20,  /* moved or changed sprite this tick (collision candidate) */
    AF_FLAT = 0x40,   /* lies on the snow: drawn under everything */
};

/* Course geometry (BuildCourses / Check*). */
enum {
    COURSE_START_Y = 640,
    SLALOM_FINISH_Y = 8640,
    LONG_FINISH_Y = 16640, /* Tree Slalom and Freestyle */
    MISSED_GATE_MS = 5000,
};

typedef struct Sprite {
    s16 width;
    s16 height;
    s16 area; /* width*height, the spawn density budget */
} Sprite;

struct Placement;

typedef struct Actor {
    struct Actor *next;
    struct Placement *placement; /* placement it was spawned from, or NULL */
    uint16_t spriteId;
    const Sprite *sprite;
    int type;  /* ActorType */
    int state; /* ActorState */
    s16 x, y, z;
    s16 dx, dy, dz;
    uint32_t flags;
    /* Stand-in for the original's erase ghost: the actor's y when it was last
     * drawn (if it was on screen then). See ActorOrGhost. */
    int wasDrawn;
    s16 drawnY;
} Actor;

typedef struct Placement {
    Actor *actor;
    const Sprite *sprite;
    uint16_t spriteId; /* nonzero: static sprite; 0: animated, use state */
    int type;
    int state;
    s16 x, y, z;
    s16 dx, dy, dz;
    int32_t timer;
} Placement;

typedef struct PlacementList {
    Placement *begin;
    Placement *end;
    Placement *cursor;
} PlacementList;

/* ---- Globals (names from symbols.tsv) ----------------------------------- */

static Sprite g_sprites[SKI_NUM_SPRITES];
static Actor g_actorPool[NUM_ACTORS];
static Actor g_actorTemplate;
static Actor *g_actorList;
static Actor *g_actorFreeList;
static Actor *g_player;    /* the skier: keys, mouse, status, courses */
static Actor *g_viewActor; /* what the view follows */
static int g_onScreenArea;
static int g_spawnArea;

static Placement g_placements[NUM_PLACEMENTS];
static uint16_t g_numPlacements;
static PlacementList g_slalomCourse;
static PlacementList g_treeSlalomCourse;
static PlacementList g_freestyleCourse;
static PlacementList g_liftTowers;
static PlacementList g_movingObjects; /* chairs and yetis */

static int g_inSlalom, g_inTreeSlalom, g_inFreestyle;
static int g_slalomDone, g_treeSlalomDone, g_freestyleDone;
static Placement *g_slalomGates;
static Placement *g_treeSlalomGates;
static Placement *g_nextGate;
static int32_t g_runTimeMs;
static int32_t g_runStartTime;
static int32_t g_stylePoints;

static SkiRect g_clientRect;
static SkiRect g_spawnRect; /* client rect + 120px; actors outside are culled */
static s16 g_viewX, g_viewY;
static s16 g_clientWidth, g_clientHeight;
static s16 g_anchorX, g_anchorY; /* where the view actor sits on screen */
static int g_spawnAccumX, g_spawnAccumY;

static int32_t g_now, g_prevNow, g_frameMs, g_lastStatusDraw, g_pauseStart;
static int g_tickMs;
static int g_timerRunning, g_paused, g_gameActive, g_windowActive, g_minimized;
static int g_fastMode;
static int g_mouseSeen;
static s16 g_lastMouseX, g_lastMouseY;

static SkiStatus g_status;
static uint32_t g_randSeed = 1;

/* ---- Helpers ------------------------------------------------------------ */

#define SKI_ASSERT(cond, line)                                                     \
    do {                                                                           \
        if (!(cond))                                                               \
            fprintf(stderr, "skifree: assertion failed (ski2.c line %d)\n", line); \
    } while (0)

/* MSVC rand(), which the original links statically. */
static void ski_srand(uint32_t seed) { g_randSeed = seed; }

static int ski_rand(void)
{
    g_randSeed = g_randSeed * 214013u + 2531011u;
    return (int)((g_randSeed >> 16) & 0x7fff);
}

static s16 Random(s16 n) { return S16((s16)ski_rand() % n); }

static int RectsOverlap(const SkiRect *a, const SkiRect *b)
{
    return b->left < a->right && a->left < b->right && b->top < a->bottom && a->top < b->bottom;
}

/* World -> screen: centred on x, bottom edge at y - z. */
static SkiRect ComputeSpriteRect(const Sprite *s, s16 x, s16 y, s16 z)
{
    SkiRect r;
    s16 w = s->width, h = s->height;
    s16 yoff = S16(g_anchorY - g_viewY);
    r.left = S16(x + S16(S16(g_anchorX - S16(w / 2)) - g_viewX));
    r.right = r.left + w;
    r.top = S16(S16(y + S16(yoff - z)) - h);
    r.bottom = h + r.top;
    return r;
}

static SkiRect GetActorScreenRect(const Actor *a)
{
    return ComputeSpriteRect(a->sprite, a->x, a->y, a->z);
}

/* The original returns the actor's erase ghost if it has one, i.e. the copy
 * left at its position when it was last drawn. Only y is ever read. */
static s16 ActorOrGhostY(const Actor *a) { return a->wasDrawn ? a->drawnY : a->y; }

static int InterpolateCrossing(int v, int prevV, int y, int prevY, int lineY)
{
    SKI_ASSERT(y != prevY, 0x64c);
    if (y == prevY)
        return v;
    return v - ((v - prevV) * (y - lineY)) / (y - prevY);
}

/* ---- Forward declarations ----------------------------------------------- */

static Actor *SetActorState(Actor *a, int state);
static Actor *SetActorSprite(Actor *a, uint16_t spriteId);
static Actor *SetActorPos(Actor *a, s16 x, s16 y, s16 z);
static void CheckSlalom(Actor *a, s16 prevX, s16 prevY);
static void CheckFreestyle(Actor *a, s16 prevX, s16 prevY);
static void CheckTreeSlalom(Actor *a, s16 prevX, s16 prevY);
static void UpdateStatus(void);
static void StartGameTimer(void);
static void StopGameTimer(void);

/* ---- Actor pool and lifecycle ------------------------------------------- */

static void InitActorPool(void)
{
    int i;
    g_actorList = NULL;
    g_actorFreeList = &g_actorPool[0];
    for (i = 0; i < NUM_ACTORS - 1; i++)
        g_actorPool[i].next = &g_actorPool[i + 1];
    g_actorPool[NUM_ACTORS - 1].next = NULL;
}

/* AllocActor + CloneActor(&g_actorTemplate, 0): new actors go at the head
 * of the active list, which fixes iteration and draw-tie order. */
static Actor *AllocActor(void)
{
    Actor *a = g_actorFreeList;
    g_actorTemplate.sprite = &g_sprites[0];
    if (a == NULL) {
        SKI_ASSERT(0, 0x359);
        return NULL;
    }
    g_actorFreeList = a->next;
    *a = g_actorTemplate;
    a->placement = NULL;
    a->next = g_actorList;
    g_actorList = a;
    return a;
}

static Actor *NewActor(int type, int state)
{
    Actor *a = AllocActor();
    if (a == NULL)
        return NULL;
    SKI_ASSERT(type >= 0 && type <= AT_DECOR, 0x56c);
    a->type = type;
    return SetActorState(a, state);
}

static Actor *NewActorWithSprite(int type, uint16_t spriteId)
{
    Actor *a = AllocActor();
    if (a == NULL)
        return NULL;
    SKI_ASSERT(type >= 0 && type <= AT_DECOR, 0x57b);
    a->type = type;
    return SetActorSprite(a, spriteId);
}

static void DeleteActor(Actor *a) { a->flags |= AF_DELETE; }

static void FreeDeletedActors(void)
{
    Actor **link = &g_actorList;
    while (*link != NULL) {
        Actor *a = *link;
        if (a->flags & AF_DELETE) {
            if (a->placement != NULL) {
                SKI_ASSERT(a->placement->actor == a, 0x376);
                a->placement->actor = NULL;
            }
            if (a == g_player)
                g_player = NULL;
            if (a == g_viewActor)
                g_viewActor = NULL;
            *link = a->next;
            a->next = g_actorFreeList;
            g_actorFreeList = a;
        } else {
            link = &a->next;
        }
    }
}

static int IsFlatSprite(s16 spriteId) { return spriteId == SPR_MOGUL || spriteId == SPR_DOG_MESS; }

static Actor *SetActorSprite(Actor *a, uint16_t spriteId)
{
    if (spriteId != a->spriteId) {
        g_onScreenArea -= a->sprite->area;
        a->spriteId = spriteId;
        a->sprite = &g_sprites[spriteId];
        g_onScreenArea += a->sprite->area;
        a->flags |= AF_MOVED;
        if (IsFlatSprite((s16)spriteId))
            a->flags |= AF_FLAT;
        else
            a->flags &= ~AF_FLAT;
    }
    return a;
}

static Actor *SetActorState(Actor *a, int state)
{
    SKI_ASSERT(state <= 63, 0x43d);
    if (a->state != state) {
        a = SetActorSprite(a, ski_stateSprite[state]);
        a->state = state;
    }
    return a;
}

static void ScrollView(s16 x, s16 y)
{
    g_viewY = y;
    g_viewX = x;
}

static Actor *SetActorPos(Actor *a, s16 x, s16 y, s16 z)
{
    int moved = a->x != x || a->y != y;
    if (a == g_viewActor && moved)
        ScrollView(x, y);
    if (!moved && a->z == z)
        return a;
    a->y = y;
    a->x = x;
    a->z = z;
    a->flags |= AF_MOVED;
    return a;
}

/* pos += velocity (twice in fast mode); gravity while airborne. */
static Actor *MoveActor(Actor *a)
{
    s16 x = S16(a->x + a->dx);
    s16 y = S16(a->y + a->dy);
    s16 z = S16(a->z + a->dz);
    if (g_fastMode) {
        x = S16(x + a->dx);
        y = S16(y + a->dy);
        z = S16(z + a->dz);
    }
    if (z > 0) {
        a->dz = S16(a->dz - 1);
        return SetActorPos(a, x, y, z);
    }
    a->dz = 0;
    return SetActorPos(a, x, y, 0);
}

/* Steer dy toward m->dyMax and |dx| toward m->dxRatio * dy / 2. */
static Actor *ApplyMotion(Actor *a, const SkiMotion *m)
{
    s16 dx = a->dx, dy = a->dy;
    s16 dir, cur, target, newDy;
    int t;

    SKI_ASSERT(a->state == m->state, 0x7a1);
    dir = m->dxDir;
    if (dir == 0)
        dir = dx < 0 ? -1 : dx > 0;
    cur = S16(dir * dx);
    target = S16((m->dxRatio * (dy < 1 ? 0 : dy)) / 2);
    if (target < cur) {
        t = cur - 2;
        if (target <= t)
            target = S16(t);
    } else {
        t = cur + m->dxAccel;
        if (t <= target)
            target = S16(t);
    }
    newDy = m->dyMax;
    if (newDy < dy) {
        t = dy - 2;
        if (!(t < newDy))
            newDy = S16(t);
    } else {
        t = m->dyAccel + dy;
        if (!(newDy < t))
            newDy = S16(t);
    }
    a->dy = newDy;
    a->dx = S16(dir * target);
    return a;
}

/* ---- Scoring ------------------------------------------------------------ */

static void AddStyle(int points)
{
    if (g_inFreestyle)
        g_stylePoints += points;
}

static int FormatTime(int32_t ms, char *out, size_t size)
{
    unsigned cs = ((unsigned)(ms % 1000) & 0xffff) / 10;
    unsigned s = (unsigned)((ms / 1000) % 60) & 0xffff;
    int32_t min = (ms / 1000) / 60;
    unsigned m = (unsigned)(min % 60) & 0xffff;
    unsigned h = (unsigned)(min / 60) & 0xffff;
    return snprintf(out, size, ski_strings[11], h, m, s, cs);
}

/* printf onto the end of buf, never past size. */
static void Append(char *buf, size_t size, size_t *len, const char *fmt, ...)
{
    va_list ap;
    int n;
    if (*len >= size - 1)
        return;
    va_start(ap, fmt);
    n = vsnprintf(buf + *len, size - *len, fmt, ap);
    va_end(ap);
    if (n > 0)
        *len = *len + (size_t)n < size - 1 ? *len + (size_t)n : size - 1;
}

static void AppendTime(char *buf, size_t size, size_t *len, int32_t ms)
{
    char t[32];
    FormatTime(ms, t, sizeof t);
    Append(buf, size, len, "%s", t);
}

/* Top 10 per course, stored as "%ld " values; times are negated so all three
 * lists sort descending. */
static void RecordHighScore(const char *key, int32_t score, int isTime)
{
    int32_t list[11];
    unsigned n = 0, pos = 0, i;
    char buf[256], text[512];
    char *p = buf, *e;
    size_t len;

    if (isTime)
        score = -score;
    plat_load_scores(key, buf, sizeof buf);
    while (*p != '\0' && n < 10) {
        while (*p == ' ')
            p++;
        for (e = p; *e != '\0' && *e != ' '; e++)
            ;
        if (e != p) {
            if (*e != '\0')
                *e++ = '\0';
            list[n++] = (int32_t)atol(p);
            p = e;
        }
    }
    if (n != 0) {
        for (pos = 0; pos < n; pos++)
            if (list[pos] < score)
                break;
    }
    if (pos <= 9) {
        if (n == 10)
            n = 9;
        for (i = n; i > pos; i--)
            list[i] = list[i - 1];
        n++;
        list[pos] = score;
    }

    len = 0;
    buf[0] = '\0';
    for (i = 0; i < n; i++)
        Append(buf, sizeof buf, &len, "%ld ", (long)list[i]);
    plat_save_scores(key, buf);

    len = 0;
    text[0] = '\0';
    for (i = 0; i < n; i++) {
        if (i != 0)
            Append(text, sizeof text, &len, "\n");
        if (isTime)
            AppendTime(text, sizeof text, &len, -list[i]);
        else
            Append(text, sizeof text, &len, "%9ld", (long)list[i]);
        if (i == pos)
            Append(text, sizeof text, &len, "%s", ski_strings[16]);
    }
    if (pos == 10) {
        Append(text, sizeof text, &len, "\n\n");
        if (isTime)
            AppendTime(text, sizeof text, &len, -score);
        else
            Append(text, sizeof text, &len, "%9ld", (long)score);
        Append(text, sizeof text, &len, "%s", ski_strings[17]);
    }
    plat_show_scores(ski_strings[15], text);
}

/* Stop the player at a finish line. */
static void EndCourseRun(void)
{
    int state;
    if (g_player == NULL)
        return;
    state = g_player->state;
    if (state != PS_CRASHED && state != PS_TUMBLE)
        state = g_player->z < 1 ? PS_LEFT : PS_JUMP_LEFT;
    SetActorState(g_player, state);
    UpdateStatus();
}

/* ---- Per-type behaviour ------------------------------------------------- */

static Actor *UpdateSkier(Actor *a)
{
    int state = a->state;
    s16 r;
    if (state > SK_RIGHT)
        return a; /* knocked over */
    a = MoveActor(a);
    a = ApplyMotion(a, &ski_skierMotion[state - SK_DOWN]);
    if (Random(12) == 0) {
        r = Random(3);
        if (r == 0)
            state = SK_DOWN;
        else if (r == 1)
            return SetActorState(a, SK_LEFT);
        else if (r == 2)
            return SetActorState(a, SK_RIGHT);
    }
    return SetActorState(a, state);
}

static Actor *UpdateDog(Actor *a)
{
    int state = a->state;
    s16 x, y, z;
    Actor *mess;

    switch (state) {
    case DOG_WALK1:
        a->dy = S16(Random(3) - 1);
        return SetActorState(MoveActor(a), DOG_WALK2);
    case DOG_WALK2:
        a->dx = 4;
        return SetActorState(MoveActor(a), DOG_WALK1);
    case DOG_WOOF1:
        a->dy = 0;
        a->dx = 0;
        return SetActorState(MoveActor(a), Random(32) != 0 ? DOG_WOOF2 : DOG_WALK1);
    case DOG_WOOF2:
        if (Random(100) != 0)
            return SetActorState(MoveActor(a), DOG_WOOF1);
        z = a->z;
        x = a->x;
        y = S16(a->y - 2);
        mess = NewActorWithSprite(AT_DECOR, SPR_DOG_MESS);
        if (mess != NULL)
            SetActorPos(mess, S16(x - 4), y, z);
        state = DOG_WALK1;
        plat_play_sound(SND_WOOF);
        break;
    }
    return SetActorState(MoveActor(a), state);
}

static Actor *UpdateSnowboarder(Actor *a)
{
    int state = a->state;
    a = MoveActor(a);
    a = ApplyMotion(a, &ski_snowboarderMotion[state - SB_LEFT]);
    if (state == SB_LEFT) {
        if (Random(10) == 0)
            state = SB_RIGHT;
    } else if (state == SB_RIGHT) {
        if (Random(10) == 0)
            return SetActorState(a, SB_LEFT);
    } else if (state == SB_JUMP) {
        if (a->z == 0)
            return SetActorState(a, SB_RIGHT);
    } else {
        state++;
        if (state == SB_CRASH5 + 1)
            return SetActorState(a, SB_RIGHT);
    }
    return SetActorState(a, state);
}

static Actor *UpdateBurningTree(Actor *a)
{
    int state = a->state + 1;
    if (state > FIRE4)
        state = FIRE1;
    return SetActorState(a, state);
}

static Actor *UpdateWalkingTree(Actor *a)
{
    int state = a->state;
    s16 dx;
    switch (state) {
    case WTREE_STILL:
        if (Random(100) == 0) {
            a->dx = S16(Random(2) * 2 - 1);
            return SetActorState(MoveActor(a), WTREE_START);
        }
        break;
    case WTREE_START:
        if (Random(10) == 0) {
            a->dx = 0;
            return SetActorState(MoveActor(a), WTREE_STILL);
        }
        dx = a->dx;
        return SetActorState(MoveActor(a), dx >= 0 ? WTREE_WALK_RIGHT : WTREE_WALK_LEFT);
    case WTREE_WALK_LEFT:
    case WTREE_WALK_RIGHT:
        state = WTREE_START;
        break;
    }
    return SetActorState(MoveActor(a), state);
}

static Actor *UpdateActor(Actor *a)
{
    s16 prevX, prevY, s;
    int state;

    SKI_ASSERT(a->type <= AT_WALKING_TREE && a->placement == NULL, 0x908);
    switch (a->type) {
    case AT_PLAYER:
        break;
    case AT_SKIER:
        return UpdateSkier(a);
    case AT_DOG:
        return UpdateDog(a);
    case AT_SNOWBOARDER:
        return UpdateSnowboarder(a);
    case AT_BURNING_TREE:
        return UpdateBurningTree(a);
    case AT_WALKING_TREE:
        return UpdateWalkingTree(a);
    default:
        SKI_ASSERT(0, 0x91f);
        return a;
    }

    /* The player. */
    prevX = a->x;
    prevY = a->y;
    state = a->state;
    if (state == PS_CRASHED) {
        /* Stay put while the speed counts down, then sit. */
        s = a->dx;
        if (s == 0 && a->dy == 0)
            state = PS_SITTING;
        a->dx = S16(s - (s < 0 ? -1 : s > 0));
        s = a->dy;
        a->dy = S16(s - (s < 0 ? -1 : s > 0));
    } else {
        a = MoveActor(a);
        SKI_ASSERT(state <= PS_FLIP_RIGHT, 0x7f8);
        a = ApplyMotion(a, &ski_playerMotion[state]);
        switch (state) {
        case PS_WALK_LEFT:
        case PS_CLIMB_LEFT:
            state = PS_LEFT;
            break;
        case PS_WALK_RIGHT:
        case PS_CLIMB_RIGHT:
            state = PS_RIGHT;
            break;
        case PS_JUMP: case PS_JUMP_LEFT: case PS_JUMP_RIGHT: case PS_JUMP_BACK:
        case PS_TUMBLE: case PS_FLIP1: case PS_FLIP2: case PS_FLIP_LEFT: case PS_FLIP_RIGHT:
            if (a->z == 0) {
                state = ski_landingState[state - PS_JUMP];
                /* Original bug, kept: the table yields PS_CRASHED (11) for a
                 * bad landing, never PS_TUMBLE, so this never fires. */
                if (state == PS_TUMBLE) {
                    AddStyle(-64);
                    plat_play_sound(SND_CRASH);
                } else {
                    plat_play_sound(SND_LAND);
                }
            }
            break;
        }
    }
    a = SetActorState(a, state);
    switch (state) {
    case PS_WALK_LEFT: case PS_WALK_RIGHT: case PS_CLIMB_LEFT: case PS_CLIMB_RIGHT:
        AddStyle(-1);
        break;
    case PS_JUMP_BACK:
        AddStyle(2);
        break;
    case PS_FLIP1: case PS_FLIP2:
        AddStyle(4);
        break;
    case PS_FLIP_LEFT: case PS_FLIP_RIGHT:
        AddStyle(8);
        break;
    }
    CheckSlalom(a, prevX, prevY);
    CheckFreestyle(a, prevX, prevY);
    CheckTreeSlalom(a, prevX, prevY);
    return a;
}

/* What happens to a when it overlaps b. */
static Actor *HandleCollision(Actor *a, Actor *b)
{
    s16 ay, by, aPrevY, bPrevY, az, bTop, w;
    int crossed, bType, state, dist;

    if (a->type > AT_WALKING_TREE)
        return a;

    /* Did a and b swap vertical order since the last draw (or are level)? */
    ay = a->y;
    by = b->y;
    aPrevY = ActorOrGhostY(a);
    bPrevY = ActorOrGhostY(b);
    if (((ay < by || bPrevY < aPrevY) && (by < ay || aPrevY < bPrevY)) ||
        (ay == by && aPrevY == bPrevY))
        crossed = 0;
    else
        crossed = 1;

    bType = b->type;
    state = a->state;
    az = a->z;
    bTop = S16(b->sprite->height + b->z);

    switch (a->type) {
    case AT_PLAYER:
        if (state == PS_TUMBLE)
            break;
        switch (bType) {
        case AT_DOG:
        case AT_FLAG:
        case AT_DECOR:
            if (crossed)
                a->dy = S16(a->dy / 2);
            if (b->spriteId == SPR_DOG_MESS) {
                AddStyle(-16);
                return SetActorState(a, state);
            }
            break;

        case AT_MOGUL:
            if (state == PS_DOWN) {
                state = PS_JUMP;
                a->dz = 1;
                if (a->dy > 4) {
                    a->dy = S16(a->dy / 2);
                    return SetActorState(a, PS_JUMP);
                }
            }
            break;

        case AT_ROCK:
            if (az > 0) {
                if (bTop < az) {
                    if (b->spriteId == SPR_ITEM) {
                        DeleteActor(b);
                        AddStyle(100);
                        return SetActorState(a, state);
                    }
                    break;
                }
                if (!crossed)
                    break;
                goto bounce;
            }
            /* On the ground a rock is an obstacle. */
            /* fall through */
        case AT_SKIER:
        case AT_SNOWBOARDER:
        case AT_CHAIRLIFT:
        case AT_BURNING_TREE:
        case AT_WALKING_TREE:
        case AT_TREE:
            if (bTop < az || S16(a->sprite->height + az) < b->z) {
                /* Over it (or under it). */
                if (bType == AT_BURNING_TREE) {
                    AddStyle(1000);
                    b->type = AT_TREE;
                    SetActorSprite(b, SPR_DEAD_TREE);
                    return SetActorState(a, state);
                }
                AddStyle(6);
                return SetActorState(a, state);
            }
            if (!crossed)
                break;
            if (bType == AT_TREE) {
                w = b->sprite->width;
                if (w < a->sprite->width)
                    w = a->sprite->width;
                dist = a->x - b->x;
                if (w / 2 < abs(dist)) {
                    a->dy = S16(a->dy / 2); /* glanced it */
                    return SetActorState(a, state);
                }
            }
            if (az == 0 && a->dz == 0) {
                state = PS_CRASHED;
            } else {
                state = PS_TUMBLE;
                if (b->spriteId == SPR_DEAD_TREE) {
                    b->type = AT_BURNING_TREE;
                    SetActorState(b, FIRE1);
                    AddStyle(16);
                    return SetActorState(a, PS_TUMBLE);
                }
            }
            if (a->dy < 0 && b->spriteId == SPR_STUMP) {
                SetActorSprite(b, SPR_ITEM);
                return SetActorState(a, state);
            }
            AddStyle(-32);
            plat_play_sound(SND_CRASH);
            return SetActorState(a, state);

        case AT_BUMP:
            if (az < 1) {
                a->dz = 4;
                goto hop;
            }
            if (bTop <= az)
                break;
        bounce:
            a->dz = S16(a->dy / 2);
            AddStyle(1);
            plat_play_sound(SND_JUMP);
            return SetActorState(a, state);

        case AT_JUMP:
            if (crossed && az < bTop / 2 && a->dy > 0) {
                a->dz = a->dy;
                goto hop;
            }
            break;

        hop:
            AddStyle(1);
            plat_play_sound(SND_JUMP);
            return SetActorState(a, PS_JUMP);
        }
        break;

    case AT_SKIER:
        if (state > SK_RIGHT)
            break;
        if (bType == AT_PLAYER)
            AddStyle(20);
        plat_play_sound(SND_SKIER_HIT);
        return SetActorState(a, b->z > 0 ? SK_CRASHED_AIR : SK_CRASHED);

    case AT_DOG:
        if (state < DOG_WOOF1 && (b->dx != 0 || b->dy != 0)) {
            if (bType == AT_PLAYER)
                AddStyle(3);
            plat_play_sound(SND_DOG_HIT);
            return SetActorState(a, DOG_WOOF1);
        }
        break;

    case AT_SNOWBOARDER:
        switch (bType) {
        case AT_PLAYER:
            AddStyle(20);
            /* fall through */
        case AT_SKIER:
        case AT_SNOWBOARDER:
        case AT_TREE:
        case AT_ROCK:
            if (az < bTop && state != SB_CRASH1)
                return SetActorState(a, SB_CRASH1);
            break;
        case AT_BUMP:
        case AT_JUMP:
            if (az < bTop) {
                a->dz = S16(a->dy / 2);
                plat_play_sound(SND_BOARDER_JUMP);
                return SetActorState(a, SB_JUMP);
            }
            break;
        }
        break;

    case AT_CHAIRLIFT:
    case AT_BURNING_TREE:
        break;

    case AT_YETI_UPHILL:
    case AT_YETI_DOWNHILL:
    case AT_YETI_LEFT:
    case AT_YETI_RIGHT:
        if (b == g_player) {
            Placement *p = a->placement;
            plat_play_sound(SND_YETI_EAT);
            DeleteActor(b);
            SKI_ASSERT(p != NULL, 0x95c);
            if (p != NULL) {
                p->state = YETI_EAT1;
                p->dx = 0;
                p->dy = 0;
                p->timer = g_now;
            }
            a->dx = 0;
            a->dy = 0;
            return SetActorState(a, YETI_EAT1);
        }
        break;

    case AT_WALKING_TREE:
        a->dx = 0;
        return SetActorState(a, WTREE_STILL);
    }
    return SetActorState(a, state);
}

/* ---- Spawning scenery --------------------------------------------------- */

/* Random world position just outside a screen edge: 0 left, 1 right, 2 top,
 * 3 bottom. */
static void RandomEdgePos(int edge, s16 *x, s16 *y)
{
    *x = S16(g_viewX - g_anchorX);
    *y = S16(g_viewY - g_anchorY);
    switch (edge) {
    case 0:
    case 1:
        *x = S16(*x + (edge == 0 ? g_clientRect.left - 60 : g_clientRect.right + 60));
        *y = S16(*y + Random(g_clientHeight) + g_clientRect.top);
        return;
    case 2:
    case 3:
        *x = S16(*x + Random(g_clientWidth) + g_clientRect.left);
        *y = S16(*y + (edge == 2 ? g_clientRect.top - 60 : g_clientRect.bottom + 60));
        return;
    }
    SKI_ASSERT(0, 0x5ae);
}

static Actor *PlaceAtEdge(Actor *a, int edge)
{
    s16 x, y;
    if (a == NULL)
        return NULL;
    RandomEdgePos(edge, &x, &y);
    return SetActorPos(a, x, y, 0);
}

static int PickTypeOpenSlope(void)
{
    uint16_t r;
    if (g_spawnArea / 32 < g_onScreenArea)
        return AT_NONE;
    r = (uint16_t)Random(1000);
    if (r < 50) return AT_WALKING_TREE;
    if (r < 500) return AT_TREE;
    if (r < 700) return AT_BUMP;
    if (r < 750) return AT_MOGUL;
    if (r < 950) return AT_ROCK;
    if (r < 970) return AT_JUMP;
    if (r < 990) return AT_SKIER;
    return AT_DOG;
}

static int PickTypeSlalom(void)
{
    return g_onScreenArea <= g_spawnArea / 64 ? AT_MOGUL : AT_NONE;
}

static int PickTypeTreeSlalom(void)
{
    if (g_spawnArea / 16 < g_onScreenArea)
        return AT_NONE;
    return Random(64) != 0 ? AT_TREE : AT_DOG;
}

static int PickTypeFreestyle(void)
{
    uint16_t r;
    if (g_spawnArea / 32 < g_onScreenArea)
        return AT_NONE;
    r = (uint16_t)Random(100);
    if (r < 2) return AT_WALKING_TREE;
    if (r < 20) return AT_TREE;
    if (r < 50) return AT_BUMP;
    if (r < 60) return AT_MOGUL;
    if (r < 80) return AT_ROCK;
    return AT_JUMP;
}

static uint16_t PickSpriteForType(int type)
{
    s16 r;
    switch (type) {
    case AT_MOGUL:
        return SPR_MOGUL;
    case AT_TREE:
        r = Random(8);
        if (r == 0)
            return SPR_DEAD_TREE;
        return r == 1 ? SPR_BIG_TREE : SPR_TREE;
    case AT_ROCK:
        return Random(4) != 0 ? SPR_ROCK : SPR_STUMP;
    case AT_BUMP:
        return Random(3) != 0 ? SPR_BUMP_SMALL : SPR_BUMP_LARGE;
    case AT_JUMP:
        return SPR_JUMP;
    }
    SKI_ASSERT(0, 0x623);
    return 0;
}

static Actor *SpawnAtEdge(int edge)
{
    s16 x, y;
    int type;
    Actor *a;

    RandomEdgePos(edge, &x, &y);
    if (x < -576 || x > -320 || y < COURSE_START_Y || y > SLALOM_FINISH_Y) {
        if (x < 320 || x > 512 || y < COURSE_START_Y || y > LONG_FINISH_Y) {
            if (x < -160 || x > 160 || y < COURSE_START_Y || y > LONG_FINISH_Y)
                type = PickTypeOpenSlope();
            else
                type = PickTypeFreestyle();
        } else {
            type = PickTypeTreeSlalom();
        }
    } else {
        type = PickTypeSlalom();
    }
    if (type == AT_NONE)
        return NULL;
    if (type < AT_MOGUL)
        a = NewActor(type, ski_initialState[type]);
    else
        a = NewActorWithSprite(type, PickSpriteForType(type));
    return a != NULL ? SetActorPos(a, x, y, 0) : NULL;
}

/* ---- Placements: courses, lift, yetis ----------------------------------- */

static void ResetPlacements(void) { g_numPlacements = 0; }

static void InitPlacementList(PlacementList *list) { list->begin = NULL; }

static Placement *AddPlacement(PlacementList *list, const Placement *tmpl)
{
    Placement *p;
    SKI_ASSERT(g_numPlacements < NUM_PLACEMENTS, 0xa1d);
    p = &g_placements[g_numPlacements++];
    if (list->begin == NULL)
        list->begin = list->end = list->cursor = p;
    SKI_ASSERT(list->end == p, 0xa20);
    list->end++;
    *p = *tmpl;
    p->actor = NULL;
    p->sprite = &g_sprites[p->spriteId];
    return p;
}

static Actor *SpawnPlacementIfVisible(Placement *p)
{
    SkiRect r;
    Actor *a;
    if (p->actor == NULL) {
        r = ComputeSpriteRect(p->sprite, p->x, p->y, p->z);
        if (RectsOverlap(&r, &g_spawnRect)) {
            if (p->spriteId == 0)
                a = NewActor(p->type, p->state);
            else
                a = NewActorWithSprite(p->type, p->spriteId);
            if (a != NULL) {
                a = SetActorPos(a, p->x, p->y, p->z);
                p->actor = a;
                a->placement = p;
            }
        }
    }
    return p->actor;
}

/* Static list sorted by y: spawn actors for placements entering the view. */
static void UpdatePlacementList(PlacementList *list)
{
    s16 anchorY = g_anchorY;
    s16 bottom = S16(g_spawnRect.bottom);
    s16 top = S16(S16(S16(g_spawnRect.top) - anchorY) - 60);
    Placement *p = list->cursor;

    while (p < list->end && !(top <= p->y - g_viewY))
        p++;
    while (list->begin < p && !(p->y - g_viewY < top))
        p--;
    list->cursor = p;
    for (; p < list->end; p++) {
        if (S16(S16(bottom - anchorY) + 60) <= p->y - g_viewY)
            return;
        SpawnPlacementIfVisible(p);
    }
}

static void UpdateChairlift(Placement *p)
{
    Actor *a;
    if (p->y < -1023) {
        p->state = LIFT_DOWN;
        p->dy = 2;
        p->x = -144;
        return;
    }
    if (p->y > 23551) {
        p->state = LIFT_UP_FULL;
        p->dy = -2;
        p->x = -112;
        return;
    }
    if (p->actor != NULL && p->state == LIFT_UP_FULL && Random(1000) == 0) {
        a = NewActor(AT_SNOWBOARDER, SB_JUMP);
        if (a != NULL)
            SetActorPos(a, p->x, p->y, p->z);
        p->state = LIFT_UP_EMPTY;
    }
}

static void UpdateYeti(Placement *p)
{
    int state = p->state, type = p->type;
    s16 px, py, qx, qy, mdx = 0, mdy = 0;
    int ddx, ddy, chase = 0;
    int32_t t;

    if (p->z < 1) {
        p->dz = 0;
        p->z = 0;
    } else {
        p->dz = S16(p->dz - 1);
    }
    if (p->z != 0)
        return; /* mid-hop: keep going */

    if (state > YETI_RUN_UP1 + 1 && state < YETI_EAT6 + 1) {
        /* Eating: about 3 seconds, by the clock. */
        t = g_now - p->timer;
        switch (state) {
        case YETI_EAT1: p->state = YETI_EAT2; return;
        case YETI_EAT2: p->state = t > 499 ? YETI_EAT3 : YETI_EAT1; return;
        case YETI_EAT3: if (t > 700) p->state = YETI_EAT4; return;
        case YETI_EAT4: if (t > 1000) p->state = YETI_EAT5; return;
        case YETI_EAT5: p->state = YETI_EAT6; return;
        case YETI_EAT6: p->state = t > 2999 ? YETI_STAND : YETI_EAT5; return;
        }
        return;
    }

    /* Each yeti waits beyond its own line, and walks back if it strays. */
    py = p->y;
    px = p->x;
    switch (type) {
    case AT_YETI_UPHILL:
        if (py < -1999) chase = 1; else mdy = -10;
        break;
    case AT_YETI_DOWNHILL:
        if (py > 31999) chase = 1; else mdy = 26;
        break;
    case AT_YETI_LEFT:
        if (px < -15999) chase = 1; else mdx = -16;
        break;
    default:
        if (type != AT_YETI_RIGHT || px > 15999) chase = 1; else mdx = 16;
        break;
    }
    if (chase && g_player != NULL) {
        qx = g_player->x;
        qy = g_player->y;
        if ((type == AT_YETI_UPHILL && qy < -2000) || (type == AT_YETI_DOWNHILL && qy > 32000) ||
            (type == AT_YETI_LEFT && qx < -16000) || (type == AT_YETI_RIGHT && qx > 16000)) {
            ddx = qx - px;
            ddy = qy - py;
            /* Never more than a screen away. */
            if (g_clientWidth < ddx)
                p->x = S16(qx - g_clientWidth);
            else if (ddx < -g_clientWidth)
                p->x = S16(qx + g_clientWidth);
            if (g_clientHeight < ddy)
                p->y = S16(g_player->y - g_clientHeight);
            else if (ddy < -g_clientHeight)
                p->y = S16(g_player->y + g_clientHeight);
            mdx = S16(ddx < -16 ? -16 : ddx > 16 ? 16 : ddx);
            mdy = S16(ddy < -10 ? -10 : ddy > 26 ? 26 : ddy);
            plat_play_sound(SND_YETI_CHASE);
        }
    }

    /* The original computes a blended velocity here and then overwrites it;
     * all that survives is a little hop whenever the yeti moves. */
    if (mdx != 0 || mdy != 0)
        p->dz = 1;
    p->dy = mdy;
    p->dx = mdx;
    if (mdy < 0) {
        p->state = YETI_RUN_UP1 + (state == YETI_RUN_UP1);
    } else if (mdx < 0) {
        p->state = YETI_RUN_LEFT1 + (state == YETI_RUN_LEFT1);
    } else if (mdx < 1 && mdy < 1) {
        if (Random(10) == 0) {
            p->dz = 4;
            p->state = YETI_JUMP;
        } else {
            p->state = YETI_STAND;
        }
    } else {
        p->state = YETI_RUN_RIGHT1 + (state == YETI_RUN_RIGHT1);
    }
}

static void UpdateMovingPlacement(Placement *p)
{
    Actor *a;
    p->x = S16(p->x + p->dx);
    p->y = S16(p->y + p->dy);
    p->z = S16(p->z + p->dz);
    if (p->type == AT_CHAIRLIFT)
        UpdateChairlift(p);
    else if (p->type >= AT_YETI_UPHILL && p->type <= AT_YETI_RIGHT)
        UpdateYeti(p);
    else
        SKI_ASSERT(0, 0xaf9);
    if (p->actor != NULL) {
        a = SetActorPos(p->actor, p->x, p->y, p->z);
        SetActorState(a, p->state);
    }
}

static void UpdateMovingPlacements(PlacementList *list)
{
    s16 anchorY = g_anchorY;
    s16 bottom = S16(g_spawnRect.bottom);
    s16 top = S16(g_spawnRect.top);
    s16 rel;
    Placement *p;

    for (p = list->begin; p < list->end; p++) {
        UpdateMovingPlacement(p);
        rel = S16(p->y - g_viewY);
        if (S16(S16(top - anchorY) - 60) <= rel && rel < S16(S16(bottom - anchorY) + 60))
            SpawnPlacementIfVisible(p);
    }
}

static void SetPlacementSprite(Placement *p, uint16_t spriteId)
{
    p->spriteId = spriteId;
    p->sprite = &g_sprites[spriteId];
    if (p->actor != NULL)
        SetActorSprite(p->actor, spriteId);
}

/* Title card, version, NumPad and F2/F3 signs at the start. */
static void PlaceTitleSigns(void)
{
    s16 y = g_viewY, x, x2, y2;
    Actor *a;

    x = S16(-40 - g_sprites[SPR_TITLE].width / 2);
    a = NewActorWithSprite(AT_DECOR, SPR_TITLE);
    if (a) SetActorPos(a, x, y, 0);
    a = NewActorWithSprite(AT_DECOR, SPR_VERSION);
    if (a) SetActorPos(a, x, S16(y + g_sprites[SPR_VERSION].height + 4), 0);

    x2 = g_sprites[SPR_NUMPAD_SIGN].width;
    if (x2 <= g_sprites[SPR_KEYS_SIGN].width)
        x2 = g_sprites[SPR_KEYS_SIGN].width;
    y2 = g_sprites[SPR_NUMPAD_SIGN].height;
    a = NewActorWithSprite(AT_DECOR, SPR_NUMPAD_SIGN);
    if (a) SetActorPos(a, x2, y2, 0);
    a = NewActorWithSprite(AT_DECOR, SPR_KEYS_SIGN);
    if (a) SetActorPos(a, x2, S16(y2 + g_sprites[SPR_KEYS_SIGN].height + 4), 0);
}

/* y of a course sign: near the bottom of the starting screen. */
static s16 CourseSignY(void)
{
    s16 y = S16(S16(S16(g_clientRect.bottom) - g_anchorY) + g_viewY - 60);
    return y > COURSE_START_Y ? 520 : y;
}

static void BuildCourses(void)
{
    Placement t;
    Placement *p;
    s16 y;
    int left, i;

    memset(&t, 0, sizeof t);

    /* Slalom */
    InitPlacementList(&g_slalomCourse);
    t.type = AT_DECOR;
    t.spriteId = SPR_SIGN_SLALOM;
    t.x = S16(S16(S16(g_clientRect.left) - g_anchorX) + 60 + g_viewX);
    if (t.x < -320)
        t.x = -320;
    t.y = CourseSignY();
    AddPlacement(&g_slalomCourse, &t);
    t.spriteId = SPR_START_LEFT;
    t.x = -576;
    t.y = COURSE_START_Y;
    AddPlacement(&g_slalomCourse, &t);
    t.spriteId = SPR_START_RIGHT;
    t.x = -320;
    AddPlacement(&g_slalomCourse, &t);
    t.type = AT_FLAG;
    left = 1;
    g_slalomGates = NULL;
    for (y = 960; y < SLALOM_FINISH_Y; y = S16(y + 320)) {
        t.spriteId = left ? SPR_GATE_LEFT : SPR_GATE_RIGHT;
        t.x = left ? -496 : -400;
        left = !left;
        t.y = y;
        p = AddPlacement(&g_slalomCourse, &t);
        if (g_slalomGates == NULL)
            g_slalomGates = p;
    }
    t.type = AT_DECOR;
    t.spriteId = SPR_FINISH_LEFT;
    t.x = -576;
    t.y = SLALOM_FINISH_Y;
    AddPlacement(&g_slalomCourse, &t);
    t.spriteId = SPR_FINISH_RIGHT;
    t.x = -320;
    AddPlacement(&g_slalomCourse, &t);

    /* Tree Slalom */
    InitPlacementList(&g_treeSlalomCourse);
    t.type = AT_DECOR;
    t.spriteId = SPR_SIGN_TREE_SLALOM;
    t.x = S16(S16(S16(g_clientRect.right) - g_anchorX) - 60 + g_viewX);
    if (t.x > 320)
        t.x = 320;
    t.y = CourseSignY();
    AddPlacement(&g_treeSlalomCourse, &t);
    t.spriteId = SPR_START_LEFT;
    t.x = 320;
    t.y = COURSE_START_Y;
    AddPlacement(&g_treeSlalomCourse, &t);
    t.spriteId = SPR_START_RIGHT;
    t.x = 512;
    AddPlacement(&g_treeSlalomCourse, &t);
    left = 1;
    g_treeSlalomGates = NULL;
    for (y = 1040; y < LONG_FINISH_Y; y = S16(y + 400)) {
        t.type = AT_FLAG;
        t.spriteId = left ? SPR_GATE_LEFT : SPR_GATE_RIGHT;
        t.x = left ? 400 : 432;
        left = !left;
        t.y = y;
        p = AddPlacement(&g_treeSlalomCourse, &t);
        if (g_treeSlalomGates == NULL)
            g_treeSlalomGates = p;
        /* The original sets up a random tree here but never adds it. The
         * three rand() calls still happen, so they're kept. */
        t.type = AT_TREE;
        t.spriteId = PickSpriteForType(AT_TREE);
        t.x = S16(Random(32) + 400);
        Random(400);
    }
    t.type = AT_DECOR;
    t.spriteId = SPR_FINISH_LEFT;
    t.x = 320;
    t.y = LONG_FINISH_Y;
    AddPlacement(&g_treeSlalomCourse, &t);
    t.spriteId = SPR_FINISH_RIGHT;
    t.x = 512;
    AddPlacement(&g_treeSlalomCourse, &t);

    /* Freestyle */
    InitPlacementList(&g_freestyleCourse);
    t.type = AT_DECOR;
    t.spriteId = SPR_SIGN_FREESTYLE;
    t.x = 0;
    t.y = CourseSignY();
    AddPlacement(&g_freestyleCourse, &t);
    t.spriteId = SPR_START_LEFT;
    t.x = -160;
    t.y = COURSE_START_Y;
    AddPlacement(&g_freestyleCourse, &t);
    t.spriteId = SPR_START_RIGHT;
    t.x = 160;
    AddPlacement(&g_freestyleCourse, &t);
    t.spriteId = SPR_FINISH_LEFT;
    t.x = -160;
    t.y = LONG_FINISH_Y;
    AddPlacement(&g_freestyleCourse, &t);
    t.spriteId = SPR_FINISH_RIGHT;
    t.x = 160;
    AddPlacement(&g_freestyleCourse, &t);
    g_freestyleDone = 0;
    g_inFreestyle = 0;

    /* Lift towers */
    InitPlacementList(&g_liftTowers);
    for (i = -1024; i < 23553; i += 2048) {
        t.type = AT_TREE;
        t.spriteId = SPR_LIFT_TOWER;
        t.x = -128;
        t.z = t.dx = t.dy = t.dz = 0;
        t.y = S16(i);
        AddPlacement(&g_liftTowers, &t);
    }

    /* Chairs: uphill carrying a snowboarder at height 32, downhill empty. */
    InitPlacementList(&g_movingObjects);
    for (i = -1024; i < 23553; i += 2048) {
        t.type = AT_CHAIRLIFT;
        t.spriteId = 0;
        t.dz = t.dx = 0;
        t.z = 32;
        t.y = S16(i);
        if (i > -1024) {
            t.state = LIFT_UP_FULL;
            t.x = -112;
            t.dy = -2;
            AddPlacement(&g_movingObjects, &t);
        }
        if (i < 23552) {
            t.state = LIFT_DOWN;
            t.x = -144;
            t.dy = 2;
            AddPlacement(&g_movingObjects, &t);
        }
    }

    /* The four yetis. */
    t.type = AT_YETI_LEFT;
    t.state = YETI_STAND;
    t.spriteId = 0;
    t.x = -16060;
    t.z = 0;
    t.y = 0;
    t.dz = t.dy = t.dx = 0;
    AddPlacement(&g_movingObjects, &t);
    t.type = AT_YETI_RIGHT;
    t.x = 16060;
    AddPlacement(&g_movingObjects, &t);
    t.type = AT_YETI_UPHILL;
    t.x = 0;
    t.y = -2060;
    AddPlacement(&g_movingObjects, &t);
    t.type = AT_YETI_DOWNHILL;
    t.y = 32060;
    AddPlacement(&g_movingObjects, &t);
}

/* ---- Courses ------------------------------------------------------------ */

/* Shared by the two timed courses. */
static void CheckGates(s16 x, s16 prevX, s16 y, s16 prevY)
{
    uint16_t sprite = SPR_GATE_PASSED;
    s16 cx;
    if (g_nextGate->y < y) {
        cx = S16(InterpolateCrossing(x, prevX, y, prevY, g_nextGate->y));
        if ((g_nextGate->spriteId == SPR_GATE_LEFT && g_nextGate->x < cx) ||
            (g_nextGate->spriteId == SPR_GATE_RIGHT && cx < g_nextGate->x)) {
            sprite = SPR_GATE_MISSED;
            g_runStartTime -= MISSED_GATE_MS;
        }
        SetPlacementSprite(g_nextGate, sprite);
        g_nextGate++;
    }
}

static void CheckTimedCourse(Actor *a, s16 prevX, s16 prevY, int *inCourse, int *done,
                             int startMin, int startMax, int finishY, Placement *gates,
                             const char *key)
{
    s16 x = a->x, y = a->y, cx;
    int32_t t;

    if (!*inCourse) {
        if (prevY < COURSE_START_Y + 1 && y > COURSE_START_Y) {
            cx = S16(InterpolateCrossing(x, prevX, y, prevY, COURSE_START_Y));
            if (cx > startMin && cx < startMax) {
                *inCourse = 1;
                g_runStartTime = InterpolateCrossing(g_now, g_prevNow, y, prevY, COURSE_START_Y);
                g_runTimeMs = g_runStartTime - g_now;
                g_nextGate = gates;
            }
        }
        return;
    }
    g_runTimeMs = g_now - g_runStartTime;
    if (y > finishY) {
        t = InterpolateCrossing(g_now, g_prevNow, y, prevY, finishY);
        *inCourse = 0;
        g_runTimeMs = t - g_runStartTime;
        *done = 1;
        EndCourseRun();
        RecordHighScore(key, g_runTimeMs, 1);
        return;
    }
    if (y < COURSE_START_Y + 1) {
        *inCourse = 0;
        return;
    }
    CheckGates(x, prevX, y, prevY);
}

static void CheckSlalom(Actor *a, s16 prevX, s16 prevY)
{
    if (a != g_player)
        return;
    CheckTimedCourse(a, prevX, prevY, &g_inSlalom, &g_slalomDone, -577, -319, SLALOM_FINISH_Y,
                     g_slalomGates, "SS");
}

static void CheckTreeSlalom(Actor *a, s16 prevX, s16 prevY)
{
    if (a != g_player)
        return;
    CheckTimedCourse(a, prevX, prevY, &g_inTreeSlalom, &g_treeSlalomDone, 319, 513, LONG_FINISH_Y,
                     g_treeSlalomGates, "GS");
}

static void CheckFreestyle(Actor *a, s16 prevX, s16 prevY)
{
    s16 x = a->x, y = a->y, cx;
    if (a != g_player)
        return;
    if (!g_inFreestyle) {
        if (prevY < COURSE_START_Y + 1 && y > COURSE_START_Y) {
            cx = S16(InterpolateCrossing(x, prevX, y, prevY, COURSE_START_Y));
            if (cx > -161 && cx < 161)
                g_inFreestyle = 1;
        }
        return;
    }
    if (y > LONG_FINISH_Y) {
        g_inFreestyle = 0;
        g_freestyleDone = 1;
        EndCourseRun();
        RecordHighScore("FS", g_stylePoints, 0);
        return;
    }
    if (y < COURSE_START_Y + 1)
        g_inFreestyle = 0;
}

/* ---- The tick ----------------------------------------------------------- */

static void UpdateWorld(void)
{
    Actor *a, *b;
    SkiRect ra, rb;
    uint32_t aFlags;

    g_spawnAccumX -= g_viewX;
    g_spawnAccumY -= g_viewY;
    for (a = g_actorList; a != NULL; a = a->next) {
        if (a->flags & AF_DELETE)
            continue;
        a->flags &= ~AF_MOVED;
        if (a->placement == NULL && a->type < AT_MOGUL)
            UpdateActor(a);
        if (a != g_player) {
            ra = GetActorScreenRect(a);
            if (!RectsOverlap(&ra, &g_spawnRect)) {
                g_onScreenArea -= a->sprite->area;
                DeleteActor(a);
            }
        }
    }
    UpdatePlacementList(&g_slalomCourse);
    UpdatePlacementList(&g_treeSlalomCourse);
    UpdatePlacementList(&g_freestyleCourse);
    UpdatePlacementList(&g_liftTowers);
    UpdateMovingPlacements(&g_movingObjects);
    FreeDeletedActors();

    /* Each overlapping pair where either moved, both ways round. a's flags
     * and rect are taken once, before the inner loop, as in the original. */
    for (a = g_actorList; a != NULL; a = a->next) {
        ra = GetActorScreenRect(a);
        aFlags = a->flags;
        for (b = g_actorList; b != NULL && b != a; b = b->next) {
            if ((aFlags & AF_MOVED) || (b->flags & AF_MOVED)) {
                rb = GetActorScreenRect(b);
                if (RectsOverlap(&ra, &rb)) {
                    HandleCollision(a, b);
                    if (!(a->flags & AF_DELETE) && !(b->flags & AF_DELETE))
                        HandleCollision(b, a);
                }
            }
        }
    }

    g_spawnAccumX += g_viewX;
    for (g_spawnAccumY += g_viewY; g_spawnAccumY > 60; g_spawnAccumY -= 60)
        SpawnAtEdge(3);
    for (; g_spawnAccumY < -60; g_spawnAccumY += 60)
        SpawnAtEdge(2);
    for (; g_spawnAccumX > 60; g_spawnAccumX -= 60)
        SpawnAtEdge(1);
    for (; g_spawnAccumX < -60; g_spawnAccumX += 60)
        SpawnAtEdge(0);

    if (Random(666) == 0)
        PlaceAtEdge(NewActor(AT_SNOWBOARDER, SB_LEFT), 2);
}

/* Where DrawActors ran in the original: remember what was on screen. */
static void MarkDrawn(void)
{
    Actor *a;
    SkiRect r;
    for (a = g_actorList; a != NULL; a = a->next) {
        r = GetActorScreenRect(a);
        a->wasDrawn = !(a->flags & AF_DELETE) && RectsOverlap(&r, &g_clientRect);
        if (a->wasDrawn)
            a->drawnY = a->y;
    }
}

static void GameTick(void)
{
    int32_t now = (int32_t)plat_ticks();
    g_frameMs = now - g_now;
    g_prevNow = g_now;
    g_now = now;
    UpdateWorld();
    MarkDrawn();
    if (g_now - g_lastStatusDraw > 327)
        UpdateStatus();
}

/* ---- Status panel ------------------------------------------------------- */

static void UpdateStatus(void)
{
    s16 speed = 0, dist = 0;
    int i;
    if (g_player != NULL) {
        speed = g_frameMs == 0 ? 0 : S16((g_player->dy * 1000) / (g_frameMs << 4));
        dist = g_player->y;
        if (g_inSlalom)
            dist = S16(SLALOM_FINISH_Y - dist);
        else if (g_inFreestyle || g_inTreeSlalom)
            dist = S16(LONG_FINISH_Y - dist);
    }
    for (i = 0; i < 4; i++)
        g_status.label[i] = ski_strings[3 + i];
    FormatTime(g_runTimeMs, g_status.value[0], sizeof g_status.value[0]);
    snprintf(g_status.value[1], sizeof g_status.value[1], ski_strings[12], (int)S16(dist / 16));
    snprintf(g_status.value[2], sizeof g_status.value[2], ski_strings[13], (int)speed);
    snprintf(g_status.value[3], sizeof g_status.value[3], ski_strings[14], (long)g_stylePoints);
    g_lastStatusDraw = g_now;
}

/* ---- Game state, timer, pause ------------------------------------------- */

static int InitGameState(void)
{
    g_now = (int32_t)plat_ticks();
    ski_srand((uint32_t)g_now);
    InitActorPool();
    g_viewActor = NULL;
    g_player = NULL;
    g_onScreenArea = 0;
    ResetPlacements();
    g_fastMode = 0;
    g_viewY = 0;
    g_viewX = 0;
    g_spawnAccumY = 0;
    g_spawnAccumX = 0;
    g_stylePoints = 0;
    g_slalomDone = 0;
    g_inSlalom = 0;
    g_treeSlalomDone = 0;
    g_inTreeSlalom = 0;
    g_runTimeMs = 0;
    g_tickMs = SKI_TICK_MS;
    return 1;
}

static void StartGameTimer(void)
{
    if (!g_timerRunning && !g_paused) {
        g_timerRunning = 1;
        g_now = (int32_t)plat_ticks();
        if (g_inSlalom || g_inTreeSlalom)
            g_runStartTime += g_now - g_pauseStart;
    }
}

static void StopGameTimer(void)
{
    if (g_timerRunning) {
        g_timerRunning = 0;
        g_pauseStart = g_now;
    }
}

static void TogglePause(void)
{
    g_paused = g_timerRunning;
    if (g_timerRunning) {
        StopGameTimer();
        plat_set_title(ski_strings[2]);
        return;
    }
    plat_set_title(ski_strings[1]);
    StartGameTimer();
}

static void UpdateActiveState(void)
{
    if (g_windowActive && !g_minimized) {
        g_gameActive = 1;
        StartGameTimer();
        return;
    }
    g_gameActive = 0;
    StopGameTimer();
}

static int NewGame(void)
{
    Actor *a = NewActor(AT_PLAYER, PS_LEFT);
    if (a == NULL)
        return 0;
    g_viewActor = SetActorPos(a, 0, 0, 0);
    g_player = g_viewActor;
    PlaceTitleSigns();
    BuildCourses();
    g_paused = 0;
    StartGameTimer();
    UpdateStatus();
    return 1;
}

static void RestartGame(void)
{
    if (InitGameState()) {
        if (g_paused)
            TogglePause();
        if (NewGame())
            return;
    }
    fprintf(stderr, "skifree: could not restart\n");
}

/* Client rect, screen anchor (centre, one third down), spawn rect. */
static void OnResize(int width, int height)
{
    g_mouseSeen = 0;
    g_clientRect.left = 0;
    g_clientRect.top = 0;
    g_clientRect.right = width;
    g_clientRect.bottom = height;
    g_anchorX = S16((g_clientRect.left + g_clientRect.right) / 2);
    g_anchorY = S16((g_clientRect.top + g_clientRect.bottom) / 3);
    g_spawnRect.left = g_clientRect.left - 120;
    g_spawnRect.top = g_clientRect.top - 120;
    g_spawnRect.right = g_clientRect.right + 120;
    g_spawnRect.bottom = g_clientRect.bottom + 120;
    g_clientHeight = S16(g_clientRect.bottom - g_clientRect.top);
    g_clientWidth = S16(g_clientRect.right - g_clientRect.left);
    g_spawnArea = (g_spawnRect.bottom - g_spawnRect.top) * (g_spawnRect.right - g_spawnRect.left);
}

/* ---- Input -------------------------------------------------------------- */

static void OnKeyDown(int vk)
{
    int state, d;
    s16 z;

    switch (vk) {
    case SKI_VK_RETURN:
        if (g_player != NULL)
            return;
        /* fall through: Enter restarts after being eaten */
    case SKI_VK_F2:
        RestartGame();
        return;
    case SKI_VK_ESCAPE:
        plat_minimize();
        return;
    case SKI_VK_F3:
        TogglePause();
        return;
    }
    if (g_player == NULL)
        return;
    state = g_player->state;
    z = g_player->z;
    if (state != PS_CRASHED && state != PS_TUMBLE) {
        switch (vk) {
        case SKI_VK_PRIOR:
        case SKI_VK_NUMPAD0 + 9:
            if (z == 0)
                state = PS_RIGHT;
            break;
        case SKI_VK_NEXT:
        case SKI_VK_NUMPAD0 + 3:
            if (z == 0)
                state = PS_DOWN_RIGHT;
            break;
        case SKI_VK_END:
        case SKI_VK_NUMPAD0 + 1:
            if (z == 0)
                state = PS_DOWN_LEFT;
            break;
        case SKI_VK_HOME:
        case SKI_VK_NUMPAD0 + 7:
            if (z == 0)
                state = PS_LEFT;
            break;
        case SKI_VK_LEFT:
        case SKI_VK_NUMPAD0 + 4:
            SKI_ASSERT(state <= PS_FLIP_RIGHT, 0xf63);
            state = ski_turnTable[state][0];
            if (state == PS_WALK_LEFT) {
                d = g_player->dx - 8;
                if (d < -7)
                    d = -8;
                g_player->dx = S16(d);
            }
            break;
        case SKI_VK_UP:
        case SKI_VK_NUMPAD0 + 8:
            switch (state) {
            case PS_LEFT: case PS_WALK_LEFT: case PS_SITTING:
                if (g_player->dy == 0) {
                    state = PS_CLIMB_LEFT;
                    g_player->dy = -4;
                }
                break;
            case PS_RIGHT: case PS_WALK_RIGHT:
                if (g_player->dy == 0) {
                    state = PS_CLIMB_RIGHT;
                    g_player->dy = -4;
                }
                break;
            case PS_JUMP: state = PS_FLIP1; break;
            case PS_JUMP_LEFT: state = PS_FLIP_LEFT; break;
            case PS_JUMP_RIGHT: state = PS_FLIP_RIGHT; break;
            case PS_FLIP1: state = PS_FLIP2; break;
            case PS_FLIP2: state = PS_JUMP; break;
            }
            break;
        case SKI_VK_RIGHT:
        case SKI_VK_NUMPAD0 + 6:
            SKI_ASSERT(state <= PS_FLIP_RIGHT, 0xf6b);
            state = ski_turnTable[state][1];
            if (state == PS_WALK_RIGHT) {
                d = g_player->dx + 8;
                if (d > 7)
                    d = 8;
                g_player->dx = S16(d);
            }
            break;
        case SKI_VK_DOWN:
        case SKI_VK_NUMPAD0 + 2:
            if (z == 0) {
                state = PS_DOWN;
                break;
            }
            switch (state) {
            case PS_JUMP: state = PS_FLIP2; break;
            case PS_FLIP1: state = PS_JUMP; break;
            case PS_FLIP2: state = PS_FLIP1; break;
            case PS_FLIP_LEFT: state = PS_JUMP_LEFT; break;
            case PS_FLIP_RIGHT: state = PS_JUMP_RIGHT; break;
            }
            break;
        case SKI_VK_INSERT:
        case SKI_VK_NUMPAD0:
            if (z == 0) {
                g_player->dz = 2;
                state = PS_JUMP;
                if (g_player->dy > 4)
                    g_player->dy = S16(g_player->dy - 4);
            }
            break;
        }
    }
    if (state != g_player->state)
        SetActorState(g_player, state);
}

/* On the ground: direction from the cursor's angle. */
static int DirStateFromMouse(s16 dx, s16 dy)
{
    s16 r;
    if (dy > 0) {
        if (dx == 0)
            return PS_DOWN;
        r = S16((dy * 4) / dx);
        if (r < -11) return PS_DOWN;
        if (r < -5) return PS_DOWN_LEFT;
        if (r < -2) return PS_LEFT_DOWN;
        if (r < 0) return PS_LEFT;
        if (r > 11) return PS_DOWN;
        if (r > 5) return PS_DOWN_RIGHT;
        if (r > 2) return PS_RIGHT_DOWN;
        if (r > 0) return PS_RIGHT;
    }
    return dx >= 0 ? PS_RIGHT : PS_LEFT;
}

/* In the air: pose from the cursor's quadrant. The lower-right quadrant
 * gives PS_JUMP_LEFT; that's what the original does. */
static int AirStateFromMouse(s16 dx, s16 dy)
{
    if (dx < 0) {
        if (dy < 0)
            return dx <= dy ? PS_JUMP_LEFT : PS_JUMP_BACK;
        return dx <= -dy ? PS_JUMP_LEFT : PS_JUMP;
    }
    if (dy < 0)
        return -dy > dx ? PS_JUMP_BACK : PS_JUMP_RIGHT;
    return dy <= dx ? PS_JUMP_LEFT : PS_JUMP;
}

static void OnMouseMove(s16 x, s16 y)
{
    int state;
    if (g_mouseSeen && (x != g_lastMouseX || y != g_lastMouseY) && g_player != NULL &&
        g_player->state != PS_CRASHED && g_player->state != PS_TUMBLE) {
        s16 dx = S16(x - g_anchorX), dy = S16(y - g_anchorY);
        state = g_player->z == 0 ? DirStateFromMouse(dx, dy) : AirStateFromMouse(dx, dy);
        SetActorState(g_player, state);
    }
    g_lastMouseX = x;
    g_lastMouseY = y;
    g_mouseSeen = 1;
}

static void OnMouseClick(void)
{
    int state;
    if (g_player == NULL) {
        RestartGame();
        return;
    }
    state = g_player->state;
    if (state != PS_CRASHED) {
        if (g_player->z == 0) {
            g_player->dz = 4;
            state = PS_JUMP;
        } else if (state != PS_TUMBLE) {
            switch (state) {
            case PS_JUMP: state = PS_FLIP1; break;
            case PS_JUMP_LEFT: state = PS_FLIP_LEFT; break;
            case PS_JUMP_RIGHT: state = PS_FLIP_RIGHT; break;
            case PS_FLIP1: state = PS_FLIP2; break;
            case PS_FLIP2: state = PS_JUMP; break;
            }
        }
    }
    if (state != g_player->state)
        SetActorState(g_player, state);
}

static void OnChar(int ch)
{
    switch (ch) {
    case 'f': /* fast mode */
        g_fastMode = !g_fastMode;
        return;
    case 't': /* debug: one tick */
        GameTick();
        return;
    case 'r': /* debug: redraw (the frontend always redraws) */
        return;
    }
    if (g_player == NULL)
        return;
    switch (ch) { /* debug: nudge the player */
    case 'X': SetActorPos(g_player, S16(g_player->x - 2), g_player->y, g_player->z); break;
    case 'x': SetActorPos(g_player, S16(g_player->x + 2), g_player->y, g_player->z); break;
    case 'Y': SetActorPos(g_player, g_player->x, S16(g_player->y - 2), g_player->z); break;
    case 'y': SetActorPos(g_player, g_player->x, S16(g_player->y + 2), g_player->z); break;
    }
}

/* ---- Public API --------------------------------------------------------- */

int ski_init(int clientWidth, int clientHeight)
{
    int i;
    for (i = 0; i < SKI_NUM_SPRITES; i++) {
        g_sprites[i].width = ski_bitmaps[i].width;
        g_sprites[i].height = ski_bitmaps[i].height;
        g_sprites[i].area = S16(ski_bitmaps[i].width * ski_bitmaps[i].height);
    }
    if (!InitGameState())
        return 0;
    /* InitInstance, then WM_CREATE / WM_SIZE / WM_ACTIVATE. */
    g_timerRunning = 0;
    g_gameActive = 0;
    g_windowActive = 0;
    g_minimized = 1;
    OnResize(clientWidth < SKI_MIN_WIDTH ? SKI_MIN_WIDTH : clientWidth,
             clientHeight < SKI_MIN_HEIGHT ? SKI_MIN_HEIGHT : clientHeight);
    g_minimized = 0;
    g_windowActive = 1;
    UpdateActiveState();
    plat_set_title(ski_strings[1]);
    return NewGame();
}

void ski_resize(int clientWidth, int clientHeight)
{
    OnResize(clientWidth < SKI_MIN_WIDTH ? SKI_MIN_WIDTH : clientWidth,
             clientHeight < SKI_MIN_HEIGHT ? SKI_MIN_HEIGHT : clientHeight);
}

void ski_set_window_active(int active)
{
    g_windowActive = active;
    UpdateActiveState();
}

void ski_set_minimized(int minimized)
{
    g_minimized = minimized;
    UpdateActiveState();
}

void ski_timer(void)
{
    if (g_timerRunning && g_gameActive)
        GameTick();
}

void ski_key_down(int vk)
{
    if (g_gameActive)
        OnKeyDown(vk);
}

void ski_char(int ch)
{
    if (g_gameActive)
        OnChar(ch);
}

void ski_mouse_move(int x, int y)
{
    if (g_gameActive)
        OnMouseMove(S16(x), S16(y));
}

void ski_mouse_click(void)
{
    if (g_gameActive)
        OnMouseClick();
}

/* Back to front: ascending y (flat sprites by their top edge), ties in
 * list order, as DrawActor orders each overlap group. */
void ski_draw(SkiDrawFn draw, void *ctx)
{
    Actor *vis[NUM_ACTORS];
    s16 key[NUM_ACTORS];
    SkiRect r;
    Actor *a;
    int n = 0, i, j;

    for (a = g_actorList; a != NULL; a = a->next) {
        if ((a->flags & AF_DELETE) || a->spriteId == 0)
            continue;
        r = GetActorScreenRect(a);
        if (!RectsOverlap(&r, &g_clientRect))
            continue;
        vis[n] = a;
        key[n] = S16(a->y - ((a->flags & AF_FLAT) ? a->sprite->height : 0));
        n++;
    }
    for (i = 1; i < n; i++) { /* stable insertion sort */
        Actor *va = vis[i];
        s16 k = key[i];
        for (j = i; j > 0 && key[j - 1] > k; j--) {
            vis[j] = vis[j - 1];
            key[j] = key[j - 1];
        }
        vis[j] = va;
        key[j] = k;
    }
    for (i = 0; i < n; i++) {
        r = GetActorScreenRect(vis[i]);
        draw(ctx, vis[i]->spriteId, r.left, r.top);
    }
}

const SkiStatus *ski_status(void) { return &g_status; }

void ski_seed(uint32_t seed) { ski_srand(seed); }

/* MSVC rand() after srand(1), and a few sprite sizes from the analysis. */
int ski_selftest(void)
{
    static const int expected[5] = {41, 18467, 6334, 26500, 19169};
    uint32_t saved = g_randSeed;
    int i, ok = 1;
    ski_srand(1);
    for (i = 0; i < 5; i++)
        ok &= ski_rand() == expected[i];
    g_randSeed = saved;
    ok &= ski_bitmaps[1].width == 16 && ski_bitmaps[1].height == 32;
    ok &= ski_bitmaps[53].width == 93 && ski_bitmaps[53].height == 57;
    ok &= ski_stateSprite[SK_DOWN] == 28 && ski_initialState[AT_WALKING_TREE] == WTREE_STILL;
    return ok;
}

uint32_t ski_state_hash(void)
{
    uint32_t h = 2166136261u;
    const Actor *a;
    int v[5], i;
    for (a = g_actorList; a != NULL; a = a->next) {
        v[0] = a->type;
        v[1] = a->state;
        v[2] = a->x;
        v[3] = a->y;
        v[4] = a->z;
        for (i = 0; i < 5; i++)
            h = (h ^ (uint32_t)v[i]) * 16777619u;
    }
    return h;
}

void ski_debug_player(int *x, int *y, int *state)
{
    *x = g_player ? g_player->x : 0;
    *y = g_player ? g_player->y : 0;
    *state = g_player ? g_player->state : -1;
}

void ski_debug_warp(int x, int y)
{
    if (g_player != NULL)
        SetActorPos(g_player, S16(x), S16(y), g_player->z);
}
