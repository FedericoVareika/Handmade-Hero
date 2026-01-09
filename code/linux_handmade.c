#include <stdint.h>

#include <sys/mman.h>

#include <fcntl.h>
#include <sys/stat.h>
#include <unistd.h>

#include <SDL2/SDL.h>

#include <math.h>
#include <stdint.h>

#include "handmade.c"
#include "handmade.h"

#define MAX_CONTROLLERS 4
#define GET_BUTTON(handle, button)                                             \
    SDL_GameControllerGetButton(handle, SDL_CONTROLLER_BUTTON_##button)
#define GET_AXIS(handle, axis)                                                 \
    SDL_GameControllerGetAxis(handle, SDL_CONTROLLER_AXIS_##axis)

#if HANDMADE_INTERNAL

internal DebugReadFileResult debug_platform_read_entire_file(char *filename) {
    DebugReadFileResult result = {};

    int fd = open(filename, O_RDONLY);
    if (fd == -1) {
        // handle error
        return (DebugReadFileResult){};
    }

    struct stat stat_;
    if (stat(filename, &stat_) == -1) {
        // handle error
        close(fd);
        return (DebugReadFileResult){};
    }

    assert(sizeof(stat_.st_size) == sizeof(u64));
    result.size = stat_.st_size;

    result.memory = mmap(0, result.size, PROT_READ | PROT_WRITE,
                         MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
    u64 bytes_read = read(fd, result.memory, result.size);
    if (bytes_read != result.size) {
        // handle error
        close(fd);
        return (DebugReadFileResult){};
    }

    close(fd);

    return result;
}

internal void debug_platform_free_file_memory(DebugReadFileResult read_result) {
    if (read_result.memory) {
        munmap(read_result.memory, read_result.size);
    }
}

internal bool debug_platform_write_entire_file(char *filename, u64 size,
                                               void *memory) {
    /*
     * NOTE(fede): When O_CREAT flag is set, a *mode* flag must be set as well.
     *
     *    In this case:
     *
     *         S_IRWXU -- 00700 user (file owner) has read, write, and
     *                    execute permission
     *
     */

    int fd = open(filename, O_RDWR | O_CREAT, S_IRWXU);
    if (fd == -1) {
        // handle error
        return false;
    }

    int bytes_written = write(fd, memory, size);
    if (bytes_written != size) {
        // handle error
        return false;
    }

    close(fd);

    return true;
}

#endif

// TODO(fede): Fill this up
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

internal void linux_resize_backbuffer(SDLBackbuffer *buffer, i32 width,
                                      i32 height) {
    if (buffer->texture) {
        SDL_DestroyTexture(buffer->texture);
    }

    u32 new_size = width * height * buffer->bits_per_pixel / 8;
    if (buffer->data && buffer->data_capacity < new_size) {
        munmap(buffer->data, buffer->data_capacity);
        buffer->data = 0;
    }

    buffer->width = width;
    buffer->height = height;
    buffer->pitch = width * buffer->bits_per_pixel / 8;

    if (!buffer->data) {
        buffer->data = mmap(0, new_size, PROT_READ | PROT_WRITE,
                            MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
        if (!buffer->data) {
            // TODO(fede): allocation failed
        }
        buffer->data_capacity = new_size;
    }

    buffer->texture = SDL_CreateTexture(
        buffer->renderer, SDL_PIXELFORMAT_ARGB8888, SDL_TEXTUREACCESS_STREAMING,
        buffer->width, buffer->height);
}

typedef struct {
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
} SDLSoundOutput;

internal void sdl_audio_callback(void *userdata, u8 *stream, int len) {
    SDLAudioRingBuffer *buffer = (SDLAudioRingBuffer *)userdata;

    int region1_size = len;
    int region2_size = 0;
    if (len > buffer->size - buffer->play_cursor) {
        region1_size = buffer->size - buffer->play_cursor;
        region2_size = len - region1_size;
    }

    memcpy(stream, (u8 *)(buffer->data) + buffer->play_cursor, region1_size);
    memcpy(&stream[region1_size], buffer->data, region2_size);
    buffer->play_cursor = (buffer->play_cursor + len) % buffer->size;
    buffer->write_cursor = (buffer->play_cursor + len) %
                           buffer->size; // + 2048 in handmade penguin
}

internal void sdl_init_audio(SDLSoundOutput *sound_output,
                             SDLAudioRingBuffer *ring_buffer) {
    int buffer_size = sound_output->samples_per_second *
                      sound_output->bytes_per_sample; // 1 second

    ring_buffer->play_cursor = 0;
    ring_buffer->write_cursor = 0;
    ring_buffer->size = buffer_size;
    ring_buffer->data = mmap(0, ring_buffer->size, PROT_READ | PROT_WRITE,
                             MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
    if (!ring_buffer->data) {
        // TODO(fede): allocation failed
    }

    SDL_AudioSpec desired_audio_spec = {};
    desired_audio_spec.freq = sound_output->samples_per_second;
    desired_audio_spec.format = AUDIO_S16LSB; // 16 bit signed little endian
    desired_audio_spec.channels = 2;          // stereo
    desired_audio_spec.samples = 512;
    desired_audio_spec.callback = &sdl_audio_callback;
    desired_audio_spec.userdata = ring_buffer;

    SDL_OpenAudio(&desired_audio_spec, 0);
}

internal void sdl_fill_sound_buffer(SDLAudioRingBuffer *ring_buffer,
                                    SDLSoundOutput *sound_output,
                                    GameSoundOutputBuffer *src_buffer,
                                    int byte_to_lock, int bytes_to_write) {
    i16 *sample_in = src_buffer->samples;

    u8 *region1 = (u8 *)(ring_buffer->data) + byte_to_lock;
    int region1_size = bytes_to_write;

    if (region1_size > ring_buffer->size - byte_to_lock) {
        region1_size = ring_buffer->size - byte_to_lock;
    }

    u8 *region2 = (u8 *)(ring_buffer->data);
    int region2_size = bytes_to_write - region1_size;

    int region1_sample_count = region1_size / sound_output->bytes_per_sample;
    i16 *sample_out = (i16 *)region1;
    for (int sample_index = 0; sample_index < region1_sample_count;
         sample_index++) {
        *sample_out++ = *sample_in++;
        *sample_out++ = *sample_in++;
        sound_output->running_sample_index++;
    }

    int region2_sample_count = region2_size / sound_output->bytes_per_sample;
    sample_out = (i16 *)region2;
    for (int sample_index = 0; sample_index < region2_sample_count;
         sample_index++) {
        *sample_out++ = *sample_in++;
        *sample_out++ = *sample_in++;
        sound_output->running_sample_index++;
    }
}

internal void sdl_handle_button(GameButton *old_state, bool is_down,
                                GameButton *new_state) {
    new_state->ended_down = is_down;
    new_state->half_transition_count = old_state->ended_down != is_down ? 1 : 0;
}

internal bool handle_event(SDL_Event *event, SDLBackbuffer *backbuffer) {
    switch (event->type) {
    case SDL_QUIT: {
        return true;
    } break;
    case SDL_WINDOWEVENT: {
        SDL_WindowEvent window_event = event->window;
        switch (window_event.event) {
        case SDL_WINDOWEVENT_RESIZED: {
            linux_resize_backbuffer(backbuffer, window_event.data1,
                                    window_event.data2);
        } break;
        }
    } break;
    }
    return false;
}

internal bool linux_handle_events(SDLBackbuffer *backbuffer) {
    int should_quit = false;

    SDL_Event event;
    while (SDL_PollEvent(&event)) {
        if (handle_event(&event, backbuffer))
            should_quit = true;
    }

    return should_quit;
}

int main(void) {
    if (!SDL_Init(SDL_INIT_VIDEO | SDL_INIT_GAMECONTROLLER | SDL_INIT_AUDIO)) {
        // TODO(fede): SDL did not work!!
    }

    u32 width = 1280;
    u32 height = 720;
    SDL_Window *window = SDL_CreateWindow(
        "Handmade Hero", SDL_WINDOWPOS_UNDEFINED, SDL_WINDOWPOS_UNDEFINED,
        width, height, SDL_WINDOW_RESIZABLE);

    // TODO(fede): Check VSYNC
    SDL_Renderer *renderer =
        SDL_CreateRenderer(window, -1, SDL_RENDERER_SOFTWARE);

    SDLBackbuffer backbuffer = {};
    backbuffer.renderer = renderer;
    backbuffer.bits_per_pixel = 32;
    linux_resize_backbuffer(&backbuffer, width, height);

    SDL_GameController *controller_handles[MAX_CONTROLLERS] = {};

    int max_joysticks = SDL_NumJoysticks();
    int controller_index = 0;
    for (int joystick_index = 0; joystick_index < max_joysticks;
         joystick_index++) {
        if (!SDL_IsGameController(joystick_index))
            continue;

        if (controller_index >= MAX_CONTROLLERS)
            break;

        controller_handles[controller_index++] =
            SDL_GameControllerOpen(joystick_index);
    }

    SDLSoundOutput sound_output = {};
    sound_output.samples_per_second = 48000;
    sound_output.tone_hz = 512;
    sound_output.running_sample_index = 0;
    sound_output.bytes_per_sample = sizeof(i16) * 2;
    sound_output.latency_sample_count = sound_output.samples_per_second / 15;

    SDLAudioRingBuffer ring_buffer = {};
    sdl_init_audio(&sound_output, &ring_buffer);

    GameSoundOutputBuffer game_sound_buffer = {};
    game_sound_buffer.samples =
        mmap(0, ring_buffer.size, PROT_READ | PROT_WRITE,
             MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);

    SDL_PauseAudio(0);

#if HANDMADE_INTERNAL
    void *base_address = (void *)terabytes((u64)2);
#else
    void *base_address = 0;
#endif

    GameMemory game_memory = {};
    game_memory.permanent_storage_size = megabytes(64);
    game_memory.transient_storage_size = gigabytes((u64)4);
    {
        u64 total_storage_size = game_memory.permanent_storage_size +
                                 game_memory.transient_storage_size;
        void *total_storage =
            mmap(base_address, total_storage_size, PROT_READ | PROT_WRITE,
                 MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);

        game_memory.permanent_storage = total_storage;
        game_memory.transient_storage =
            (u8 *)total_storage + game_memory.permanent_storage_size;
    }

    if (!game_sound_buffer.samples || !game_memory.permanent_storage ||
        !game_memory.transient_storage) {
        // TODO(fede): allocation failed
        return 0;
    }

    GameInput old_input = {};
    GameInput new_input = {};

    while (!linux_handle_events(&backbuffer)) {
        // NOTE(fede): controller input
        // TODO(fede): make this platform independent
        {
            int max_controller_count =
                min(MAX_CONTROLLERS, array_count(new_input.inputs));
            for (int controller_index = 0;
                 controller_index < max_controller_count; controller_index++) {
                GameControllerInput *old_controller_state =
                    &old_input.inputs[controller_index];
                GameControllerInput *new_controller_state =
                    &new_input.inputs[controller_index];

                new_controller_state->is_analog = true;

                SDL_GameController *controller_handle =
                    controller_handles[controller_index];
                if (controller_handle == 0 ||
                    !SDL_GameControllerGetAttached(controller_handle))
                    continue;

                // TODO(fede): see if this is better with an X macro
                sdl_handle_button(&old_controller_state->button_a,
                                  GET_BUTTON(controller_handle, A),
                                  &new_controller_state->button_a);
                sdl_handle_button(&old_controller_state->button_b,
                                  GET_BUTTON(controller_handle, B),
                                  &new_controller_state->button_b);
                sdl_handle_button(&old_controller_state->button_x,
                                  GET_BUTTON(controller_handle, X),
                                  &new_controller_state->button_x);
                sdl_handle_button(&old_controller_state->button_y,
                                  GET_BUTTON(controller_handle, Y),
                                  &new_controller_state->button_y);
                sdl_handle_button(&old_controller_state->button_y,
                                  GET_BUTTON(controller_handle, Y),
                                  &new_controller_state->button_y);
                sdl_handle_button(&old_controller_state->left_shoulder,
                                  GET_BUTTON(controller_handle, LEFTSHOULDER),
                                  &new_controller_state->left_shoulder);
                sdl_handle_button(&old_controller_state->right_shoulder,
                                  GET_BUTTON(controller_handle, RIGHTSHOULDER),
                                  &new_controller_state->right_shoulder);

                i16 left_stick_y = GET_AXIS(controller_handle, LEFTY);
                i16 left_stick_x = GET_AXIS(controller_handle, LEFTX);

                // TODO(fede): deadzone
                {
                    f32 x;
                    f32 y;
                    if (left_stick_x < 0)
                        x = (f32)left_stick_x / (f32)-INT16_MIN;
                    else
                        x = (f32)left_stick_x / (f32)INT16_MAX;

                    if (left_stick_y < 0)
                        y = (f32)left_stick_y / (f32)-INT16_MIN;
                    else
                        y = (f32)left_stick_y / (f32)INT16_MAX;

                    new_controller_state->start_x = old_controller_state->end_x;
                    new_controller_state->start_y = old_controller_state->end_y;

                    new_controller_state->min_x =
                        min(new_controller_state->start_x, x);
                    new_controller_state->min_y =
                        min(new_controller_state->start_y, y);

                    new_controller_state->max_x =
                        max(new_controller_state->start_x, x);
                    new_controller_state->max_y =
                        max(new_controller_state->start_y, y);

                    new_controller_state->end_x = x;
                    new_controller_state->end_y = y;
                }
            }
        }

        // NOTE(fede): Sound buffer
        SDL_LockAudio();

        int byte_to_lock = (sound_output.running_sample_index *
                            sound_output.bytes_per_sample) %
                           ring_buffer.size;

        int target_cursor =
            (ring_buffer.play_cursor + (sound_output.latency_sample_count *
                                        sound_output.bytes_per_sample)) %
            ring_buffer.size;

        int bytes_to_write;
        if (byte_to_lock > target_cursor) {
            bytes_to_write = ring_buffer.size - byte_to_lock;
            bytes_to_write += target_cursor;
        } else {
            bytes_to_write = target_cursor - byte_to_lock;
        }

        SDL_UnlockAudio();

        game_sound_buffer.samples_per_second = sound_output.samples_per_second;
        game_sound_buffer.sample_count =
            bytes_to_write / sound_output.bytes_per_sample;

        GameDisplayBuffer game_buffer = {};
        game_buffer.data = backbuffer.data;
        game_buffer.height = backbuffer.height;
        game_buffer.width = backbuffer.width;

        game_update_and_render(&game_memory, &game_buffer, &game_sound_buffer,
                               &new_input);

        sdl_fill_sound_buffer(&ring_buffer, &sound_output, &game_sound_buffer,
                              byte_to_lock, bytes_to_write);

        {
            SDL_RenderClear(renderer);

            if (SDL_UpdateTexture(backbuffer.texture, 0,
                                  (void *)backbuffer.data,
                                  backbuffer.pitch) <= 0)
                ; // TODO(fede): Update texture failure
            if (SDL_RenderCopy(renderer, backbuffer.texture, 0, 0) <= 0)
                ; // TODO(fede): Render texture failure

            SDL_RenderPresent(renderer);
        }

        // TODO(fede): This is probably copying the entire struct 3 times,
        //             check if these should be pointers instead (like casey
        //             did)
        GameInput aux_input = old_input;
        old_input = new_input;
        new_input = aux_input;
    }

    SDL_CloseAudio();
    SDL_Quit();

    return 0;
}
