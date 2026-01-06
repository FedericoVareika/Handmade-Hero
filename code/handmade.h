#pragma once

#include <math.h>
#include <stdint.h>

#define internal static
#define global static

#define PI 3.14159265359

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

void game_update_and_render(GameDisplayBuffer *display_buffer, i32 x_offset,
                            i32 y_offset, GameSoundOutputBuffer *audio_buffer,
                            int tone_hz);
