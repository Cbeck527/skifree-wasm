/* Data extracted from the original ski32.exe by tools/gen_assets.py. */
#ifndef SKI_ASSETS_H
#define SKI_ASSETS_H

#include <stdint.h>

#define SKI_NUM_SPRITES 90 /* bitmap resource ids 1..89; 0 is an empty sprite */

typedef struct SkiBitmap {
    int16_t width;
    int16_t height;
    const unsigned char *bmp; /* complete .bmp file (4-bit, white = transparent) */
    unsigned int bmpSize;
} SkiBitmap;

/* Per-state steering/speed row (Motion in analysis/ski32/types.h). */
typedef struct SkiMotion {
    int16_t dyAccel;
    int16_t dyMax;
    int16_t dxAccel;
    int16_t dxRatio;
    int16_t dxDir; /* -1 left, +1 right, 0 keep the current direction */
    int16_t pad;
    int32_t state;
} SkiMotion;

extern const SkiBitmap ski_bitmaps[SKI_NUM_SPRITES];

/* String table, indexed by resource string id (see GetResString). */
extern const char *const ski_strings[18];

extern const uint16_t ski_stateSprite[64];    /* state -> sprite id */
extern const int32_t ski_initialState[11];    /* spawn state per animated type */
extern const int32_t ski_landingState[9];     /* PS_JUMP..PS_FLIP_RIGHT -> state on touchdown */
extern const int32_t ski_turnTable[22][2];    /* player state -> {after left, after right} */
extern const SkiMotion ski_playerMotion[22];
extern const SkiMotion ski_skierMotion[5];       /* states 22-26 */
extern const SkiMotion ski_snowboarderMotion[8]; /* states 31-38 */

#endif
