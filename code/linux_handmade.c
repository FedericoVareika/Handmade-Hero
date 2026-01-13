#include <stdint.h>

#include <sys/mman.h>
#include <time.h>

#include <fcntl.h>
#include <sys/stat.h>
#include <unistd.h>

#include <SDL2/SDL.h>

#include <math.h>
#include <stdint.h>

#include "handmade.c"
#include "handmade.h"

#include "linux_handmade.h"

#define GET_BUTTON(handle, button)                                             \
    SDL_GameControllerGetButton(handle, SDL_CONTROLLER_BUTTON_##button)
#define GET_AXIS(handle, axis)                                                 \
    SDL_GameControllerGetAxis(handle, SDL_CONTROLLER_AXIS_##axis)

#define frames_of_audio_latency 2

global bool game_running = true;
global u64 performance_frequency;

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

    u64 bytes_written = write(fd, memory, size);
    if (bytes_written != size) {
        // handle error
        return false;
    }

    close(fd);

    return true;
}

#endif

internal void sdl_resize_backbuffer(SDLBackbuffer *buffer, i32 width,
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

    sound_output->secondary_buffer_size = buffer_size;

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
    // desired_audio_spec.samples =
    //     desired_audio_spec.freq / (desired_audio_spec.channels * 30);
    // desired_audio_spec.samples /= 2;
    desired_audio_spec.samples = 512;
    desired_audio_spec.callback = &sdl_audio_callback;
    desired_audio_spec.userdata = (void *)ring_buffer;

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

internal void sdl_handle_keyboard_key(GameButton *new_state, bool is_down) {
    assert(new_state->ended_down != is_down);
    new_state->ended_down = is_down;
    new_state->half_transition_count++;
}

internal void sdl_handle_button(GameButton *old_state, bool is_down,
                                GameButton *new_state) {
    new_state->ended_down = is_down;
    new_state->half_transition_count = old_state->ended_down != is_down ? 1 : 0;
}

internal f32 sdl_normalize_stick(i16 stick_pos, i16 deadzone) {
    if (abs(stick_pos) <= 8000)
        stick_pos = 0;

    f32 result;
    if (stick_pos < 0)
        result = (f32)stick_pos / (f32)-INT16_MIN;
    else
        result = (f32)stick_pos / (f32)INT16_MAX;

    return result;
}

internal void sdl_update_controller_sticks(GameControllerInput *old_state,
                                           GameControllerInput *new_state,
                                           f32 x, f32 y) {
    // TODO(fede): maybe do a move deadzone? It is just normal deadzone right
    //             now. Dashing could be extra sensitive if this is left alone.
    f32 threshold = 0;
    sdl_handle_button(&old_state->move_right, x > threshold,
                      &new_state->move_right);
    sdl_handle_button(&old_state->move_left, x < -threshold,
                      &new_state->move_left);
    sdl_handle_button(&old_state->move_up, y < -threshold, &new_state->move_up);
    sdl_handle_button(&old_state->move_down, y > threshold,
                      &new_state->move_down);

    new_state->avg_stick_x = x;
    new_state->avg_stick_y = y;
}

internal void sdl_remove_controller(SDLControllers *controllers,
                                    SDL_GameController *handle) {
    bool controller_exists = false;

    int controller_index = 0;
    while (controllers->handles[controller_index] != handle) {
        controller_index++;
    }

    assert(controller_index < MAX_CONTROLLERS);

    while (controller_index < MAX_CONTROLLERS) {
        controllers->handles[controller_index] =
            controllers->handles[controller_index + 1];
        controller_index++;
    }

    controllers->count--;
}

internal bool handle_event(SDL_Event *event, SDLBackbuffer *backbuffer,
                           SDLControllers *controllers,
                           GameControllerInput *keyboard_controller) {
    switch (event->type) {
    case SDL_QUIT: {
        return true;
    } break;
    case SDL_WINDOWEVENT: {
        SDL_WindowEvent window_event = event->window;
        switch (window_event.event) {
        case SDL_WINDOWEVENT_RESIZED: {
            sdl_resize_backbuffer(backbuffer, window_event.data1,
                                  window_event.data2);
        } break;
        }
    } break;
    case SDL_CONTROLLERDEVICEADDED: {
        SDL_ControllerDeviceEvent controller_event = event->cdevice;
        int joystick_index = controller_event.which;

        if (controllers->count >= MAX_CONTROLLERS) {
            assert(controllers->count == MAX_CONTROLLERS);
            break;
        }

        if (!SDL_IsGameController(joystick_index))
            break;

        controllers->handles[controllers->count++] =
            SDL_GameControllerOpen(joystick_index);

        printf("Controller device added\n");
    } break;
    case SDL_CONTROLLERDEVICEREMOVED: {
        SDL_ControllerDeviceEvent controller_event = event->cdevice;
        SDL_GameController *controller_handle =
            SDL_GameControllerFromInstanceID(controller_event.which);

        assert(controller_handle);

        sdl_remove_controller(controllers, controller_handle);
        SDL_GameControllerClose(controller_handle);

        printf("Controller device removed\n");
    } break;
    case SDL_KEYDOWN:
    case SDL_KEYUP: {
        SDL_KeyboardEvent key_event = event->key;

        // STUDY(fede): Maybe for UI, i would want key repeat (typing overall).
        //              If i do not, i could remove key repeat entirely, but i
        //              am not sure that is something i want.
        if (key_event.repeat)
            break;

        switch (key_event.keysym.sym) {
        case SDLK_UP:
        case SDLK_w: {
            sdl_handle_keyboard_key(&keyboard_controller->move_up,
                                    key_event.type == SDL_KEYDOWN);
        } break;

        case SDLK_LEFT:
        case SDLK_a: {
            sdl_handle_keyboard_key(&keyboard_controller->move_left,
                                    key_event.type == SDL_KEYDOWN);
        } break;

        case SDLK_DOWN:
        case SDLK_s: {
            sdl_handle_keyboard_key(&keyboard_controller->move_down,
                                    key_event.type == SDL_KEYDOWN);
        } break;

        case SDLK_RIGHT:
        case SDLK_d: {
            sdl_handle_keyboard_key(&keyboard_controller->move_right,
                                    key_event.type == SDL_KEYDOWN);
        } break;

        case SDLK_q: {
            sdl_handle_keyboard_key(&keyboard_controller->left_shoulder,
                                    key_event.type == SDL_KEYDOWN);
        } break;

        case SDLK_e: {
            sdl_handle_keyboard_key(&keyboard_controller->right_shoulder,
                                    key_event.type == SDL_KEYDOWN);
        } break;

        case SDLK_RETURN: {
            game_running = false;
        } break;
        }
    } break;
    }

    return false;
}

internal bool sdl_handle_events(SDLBackbuffer *backbuffer,
                                SDLControllers *controllers,
                                GameControllerInput *keyboard_controller) {
    int should_quit = false;

    SDL_Event event;
    while (SDL_PollEvent(&event)) {
        if (handle_event(&event, backbuffer, controllers, keyboard_controller))
            should_quit = true;
    }

    return should_quit;
}

internal inline f64 sdl_get_seconds_elapsed(u64 start_counter,
                                            u64 end_counter) {
    return (f64)(end_counter - start_counter) / performance_frequency;
}

internal void sdl_sleep_to_target(u64 last_counter, f64 target_seconds) {
    f64 seconds_elapsed_for_frame =
        sdl_get_seconds_elapsed(last_counter, SDL_GetPerformanceCounter());

    u32 ms_to_sleep =
        (u32)(1000.0f * (target_seconds - seconds_elapsed_for_frame));

    SDL_Delay(ms_to_sleep);
}

internal void linux_sleep_to_target(u64 last_counter, f64 target_seconds) {
    f64 seconds_elapsed_for_frame;

    struct timespec sleep_time = {};
    struct timespec remaining_time = {};
    do {
        seconds_elapsed_for_frame =
            sdl_get_seconds_elapsed(last_counter, SDL_GetPerformanceCounter());

        // NOTE(fede): truncate the amount of ms to sleep
        f64 ms_to_sleep =
            (f64)(u64)(1000.0f * (target_seconds - seconds_elapsed_for_frame));

        // NOTE(fede): give 100,000 ns of leeway
        ms_to_sleep -= 0.1f;

        u64 nsec_to_sleep = (u64)(ms_to_sleep * 1000000.0f);

        sleep_time.tv_sec = 0;
        sleep_time.tv_nsec = nsec_to_sleep;
    } while (nanosleep(&sleep_time, &remaining_time) == -1);

    seconds_elapsed_for_frame =
        sdl_get_seconds_elapsed(last_counter, SDL_GetPerformanceCounter());

    if (seconds_elapsed_for_frame <= target_seconds) {
        while (seconds_elapsed_for_frame < target_seconds)
            seconds_elapsed_for_frame = sdl_get_seconds_elapsed(
                last_counter, SDL_GetPerformanceCounter());
    } else {
        // TODO(fede): ERROR -- missed target frame rate
        printf("Missed target frame rate!\n");
    }
}

internal void sdl_debug_draw_vertical(SDLBackbuffer *backbuffer, int x, int top,
                                      int bottom, u32 color) {
    for (int y = top; y < bottom; y++) {
        u8 *pixel_pos = (u8 *)backbuffer->data +
                        x * backbuffer->bits_per_pixel / 8 +
                        y * backbuffer->pitch;
        u32 *pixel = (u32 *)pixel_pos;
        *pixel = *pixel | color;
    }
}

internal void sdl_debug_draw_horizontal(SDLBackbuffer *backbuffer, int y,
                                        int left, int right, u32 color) {
    u32 *pixel_out =
        (u32 *)((u8 *)backbuffer->data + left * backbuffer->bits_per_pixel / 8 +
                y * backbuffer->pitch);
    for (int x = left; x < right; x++) {
        *pixel_out++ = *pixel_out | color;
    }
}

internal void sdl_debug_draw_cursor(SDLBackbuffer *backbuffer, int cursor,
                                    int top, int bottom, f32 coef, u32 color) {
    int x = (int)((f32)cursor * coef);
    sdl_debug_draw_vertical(backbuffer, x, top, bottom, color);
}

internal void sdl_debug_sync_display(SDLBackbuffer *backbuffer,
                                     SDLSoundOutput *sound_output,
                                     int debug_time_marker_count,
                                     SDLDebugTimeMarker *debug_time_markers) {
    int pad_x = 15;
    int pad_y = 15;

    int top = pad_y;
    int bottom = backbuffer->height - pad_y;

    f32 coef = (f32)(backbuffer->width - 2 * pad_x) /
               (f32)sound_output->secondary_buffer_size;

    // Draw frame boundries
    {
        int x = 0;
        while (x < sound_output->secondary_buffer_size) {
            // sdl_debug_draw_cursor(backbuffer, x, 0, bottom + pad_y, coef,
            //                       0xFFFFFF);
            x += sound_output->samples_per_second *
                 sound_output->bytes_per_sample / 30;
        }
    }

    for (int i = 0; i < debug_time_marker_count; i++) {
        SDLDebugTimeMarker marker = debug_time_markers[i];

        sdl_debug_draw_cursor(backbuffer, marker.ask_cursor, top, bottom, coef,
                              0x00aa0000);

        sdl_debug_draw_cursor(backbuffer, marker.play_cursor, top, bottom, coef,
                              0x0000FF00);

        sdl_debug_draw_cursor(backbuffer, marker.write_cursor, top, bottom,
                              coef, 0x000000FF);

        // sdl_debug_draw_cursor(backbuffer, marker.byte_to_lock, top,
        //                       bottom, coef, 0x00FF0000);

        // sdl_debug_draw_vertical(
        //     backbuffer, x_offset + pad_x,
        //     max(bottom - (int)((f32)marker.queued_bytes_end * coef), pad_y),
        //     bottom, 0x000000FF);
    }
}

int main(void) {

    if (!SDL_Init(SDL_INIT_VIDEO | SDL_INIT_GAMECONTROLLER | SDL_INIT_AUDIO)) {
        // TODO(fede): SDL did not work!!
    }

    performance_frequency = SDL_GetPerformanceFrequency();

    u32 width = 1280;
    u32 height = 720;
    SDL_Window *window = SDL_CreateWindow(
        "Handmade Hero", SDL_WINDOWPOS_UNDEFINED, SDL_WINDOWPOS_UNDEFINED,
        width, height, SDL_WINDOW_RESIZABLE);

    // TODO(fede): Check VSYNC
    SDL_Renderer *renderer = SDL_CreateRenderer(
        window, -1, SDL_RENDERER_SOFTWARE | SDL_RENDERER_PRESENTVSYNC);

    SDLBackbuffer backbuffer = {};
    backbuffer.renderer = renderer;
    backbuffer.bits_per_pixel = 32;
    sdl_resize_backbuffer(&backbuffer, width, height);

    // NOTE(fede): get highest display refresh rate
    int display_index = SDL_GetWindowDisplayIndex(window);
    int n_display_modes = SDL_GetNumDisplayModes(display_index);

    SDL_DisplayMode mode = {};
    SDL_GetDisplayMode(display_index, 0, &mode); // highest

    /*
     * STUDY(fede): This is 144hz for my machine and probably a lot more.
     *              However, we are doing software rendering, so ~30hz is
     *              probably the goal.
     *
     *              ** Investigate ways to chose FPS reliably. **
     */

    int refresh_rate = mode.refresh_rate;

    // TODO(fede): Remove overwrite and chose frame rate reliably.
    int game_update_rate = 30;
    f32 target_seconds_per_frame = 1.0f / (f32)game_update_rate;

    SDLControllers controllers = {};

    SDLSoundOutput sound_output = {};
    sound_output.samples_per_second = 48000;
    sound_output.tone_hz = 512;
    sound_output.running_sample_index = 0;
    sound_output.bytes_per_sample = sizeof(i16) * 2;

    // NOTE(fede): 1 frame of audio latency
    sound_output.latency_sample_count = frames_of_audio_latency *
                                        sound_output.samples_per_second /
                                        game_update_rate;

    int samples_per_game_frame =
        sound_output.samples_per_second / game_update_rate;

    SDLAudioRingBuffer ring_buffer = {};

    sdl_init_audio(&sound_output, &ring_buffer);

    bool audio_is_paused = true;

    GameSoundOutputBuffer game_sound_buffer = {};

    // NOTE(fede): allocate 1 second
    game_sound_buffer.samples =
        mmap(0, sound_output.samples_per_second * sound_output.bytes_per_sample,
             PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);

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

    u64 last_counter = SDL_GetPerformanceCounter();

#if HANDMADE_INTERNAL
    u32 debug_time_marker_index = 0;
    SDLDebugTimeMarker debug_time_markers[23] = {};
#endif

    int byte_to_lock;
    int bytes_to_write;

#if 0
    {
        SDL_PauseAudioDevice(1, false);
        int bytes_to_queue = 1000000;
        SDL_QueueAudio(1, malloc(bytes_to_queue), bytes_to_queue);

        int queued_bytes_before = bytes_to_queue;
        int queued_bytes_after = bytes_to_queue;
        while (queued_bytes_after) {
            queued_bytes_after = SDL_GetQueuedAudioSize(1);
            if (queued_bytes_after != queued_bytes_before) {
                printf("Queued bytes changed: %d\n", queued_bytes_after);
                queued_bytes_before = queued_bytes_after;
            }
        }
    }
#endif

    while (game_running) {
        GameControllerInput *old_keyboard_controller =
            get_game_controller(&old_input, 0);
        GameControllerInput *new_keyboard_controller =
            get_game_controller(&new_input, 0);
        *new_keyboard_controller = (GameControllerInput){};
        new_keyboard_controller->is_connected = true;
        for (u32 i = 0; i < array_count(new_keyboard_controller->buttons);
             i++) {
            new_keyboard_controller->buttons[i].ended_down =
                old_keyboard_controller->buttons[i].ended_down;
        }

        if (sdl_handle_events(&backbuffer, &controllers,
                              new_keyboard_controller)) {
            game_running = false;
        }

        // NOTE(fede): controller input
        {
            int max_controller_count =
                min(MAX_CONTROLLERS, HANDMADE_MAX_INPUTS - 1);

            for (int sdl_controller_index = 0;
                 sdl_controller_index < controllers.count;
                 sdl_controller_index++) {

                int our_controller_index = sdl_controller_index + 1;
                GameControllerInput *old_controller_state =
                    get_game_controller(&old_input, our_controller_index);
                GameControllerInput *new_controller_state =
                    get_game_controller(&new_input, our_controller_index);

                SDL_GameController *controller_handle =
                    controllers.handles[sdl_controller_index];

                assert(controller_handle != 0);
                assert(SDL_GameControllerGetAttached(controller_handle));

                new_controller_state->is_connected = true;
                new_controller_state->is_analog = true;

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

                {
                    // NOTE: Stick is said to be centered ~8000, this is
                    //       specified in the SDL Wiki:
                    //       https://wiki.libsdl.org/SDL2/SDL_GameControllerAxis
                    f32 x = sdl_normalize_stick(left_stick_x, 8000);
                    f32 y = sdl_normalize_stick(left_stick_y, 8000);

                    if (GET_BUTTON(controller_handle, DPAD_LEFT))
                        x = -1.0f;
                    if (GET_BUTTON(controller_handle, DPAD_RIGHT))
                        x = 1.0f;
                    if (GET_BUTTON(controller_handle, DPAD_UP))
                        y = -1.0f;
                    if (GET_BUTTON(controller_handle, DPAD_DOWN))
                        y = 1.0f;

                    sdl_update_controller_sticks(old_controller_state,
                                                 new_controller_state, x, y);
                }
            }
        }

        int play_cursor;
        int write_cursor;
        int ask_cursor;

        SDL_LockAudio();

        play_cursor = ring_buffer.play_cursor;
        write_cursor = ring_buffer.write_cursor;

        int ask_seconds =
            sdl_get_seconds_elapsed(last_counter, SDL_GetPerformanceCounter());
        ask_cursor = (int)((f32)sound_output.samples_per_second * ask_seconds);

        int play_frame_cursor =
            play_cursor / (sound_output.samples_per_second / 30);
        // ask_cursor += play_frame_cursor * (sound_output.samples_per_second / 30);
        ask_cursor = play_frame_cursor * (sound_output.samples_per_second / 30);

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

        // TODO(fede): This is probably copying the entire struct 3 times,
        //             check if these should be pointers instead (like casey
        //             did)
        GameInput aux_input = old_input;
        old_input = new_input;
        new_input = aux_input;

        u64 end_counter = SDL_GetPerformanceCounter();

        f64 work_seconds_elapsed =
            sdl_get_seconds_elapsed(last_counter, end_counter);

        linux_sleep_to_target(last_counter, target_seconds_per_frame);

        f64 seconds_elapsed_for_frame =
            sdl_get_seconds_elapsed(last_counter, SDL_GetPerformanceCounter());

        last_counter = SDL_GetPerformanceCounter();

#if 0
        f64 ms_per_frame = seconds_elapsed_for_frame * 1000;
        f64 fps = 1000.0f / ms_per_frame;

        printf("%.02fms/f, %.02ffps \n", ms_per_frame, fps);
#endif

        {
#if HANDMADE_INTERNAL
            sdl_debug_sync_display(&backbuffer, &sound_output,
                                   array_count(debug_time_markers),
                                   debug_time_markers);
#endif

            SDL_RenderClear(renderer);

            if (SDL_UpdateTexture(backbuffer.texture, 0,
                                  (void *)backbuffer.data,
                                  backbuffer.pitch) <= 0) {
                // TODO(fede): Update texture failure
            }

            if (SDL_RenderCopy(renderer, backbuffer.texture, 0, 0) <= 0) {
                // TODO(fede): Render texture failure
            }

            SDL_RenderPresent(renderer);
        }

#if HANDMADE_INTERNAL
        if (!audio_is_paused) {
            debug_time_markers[debug_time_marker_index++] =
                (SDLDebugTimeMarker){
                    .play_cursor = play_cursor,
                    .write_cursor = write_cursor,
                    .ask_cursor = ask_cursor,
                };
            if (debug_time_marker_index >= array_count(debug_time_markers)) {
                debug_time_marker_index = 0;
            }
        }
#endif

        if (audio_is_paused) {
            SDL_PauseAudioDevice(1, false);
            audio_is_paused = false;
        }
    }

    SDL_CloseAudio();
    SDL_Quit();

    return 0;
}
