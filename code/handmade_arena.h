#ifndef HANDMADE_ARENA_H
#define HANDMADE_ARENA_H

#include "handmade.h"

typedef struct {
    MemoryIndex size;
    MemoryIndex used;

    u8 *base;
} Arena;

#endif // HANDMADE_ARENA_H
