#ifndef HANDMADE_H
#define HANDMADE_H

#include <stdint.h>
#include <stdio.h> // NOTE(fede): for size_t type

// TODO(fede): fix screen tearing issues

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

#define internal static
#define global static
#define local_persist static

#define PI 3.14159265359

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

#define kilobytes(value) ((value) * 1024)
#define megabytes(value) (kilobytes(value) * 1024)
#define gigabytes(value) (megabytes(value) * 1024)
#define terabytes(value) (gigabytes(value) * 1024)

#define max(a, b) ((a) > (b) ? (a) : (b))
#define min(a, b) ((a) < (b) ? (a) : (b))
#define abs(a) ((a) < 0 ? -(a) : (a))

#define array_count(a) (sizeof((a)) / sizeof((a)[0]))

typedef uint8_t u8;
typedef uint16_t u16;
typedef uint32_t u32;
typedef uint64_t u64;

typedef int8_t i8;
typedef int16_t i16;
typedef int32_t i32;
typedef int64_t i64;

typedef size_t MemoryIndex; 

typedef float f32;
typedef double f64;


typedef enum { false, true } bool;

typedef struct {
    int placeholder;
} ThreadContext;

/*
 * NOTE(fede): These are platform services that are to be called from the
 * game layer.
 * */

#if HANDMADE_INTERNAL

typedef struct {
    u64 size;
    void *memory;
} DebugReadFileResult;

#define DEBUG_PLATFORM_READ_ENTIRE_FILE(name)                                  \
    DebugReadFileResult name(ThreadContext *thread, char *filename)
typedef DEBUG_PLATFORM_READ_ENTIRE_FILE(DEBUGPlatformReadEntireFile);

#define DEBUG_PLATFORM_FREE_FILE_MEMORY(name)                                  \
    void name(ThreadContext *thread, DebugReadFileResult file_result)
typedef DEBUG_PLATFORM_FREE_FILE_MEMORY(DEBUGPlatformFreeEntireFileMemory);

#define DEBUG_PLATFORM_WRITE_ENTIRE_FILE(name)                                 \
    bool name(ThreadContext *thread, char *filename, u64 size, void *memory)
typedef DEBUG_PLATFORM_WRITE_ENTIRE_FILE(DEBUGPlatformWriteEntireFile);

#endif

/*
 * NOTE(fede): These are game services that are to be called from the
 * platform layer.
 * */

typedef struct {
    u32 width;
    u32 height;
    u32 *data;
} GameDisplayBuffer;

typedef struct {
    u32 sample_count;
    u32 samples_per_second;
    i16 *samples;
} GameSoundOutputBuffer;

typedef struct {
    int tone_hz;
    int tone_volume;
    int wave_period;
    f32 t_sine;
} GameSoundOutput;

typedef struct {
    int half_transition_count;
    bool ended_down;
} GameButton;

typedef struct {
    bool is_connected;
    bool is_analog;
    f32 avg_stick_x;
    f32 avg_stick_y;

    union {
        GameButton buttons[10];
        struct {
            GameButton move_up;
            GameButton move_down;
            GameButton move_left;
            GameButton move_right;

            GameButton button_a;
            GameButton button_b;
            GameButton button_x;
            GameButton button_y;

            GameButton left_shoulder;
            GameButton right_shoulder;

// NOTE(fede): This is the example of HANDMADE_ASSERT that was listed above.
#if HANDMADE_ASSERT
            GameButton _end;
#endif
        };
    };
} GameControllerInput;

typedef struct {
    int x;
    int y;
    int z; // TODO(fede): Set scroll wheel when updating input

    union {
        GameButton buttons[5];
        struct {
            GameButton left;
            GameButton right;
            GameButton middle;
            GameButton thumb1; // thumb - back
            GameButton thumb2; // thumb - forwards

#if HANDMADE_ASSERT
            GameButton _end;
#endif
        };
    };
} MouseInput;

#define HANDMADE_MAX_INPUTS 1 + 4 // 1 keyboard, 4 controllers

typedef struct {
    f32 dt_for_frame;

    MouseInput mouse_input;

    GameControllerInput controllers[HANDMADE_MAX_INPUTS];
} GameInput;

internal inline GameControllerInput *get_game_controller(GameInput *input,
                                                         int controller_index) {
    assert(controller_index < HANDMADE_MAX_INPUTS);
    return &input->controllers[controller_index];
}

typedef struct {
    bool is_initialized;

    MemoryIndex permanent_storage_size;
    void *permanent_storage;

    MemoryIndex transient_storage_size;
    void *transient_storage;

#if HANDMADE_INTERNAL
    DEBUGPlatformReadEntireFile *debug_platform_read_entire_file;
    DEBUGPlatformFreeEntireFileMemory *debug_platform_free_file_memory;
    DEBUGPlatformWriteEntireFile *debug_platform_write_entire_file;
#endif

} GameMemory;

#define GAME_UPDATE_AND_RENDER(name)                                           \
    void name(ThreadContext *thread, GameMemory *memory,                       \
              GameDisplayBuffer *display_buffer, GameInput *input)
typedef GAME_UPDATE_AND_RENDER(GameUpdateAndRender);

extern GAME_UPDATE_AND_RENDER(game_update_and_render);

#define GAME_FILL_SOUND_BUFFER(name)                                           \
    void name(ThreadContext *thread, GameMemory *memory,                       \
              GameSoundOutputBuffer *sound_buffer)
typedef GAME_FILL_SOUND_BUFFER(GameFillSoundBuffer);

extern GAME_FILL_SOUND_BUFFER(game_fill_sound_buffer);

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
    Arena world_arena;
    World *world;
    TilemapPosition player_pos;

    LoadedBitmap backdrop;
    LoadedBitmap hero_head;
    LoadedBitmap hero_cape;
    LoadedBitmap hero_torso;
    LoadedBitmap hero_shadow;
} GameState;

#endif //HANDMADE_H
