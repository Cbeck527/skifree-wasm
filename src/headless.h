/*
 * Headless runner shared by the SDL frontend (for screenshots) and the
 * SDL-free test binary (tests/sim.c): runs the game for N ticks on a
 * simulated clock with a scripted input sequence.
 */
#ifndef SKI_HEADLESS_H
#define SKI_HEADLESS_H

#include <stdint.h>

typedef struct HeadlessOptions {
    int headless;   /* --headless given */
    int ticks;
    int width, height;
    int tickMs;
    int seedSet;
    uint32_t seed;
    const char *script;     /* "tick:action,..." */
    const char *screenshot; /* SDL frontend only */
    int hash;               /* print the actor state hash at the end */
    int trace;              /* print the player every N ticks */
} HeadlessOptions;

/* Simulated clock (ms), advanced by tickMs per tick. */
extern uint32_t headless_time;

/* Parse command-line options. Returns 0 on success, prints usage and
 * returns 2 on error. */
int headless_parse_args(int argc, char **argv, HeadlessOptions *o);

/* ski_init, then o->ticks ticks with the script applied. 0 on success. */
int headless_run(const HeadlessOptions *o);

#endif
