#ifndef HANDMADE_PLATFORM_H
#define HANDMADE_PLATFORM_H

/*
 * NOTE(fede): Compilers
*/
#ifdef __GNUC__
    #ifndef __clang__
        #define COMPILER_GCC 
    #else 
        #define COMPILER_CLANG 
    #endif //__clang__
#endif //__GNUC__

#include <stdint.h>
#include <stdio.h> // NOTE(fede): for size_t type

#define internal static
#define global static
#define local_persist static

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

            GameButton _end;
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

            GameButton _end;
        };
    };
} MouseInput;

#define HANDMADE_MAX_INPUTS 1 + 4 // 1 keyboard, 4 controllers

typedef struct {
    f32 dt_for_frame;

    MouseInput mouse_input;

    GameControllerInput controllers[HANDMADE_MAX_INPUTS];
} GameInput;

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

#endif //HANDMADE_PLATFORM_H
