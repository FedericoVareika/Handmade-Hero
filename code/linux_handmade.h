#pragma once

#define MAX_CONTROLLERS 4

typedef struct {
    SDL_Renderer *renderer;
    SDL_Texture *texture;
    u32 width;
    u32 height;
    u32 pitch; // NOTE(fede): In bytes
    u32 bits_per_pixel;
    u32 data_capacity;
    u32 *data;
} SDLBackbuffer;

typedef struct {
    u64 last_consumed_from;
    int size;
    int write_cursor;
    int play_cursor;
    i16 *data;
} SDLAudioRingBuffer;

typedef struct {
    int samples_per_second;
    int bytes_per_sample;
    int tone_hz;
    int running_sample_index;
    int latency_sample_count;
    int secondary_buffer_size;
    int safety_bytes;
    int bytes_per_sound_frame;
} SDLSoundOutput;

typedef struct {
    int count;
    SDL_GameController *handles[MAX_CONTROLLERS];
} SDLControllers;

typedef struct {
    int byte_to_lock; 
    int bytes_to_write; 
} SDLSoundWriteMarker;

typedef struct {
    int play_cursor;
    int write_cursor;

    int flip_play_cursor;
    int flip_write_cursor;

    int expected_frame_boundary_byte;
    int expected_now_byte;
    int byte_to_lock;
    int bytes_to_write;
    int target_cursor;
} SDLDebugTimeMarker;
