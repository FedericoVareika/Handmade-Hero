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
#include "handmade_math.h" 

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

typedef enum {
    EntityResidency_nonexistant,
    EntityResidency_dormant,
    EntityResidency_low,
    EntityResidency_high,

    EntityResidency_count,
} EntityResidency;

typedef struct {
    TilemapPosition p;
    f32 width;
    f32 height;

    bool collides;
    u32 d_abs_tile_z;
} DormantEntity;

typedef struct {
} LowEntity;

typedef struct {
    v2 p;                   // NOTE(fede): Relative to camera 
    v2 d_p;
    u32 abs_tile_z;

    u32 facing_direction;
    f32 speed;
} HighEntity;

typedef struct {
    u32 idx;
    EntityResidency residency;
    DormantEntity *dormant;
    LowEntity *low;
    HighEntity *high;
} Entity;

typedef struct {
    Arena world_arena;
    World *world;

    TilemapPosition camera_pos;
    u32 camera_following_entity_index;


    u32 player_index_for_controller[HANDMADE_MAX_INPUTS];

#define MAX_ENTITIES 256
    u32 entity_count;
    EntityResidency entity_residencies[MAX_ENTITIES];
    DormantEntity   dormant_entities[MAX_ENTITIES];
    LowEntity       low_entities[MAX_ENTITIES];
    HighEntity      high_entities[MAX_ENTITIES];

    LoadedBitmap backdrop;

    HeroBitmaps hero_bitmaps[4];
    LoadedBitmap hero_shadow;
} GameState;

#endif //HANDMADE_H
