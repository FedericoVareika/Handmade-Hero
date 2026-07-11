#ifndef LINUX_HANDMADE_H
#define LINUX_HANDMADE_H

#include <fcntl.h>

#include <time.h>
#include <sys/stat.h>
#include <SDL2/SDL.h>

#define MAX_CONTROLLERS 4

typedef struct {
    bool is_valid; 
    char *filename;
    struct timespec last_modified;
    void *handle;

    GameUpdateAndRender *update_and_render;
    GameFillSoundBuffer *fill_sound_buffer;
} GameLib;

typedef struct {
    SDL_Window *window;
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

#include <limits.h>
#define LINUX_FILEPATH_MAX_COUNT PATH_MAX

typedef struct {
    u64 game_memory_size;
    void *game_memory_block; 

    int recording_fd;
    int recording_index; 

    int playback_fd;
    int playing_index; 

    u64 bytes_written;
    u64 bytes_read;
    u64 memory_map_size;
    void *memory_map;

    char exe_filename[LINUX_FILEPATH_MAX_COUNT];
    char *one_past_last_slash;
} LinuxState;

#endif // LINUX_HANDMADE_H
