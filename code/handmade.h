#pragma once

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

#define PI 3.14159265359

#if HANDMADE_SLOW
#define assert(expression) \
    if (!(expression)) { *(int *)0 = 0; }
#else 
#define assert(expression) 
#endif

#define kilobytes(value) ((value)*1024)
#define megabytes(value) (kilobytes(value) * 1024)
#define gigabytes(value) (megabytes(value) * 1024)
#define terabytes(value) (gigabytes(value) * 1024)

#define max(a, b) (a) > (b) ? (a) : (b)
#define min(a, b) (a) < (b) ? (a) : (b)

#define array_count(a) (sizeof((a)) / sizeof((a)[0]))

typedef uint8_t u8;
typedef uint16_t u16;
typedef uint32_t u32;
typedef uint64_t u64;

typedef int8_t i8;
typedef int16_t i16;
typedef int32_t i32;
typedef int64_t i64;

typedef float f32;
typedef double f64;

typedef enum { false, true } bool;

/*
 * NOTE(fede): These are platform services that are to be called from the
 * game layer.
 * */

#if HANDMADE_INTERNAL 

typedef struct {
    u64 size;
    void *memory;
} DebugReadFileResult;

internal DebugReadFileResult debug_platform_read_entire_file(char *filename);
internal void debug_platform_free_file_memory(DebugReadFileResult file_result);
internal bool debug_platform_write_entire_file(char *filename, u64 size, void *memory);

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
    bool is_analog;

    union {
        GameButton buttons[6];
        struct {
            GameButton button_a;
            GameButton button_b;
            GameButton button_x;
            GameButton button_y;
            GameButton left_shoulder;
            GameButton right_shoulder;
        };
    };

    f32 start_x;
    f32 start_y;

    f32 end_x;
    f32 end_y;

    f32 max_x;
    f32 max_y;

    f32 min_x;
    f32 min_y;
} GameControllerInput;

#define GAME_MAX_INPUTS 4

typedef struct {
    GameControllerInput inputs[GAME_MAX_INPUTS];
} GameInput;

typedef struct {
    bool is_initialized;

    u64 permanent_storage_size;
    void *permanent_storage;

    u64 transient_storage_size;
    void *transient_storage;
} GameMemory;

internal void game_update_and_render(GameMemory *memory,
                                     GameDisplayBuffer *display_buffer,
                                     GameSoundOutputBuffer *audio_buffer,
                                     GameInput *input);

// TODO(fede): the platform layer should not know about the game state, move
//             this definition to another location

typedef struct {
    int x_offset;
    int y_offset;
    int tone_hz;
} GameState;
