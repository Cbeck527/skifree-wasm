/*
 * Recovered data types for ski32.exe (SkiFree 1.04, ski2.c).
 *
 * Parsed into the Ghidra program by scripts/ghidra/ApplyTypes.java before
 * symbols.tsv is applied, so signatures and globals there can use these names.
 * Windows types (HDC, RECT, HGLOBAL, DWORD) come from the program's own
 * data type manager. Keep this file free of preprocessor directives.
 *
 * Coordinates are world units = pixels. +y is downhill; Dist on the status
 * panel is y/16 metres. z is height above the snow (jumps).
 */

/* actor->type. Types below AT_MOGUL are animated and go through UpdateActor
 * (unless spawned from a Placement); the rest are static scenery. */
typedef enum ActorType {
    AT_PLAYER        = 0,
    AT_SKIER         = 1,   /* computer-controlled skier */
    AT_DOG           = 2,
    AT_SNOWBOARDER   = 3,
    AT_CHAIRLIFT     = 4,   /* chair on the lift line, driven by UpdateChairlift */
    AT_YETI_UPHILL   = 5,   /* wakes when the player goes above y = -2000 */
    AT_YETI_DOWNHILL = 6,   /* wakes past y = 32000 (2000 m), the famous one */
    AT_YETI_LEFT     = 7,   /* wakes past x = -16000 */
    AT_YETI_RIGHT    = 8,   /* wakes past x = 16000 */
    AT_BURNING_TREE  = 9,
    AT_WALKING_TREE  = 10,
    AT_MOGUL         = 11,  /* sprite 27 */
    AT_FLAG          = 12,  /* slalom gate flags, sprites 23/24 (25/26 once judged) */
    AT_TREE          = 13,  /* trees 49-51, lift towers 64: solid obstacles */
    AT_ROCK          = 14,  /* rock 45, stump 46, item 86 */
    AT_BUMP          = 15,  /* small bumps 47/48: hop */
    AT_JUMP          = 16,  /* rainbow ramp 52: big jump */
    AT_DECOR         = 17,  /* signs, banners, dog mess: pass-through */
    AT_NONE          = 18   /* spawn pickers: "spawn nothing" */
} ActorType;

/* actor->state. One global numbering; each type owns a range. g_stateSprite
 * maps every state to its sprite. */
typedef enum ActorState {
    /* player, 0x00-0x15 */
    PS_DOWN          = 0,
    PS_DOWN_LEFT     = 1,
    PS_LEFT_DOWN     = 2,
    PS_LEFT          = 3,   /* across the slope, stopped */
    PS_DOWN_RIGHT    = 4,
    PS_RIGHT_DOWN    = 5,
    PS_RIGHT         = 6,
    PS_WALK_LEFT     = 7,   /* pushing sideways */
    PS_WALK_RIGHT    = 8,
    PS_CLIMB_LEFT    = 9,   /* walking uphill */
    PS_CLIMB_RIGHT   = 10,
    PS_CRASHED       = 11,  /* "OUCH!", sliding to a stop */
    PS_SITTING       = 12,  /* stopped after a crash */
    PS_JUMP          = 13,  /* airborne; left/right in the air spin 13 -> 14 -> 16 -> 15 */
    PS_JUMP_LEFT     = 14,
    PS_JUMP_RIGHT    = 15,
    PS_JUMP_BACK     = 16,  /* mid-spin, facing uphill: crash if you land like this */
    PS_TUMBLE        = 17,  /* hit something while airborne */
    PS_FLIP1         = 18,  /* up in the air: 13 -> 18 -> 19 -> 13 (a flip) */
    PS_FLIP2         = 19,
    PS_FLIP_LEFT     = 20,
    PS_FLIP_RIGHT    = 21,
    /* computer skier, 0x16-0x1a */
    SK_DOWN          = 22,
    SK_LEFT          = 23,
    SK_RIGHT         = 24,
    SK_CRASHED       = 25,
    SK_CRASHED_AIR   = 26,
    /* dog, 0x1b-0x1e */
    DOG_WALK1        = 27,
    DOG_WALK2        = 28,
    DOG_WOOF1        = 29,  /* stopped; also what a collision does to the dog */
    DOG_WOOF2        = 30,  /* 1 in 100: leaves sprite 82 behind */
    /* snowboarder, 0x1f-0x26 */
    SB_LEFT          = 31,
    SB_RIGHT         = 32,
    SB_JUMP          = 33,
    SB_CRASH1        = 34,  /* 34-38: tumble after hitting something, then back to SB_RIGHT */
    SB_CRASH2        = 35,
    SB_CRASH3        = 36,
    SB_CRASH4        = 37,
    SB_CRASH5        = 38,
    /* chairlift chairs, 0x27-0x29 */
    LIFT_UP_FULL     = 39,  /* carries a snowboarder; may drop him off */
    LIFT_UP_EMPTY    = 40,
    LIFT_DOWN        = 41,
    /* yeti, 0x2a-0x37 */
    YETI_STAND       = 42,
    YETI_JUMP        = 43,
    YETI_RUN_LEFT1   = 44,
    YETI_RUN_LEFT2   = 45,
    YETI_RUN_RIGHT1  = 46,  /* also used for running downhill */
    YETI_RUN_RIGHT2  = 47,
    YETI_RUN_UP1     = 48,
    YETI_RUN_UP2     = 49,
    YETI_EAT1        = 50,  /* caught the player; 0x32-0x37 is the eating sequence */
    YETI_EAT2        = 51,
    YETI_EAT3        = 52,
    YETI_EAT4        = 53,
    YETI_EAT5        = 54,
    YETI_EAT6        = 55,
    /* burning tree, 0x38-0x3b */
    FIRE1            = 56,
    FIRE2            = 57,
    FIRE3            = 58,
    FIRE4            = 59,
    /* walking tree, 0x3c-0x3f */
    WTREE_STILL      = 60,
    WTREE_START      = 61,
    WTREE_WALK_LEFT  = 62,
    WTREE_WALK_RIGHT = 63
} ActorState;

/* actor->flags */
typedef enum ActorFlags {
    AF_DRAWN      = 0x01,  /* the actor's image is currently on screen */
    AF_GHOST      = 0x02,  /* erase-only copy holding a stale on-screen image */
    AF_RECT_VALID = 0x04,  /* screenRect is up to date */
    AF_DELETE     = 0x08,  /* freed by FreeDeletedActors */
    AF_REDRAW     = 0x10,  /* in the dirty area this frame */
    AF_MOVED      = 0x20,  /* moved or changed sprite this tick (collision candidate) */
    AF_FLAT       = 0x40   /* lies on the snow: drawn under everything (mogul, dog mess) */
} ActorFlags;

/* One bitmap, cut from a shared strip. g_sprites[spriteId], 90 entries
 * (index = bitmap resource id, 0 unused). */
typedef struct Sprite {
    HDC   hdcImage;   /* strip holding the colour image (<=32px wide or wide strip) */
    HDC   hdcMask;    /* matching 1bpp mask strip (NOTSRCCOPY of the image: 1 where the sprite is) */
    short stripY;     /* y offset of this bitmap within the strips */
    short width;
    short height;
    short area;       /* width*height, counted against the spawn density budget */
} Sprite;

struct Placement;

/* 100-entry pool, 0x50 bytes each. */
typedef struct Actor {
    struct Actor     *next;       /* 0x00 active or free list */
    struct Actor     *ghost;      /* 0x04 actor <-> its erase ghost, both directions */
    struct Actor     *drawNext;   /* 0x08 next actor in the same overlapping draw group */
    struct Placement *placement;  /* 0x0c course placement it was spawned from, or NULL */
    unsigned short    spriteId;   /* 0x10 */
    short             pad12;
    Sprite           *sprite;     /* 0x14 &g_sprites[spriteId] */
    ActorType         type;       /* 0x18 */
    ActorState        state;      /* 0x1c */
    RECT              screenRect; /* 0x20 valid when AF_RECT_VALID */
    RECT              drawnRect;  /* 0x30 area being redrawn this frame */
    short             x;          /* 0x40 */
    short             y;          /* 0x42 */
    short             z;          /* 0x44 */
    short             dx;         /* 0x46 */
    short             dy;         /* 0x48 */
    short             dz;         /* 0x4a */
    ActorFlags        flags;      /* 0x4c */
} Actor;

/* A pre-laid-out object (course signs, gates, lift towers, chairs, yetis).
 * Lives in g_placements (256 entries); spawns an actor while near the view. */
typedef struct Placement {
    Actor         *actor;     /* 0x00 spawned actor, NULL while off screen */
    Sprite        *sprite;    /* 0x04 */
    unsigned short spriteId;  /* 0x08 nonzero: static sprite; 0: animated, use state */
    short          pad0a;
    ActorType      type;      /* 0x0c */
    ActorState     state;     /* 0x10 */
    short          x;         /* 0x14 */
    short          y;         /* 0x16 */
    short          z;         /* 0x18 */
    short          dx;        /* 0x1a */
    short          dy;        /* 0x1c */
    short          dz;        /* 0x1e */
    DWORD          timer;     /* 0x20 tick count when the yeti started eating */
} Placement;

/* A contiguous run of g_placements, sorted by y. */
typedef struct PlacementList {
    Placement *begin;
    Placement *end;
    Placement *cursor;        /* first placement not yet above the view */
} PlacementList;

/* Per-state steering/speed row (16 bytes). ApplyMotion moves dy toward
 * dyMax by dyAccel, and |dx| toward dxRatio*dy/2 by dxAccel. */
typedef struct Motion {
    short      dyAccel;
    short      dyMax;
    short      dxAccel;
    short      dxRatio;
    short      dxDir;         /* -1 left, +1 right, 0 keep current direction */
    short      pad0a;
    ActorState state;         /* asserted equal to actor->state */
} Motion;

/* Player turn table row: state after pressing left / right. */
typedef struct TurnRow {
    ActorState left;
    ActorState right;
} TurnRow;

/* A WAVE resource slot (none exist in ski32.exe, so data is always NULL). */
typedef struct Sound {
    HGLOBAL hRes;
    void   *data;
} Sound;
