#ifndef HANDMADE_H
#define HANDMADE_H

/*
 * NOTE(fede):
 *
 *  HANDMADE_SLOW:
 *    0 - No slow code allowed.
 *    1 - Slow code allowed.
 *
 *  HANDMADE_INTERNAL:
 *    0 - Build for public use.
 *    1 - Build for developer only.
 *
 * */

#include "handmade_platform.h"

#if HANDMADE_SLOW

/*
 * NOTE(fede): HANDMADE_ASSERT is for code that we only want to include for
 *             asserts.
 *             For example the _end in the button union, that is useless for
 *             game code, only to assert that the size of the button array is
 *             equal to the size of the button struct.
 * */
#define HANDMADE_ASSERT 1

#define assert(expression)                                                     \
    if (!(expression)) {                                                       \
        *(int *)0 = 0;                                                         \
    }

#else

#define HANDMADE_ASSERT 0
#define assert(expression)

#endif


#define internal static
#define global static
#define local_persist static

#define PI 3.14159265359f

#define kilobytes(value) ((value) * 1024)
#define megabytes(value) (kilobytes(value) * 1024)
#define gigabytes(value) (megabytes(value) * 1024)
#define terabytes(value) (gigabytes(value) * 1024)

#define max(a, b) ((a) > (b) ? (a) : (b))
#define min(a, b) ((a) < (b) ? (a) : (b))
#define abs(a) ((a) < 0 ? -(a) : (a))

#define array_count(a) (sizeof((a)) / sizeof((a)[0]))

internal inline GameControllerInput *get_game_controller(GameInput *input,
                                                         int controller_index) {
    assert(controller_index < HANDMADE_MAX_INPUTS);
    return &input->controllers[controller_index];
}

#include "handmade_intrinsics.h" 
#include "handmade_tile.h" 
#include "handmade_arena.h" 

typedef struct {
    Tilemap *tilemap;
} World;

typedef struct {
    i32 width;
    i32 height;
    u32 *pixels;
} LoadedBitmap;

typedef struct {
    LoadedBitmap head;
    LoadedBitmap cape;
    LoadedBitmap torso;
    u32 align_x;
    u32 align_y;
} HeroBitmaps;

typedef struct {
    Arena world_arena;
    World *world;

    TilemapPosition player_pos;
    TilemapPosition camera_pos;

    LoadedBitmap backdrop;

    u32 hero_facing_direction;
    HeroBitmaps hero_bitmaps[4];
    LoadedBitmap hero_shadow;
} GameState;

#endif //HANDMADE_H
