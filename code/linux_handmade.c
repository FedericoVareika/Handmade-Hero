#include <SDL2/SDL.h>

#include "handmade.h"

#include "linux_handmade.h"

#include <sys/mman.h>

#include <fcntl.h>
#include <unistd.h>

#include <dlfcn.h>

#include <errno.h>

#define GET_BUTTON(handle, button)                                             \
    SDL_GameControllerGetButton(handle, SDL_CONTROLLER_BUTTON_##button)
#define GET_AXIS(handle, axis)                                                 \
    SDL_GameControllerGetAxis(handle, SDL_CONTROLLER_AXIS_##axis)

#define frames_of_audio_latency 2

global bool global_game_running = true;
global bool global_pause = false;
global u64 performance_frequency;

internal inline f64 sdl_get_seconds_elapsed(u64 start_counter,
                                            u64 end_counter) {
    return (f64)(end_counter - start_counter) / performance_frequency;
}

#if HANDMADE_INTERNAL
global SDLDebugTimeMarker debug_time_marker = {};

internal DEBUG_PLATFORM_READ_ENTIRE_FILE(debug_platform_read_entire_file) {
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

internal DEBUG_PLATFORM_FREE_FILE_MEMORY(debug_platform_free_file_memory) {
    if (file_result.memory) {
        munmap(file_result.memory, file_result.size);
    }
}

internal DEBUG_PLATFORM_WRITE_ENTIRE_FILE(debug_platform_write_entire_file) {
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

internal int string_len(char *str) {
    int result = 0;
    while (*str++)
        result++;
    return result;
}

internal void cat_strings(int source_a_count, char *source_a,
                          int source_b_count, char *source_b, int dest_count,
                          char *dest) {
    for (int i = 0; i < source_a_count; i++) {
        *dest++ = *source_a++;
    }

    for (int i = 0; i < source_b_count; i++) {
        *dest++ = *source_b++;
    }

    *dest++ = 0;
}

internal void linux_get_exe_path(LinuxState *state) {
    int filename_len = readlink("/proc/self/exe", state->exe_filename,
                                array_count(state->exe_filename));

    state->one_past_last_slash = state->exe_filename + filename_len;
    for (char *scan = state->exe_filename; *scan; scan++) {
        if (*scan == '/') {
            state->one_past_last_slash = scan + 1;
        }
    }
}

internal void linux_build_global_filename_at_exe_location(LinuxState *state,
                                                          char *dest,
                                                          char *filename) {
    cat_strings(state->one_past_last_slash - state->exe_filename,
                state->exe_filename, string_len(filename), filename,
                LINUX_FILEPATH_MAX_COUNT, dest);
}

#if 1
internal void sdl_reset_renderer(SDLBackbuffer *buffer) {
    if (buffer->renderer) {
        SDL_DestroyRenderer(buffer->renderer);
    }
    buffer->renderer = SDL_CreateRenderer(
        buffer->window, -1, SDL_RENDERER_PRESENTVSYNC | SDL_RENDERER_SOFTWARE);

    if (buffer->texture) {
        SDL_DestroyTexture(buffer->texture);
    }
    buffer->texture = SDL_CreateTexture(
        buffer->renderer, SDL_PIXELFORMAT_ARGB8888, SDL_TEXTUREACCESS_STREAMING,
        buffer->width, buffer->height);
}

#else

internal void sdl_resize_backbuffer(SDLBackbuffer *buffer, i32 width,
                                    i32 height) {
    if (buffer->texture) {
        SDL_DestroyTexture(buffer->texture);
    }

    if (buffer->renderer) {
        SDL_DestroyRenderer(buffer->renderer);
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

    buffer->renderer = SDL_CreateRenderer(
        buffer->window, -1, SDL_RENDERER_PRESENTVSYNC | SDL_RENDERER_SOFTWARE);
    buffer->texture = SDL_CreateTexture(
        buffer->renderer, SDL_PIXELFORMAT_ARGB8888, SDL_TEXTUREACCESS_STREAMING,
        buffer->width, buffer->height);
}
#endif

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

    buffer->last_consumed_from = SDL_GetPerformanceCounter();
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
                                    SDLSoundWriteMarker write_marker) {
    i16 *sample_in = src_buffer->samples;

    u8 *region1 = (u8 *)(ring_buffer->data) + write_marker.byte_to_lock;
    int region1_size = write_marker.bytes_to_write;

    if (region1_size > ring_buffer->size - write_marker.byte_to_lock) {
        region1_size = ring_buffer->size - write_marker.byte_to_lock;
    }

    u8 *region2 = (u8 *)(ring_buffer->data);
    int region2_size = write_marker.bytes_to_write - region1_size;

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
    // NOTE(fede): This used to break when pressing both 'w' and up at the same
    //             time, or when pausing the game whilst pressing down a key,
    //             later unpausing and pressing it again.
    //
    // assert(new_state->ended_down != is_down);

    if (new_state->ended_down == is_down)
        return;

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

internal void linux_get_input_state_filepath(LinuxState *state, char *dest,
                                             int index) {
    assert(index < 9 && index >= 0);
    char input_state_filename[] = "_.hmi";
    input_state_filename[0] = '0' + index;

    linux_build_global_filename_at_exe_location(state, dest,
                                                input_state_filename);
}

internal void linux_begin_recording_input(LinuxState *state,
                                          int recording_index) {
    memcpy(state->memory_map, state->game_memory_block,
           state->game_memory_size);

    state->bytes_written = state->game_memory_size;
    state->recording_index = recording_index;
    state->playing_index = 0;
}

internal void linux_end_recording_input(LinuxState *state) {
    state->recording_index = 0;
}

internal void linux_begin_input_playback(LinuxState *state, int playing_index) {
    memcpy(state->game_memory_block, state->memory_map,
           state->game_memory_size);

    state->bytes_read = state->game_memory_size;
    state->playing_index = playing_index;
}

internal void linux_end_input_playback(LinuxState *state) {
    state->bytes_read = 0;
    state->playing_index = 0;
}

internal void linux_record_input(LinuxState *state, GameInput recording_input) {
    u64 bytes_to_write = sizeof(GameInput);
    memcpy(state->memory_map + state->bytes_written, (void *)&recording_input,
           bytes_to_write);
    state->bytes_written += bytes_to_write;
}

internal void linux_playback_input(LinuxState *state,
                                   GameInput *playback_input) {
    u64 bytes_to_read = sizeof(GameInput);
    memcpy((void *)playback_input, state->memory_map + state->bytes_read,
           bytes_to_read);
    state->bytes_read += bytes_to_read;

    if (state->bytes_read == state->bytes_written) {
        linux_end_input_playback(state);
        linux_begin_input_playback(state, 1);
    }
}

internal bool sdl_handle_event(SDL_Event *event, SDLBackbuffer *backbuffer,
                               SDLControllers *controllers, LinuxState *state,
                               GameControllerInput *keyboard_controller) {
    switch (event->type) {
    case SDL_QUIT: {
        return true;
    } break;
    case SDL_WINDOWEVENT: {
        SDL_WindowEvent window_event = event->window;
        switch (window_event.event) {
        case SDL_WINDOWEVENT_RESIZED: {

            // NOTE(fede): 
            //  window_event.data1 -> new width
            //  window_event.data2 -> new height

            sdl_reset_renderer(backbuffer);
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
            global_game_running = false;
        } break;

#if HANDMADE_INTERNAL

        case SDLK_p: {
            if (key_event.type == SDL_KEYDOWN)
                global_pause = !global_pause;
        } break;

        case SDLK_l: {
            if (key_event.type == SDL_KEYDOWN) {
                if (state->playing_index != 0) {
                    linux_end_input_playback(state);
                } else if (state->recording_index == 0) {
                    linux_begin_recording_input(state, 1);
                } else {
                    linux_end_recording_input(state);
                    linux_begin_input_playback(state, 1);
                }
            }
        } break;

#endif // HANDMADE_INTERNAL
        }
    } break;
    }

    return false;
}

internal bool sdl_handle_events(SDLBackbuffer *backbuffer,
                                SDLControllers *controllers, LinuxState *state,
                                GameControllerInput *keyboard_controller) {
    int should_quit = false;

    SDL_Event event;
    while (SDL_PollEvent(&event)) {
        if (sdl_handle_event(&event, backbuffer, controllers, state,
                             keyboard_controller))
            should_quit = true;
    }

    return should_quit;
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

        // NOTE(fede): give 1,000,000 ns of leeway
        ms_to_sleep -= 1.0f;

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
    top = max(top, 0);
    bottom = min(bottom, (int)backbuffer->height - 1);

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
    left = max(left, 0);
    right = min(right, (int)backbuffer->width - 1);
    u32 *pixel_out =
        (u32 *)((u8 *)backbuffer->data + left * backbuffer->bits_per_pixel / 8 +
                y * backbuffer->pitch);
    for (int x = left; x < right; x++) {
        *pixel_out++ = *pixel_out | color;
    }
}

internal void sdl_debug_draw_cursor(SDLBackbuffer *backbuffer, int cursor,
                                    int top, int height, f32 coef, u32 color) {
    int x = (int)((f32)cursor * coef);
    sdl_debug_draw_vertical(backbuffer, x, top, top + height, color);
}

internal void sdl_debug_sync_display(SDLBackbuffer *backbuffer,
                                     SDLSoundOutput *sound_output,
                                     int debug_time_marker_count,
                                     SDLDebugTimeMarker *debug_time_markers,
                                     int current_marker_index) {
    int pad_x = 15;
    int pad_y = 15;

    int marker_height = 50;
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

        int top = pad_y;
        if (i == current_marker_index) {
            top += marker_height;
        }

        sdl_debug_draw_cursor(backbuffer, marker.expected_frame_boundary_byte,
                              top, marker_height, coef, 0x00aa0000);

        sdl_debug_draw_cursor(backbuffer, marker.play_cursor, top,
                              marker_height, coef, 0x0000FF00);
        sdl_debug_draw_cursor(backbuffer, marker.write_cursor, top,
                              marker_height, coef, 0x000000FF);

        sdl_debug_draw_cursor(backbuffer, marker.flip_play_cursor,
                              top + marker_height, 10, coef, 0x0000FF00);
        sdl_debug_draw_cursor(backbuffer, marker.flip_write_cursor,
                              top + marker_height, 10, coef, 0x000000FF);

        sdl_debug_draw_cursor(backbuffer, marker.target_cursor, top - 10, 10,
                              coef, 0x00000099);
        sdl_debug_draw_cursor(backbuffer, marker.expected_now_byte, top - 10,
                              10, coef, 0x00009900);

        {
            int bar_y = i % 2 ? top : top + marker_height;

            int bar_left = (int)((f32)marker.byte_to_lock * coef);
            int bar_right = bar_left + (int)((f32)marker.bytes_to_write * coef);
            sdl_debug_draw_horizontal(backbuffer, bar_y, bar_left, bar_right,
                                      0x0000aa00);
        }

        {
            int bar_y = top + marker_height;
            bar_y += i * 3;
            int bar_left = (int)((f32)marker.byte_to_lock * coef);
            int bar_right = (int)(((f32)marker.play_cursor + 2048.0f) * coef);
            // sdl_debug_draw_horizontal(backbuffer, bar_y, bar_left, bar_right,
            //                           0x0000aa00);
        }
    }
}

SDLSoundWriteMarker sdl_get_sound_write_marker(SDLSoundOutput *sound_output,
                                               SDLAudioRingBuffer *ring_buffer,
                                               f32 target_seconds_per_frame,
                                               u64 last_counter,
                                               bool *sound_is_valid) {
    assert(sound_is_valid != 0);

    SDLSoundWriteMarker result = {};

    int expected_frame_boundary_byte;

    SDL_LockAudio();

    int play_cursor = ring_buffer->play_cursor;
    int write_cursor = ring_buffer->write_cursor;

    u64 last_consumed_from = ring_buffer->last_consumed_from;

    SDL_UnlockAudio();

    // NOTE(fede): Check that the callback has been called at least once
    if (last_consumed_from > 0) {
        f32 seconds_until_frame_boundary =
            target_seconds_per_frame -
            sdl_get_seconds_elapsed(last_counter, SDL_GetPerformanceCounter());
        int bytes_until_frame_boundary =
            (int)(seconds_until_frame_boundary *
                  (f32)sound_output->samples_per_second *
                  (f32)sound_output->bytes_per_sample);

        f64 seconds_from_play_cursor = sdl_get_seconds_elapsed(
            last_consumed_from, SDL_GetPerformanceCounter());

        // NOTE(fede): Paused and continued
        if (seconds_until_frame_boundary < 0 ||
            seconds_from_play_cursor > target_seconds_per_frame) {
            expected_frame_boundary_byte =
                play_cursor + sound_output->bytes_per_sound_frame;
        } else {
            int bytes_from_play_cursor = seconds_from_play_cursor *
                                         sound_output->samples_per_second *
                                         sound_output->bytes_per_sample;

            expected_frame_boundary_byte = play_cursor +
                                           bytes_from_play_cursor +
                                           bytes_until_frame_boundary;

#if HANDMADE_INTERNAL
            debug_time_marker.play_cursor = play_cursor;
            debug_time_marker.write_cursor = write_cursor;
            debug_time_marker.expected_frame_boundary_byte =
                expected_frame_boundary_byte;
            debug_time_marker.expected_now_byte =
                play_cursor + bytes_from_play_cursor;
#endif
        }

    } else {
        expected_frame_boundary_byte =
            play_cursor + sound_output->bytes_per_sound_frame;
    }

    if (!*sound_is_valid) {
        sound_output->running_sample_index =
            write_cursor / sound_output->bytes_per_sample;
        *sound_is_valid = true;
    }

    result.byte_to_lock =
        (sound_output->running_sample_index * sound_output->bytes_per_sample) %
        sound_output->secondary_buffer_size;

    int safe_write_cursor = write_cursor;
    if (safe_write_cursor < play_cursor) {
        safe_write_cursor += sound_output->secondary_buffer_size;
    }
    assert(safe_write_cursor >= play_cursor);
    safe_write_cursor += sound_output->safety_bytes;

    // NOTE(fede): This was casey's expected_frame_boundary_byte
    // expected_frame_boundary_byte = play_cursor +
    // bytes_per_sound_frame;

    bool sound_is_latent = safe_write_cursor >= expected_frame_boundary_byte;

    // TODO(fede): test when sound is latent.
    //             Sound not latent is kind of tested.
    int target_cursor;
    if (sound_is_latent) {
        printf("sound is latent!\n");
        target_cursor = write_cursor + sound_output->safety_bytes +
                        sound_output->bytes_per_sound_frame;
    } else {
        target_cursor =
            expected_frame_boundary_byte + sound_output->bytes_per_sound_frame;
    }

    target_cursor %= sound_output->secondary_buffer_size;

    result.bytes_to_write = target_cursor - result.byte_to_lock;
    if (result.byte_to_lock >= target_cursor) {
        result.bytes_to_write += sound_output->secondary_buffer_size;
    }

#if HANDMADE_INTERNAL
    debug_time_marker.target_cursor = target_cursor;
    debug_time_marker.bytes_to_write = result.bytes_to_write;
    debug_time_marker.byte_to_lock = result.byte_to_lock;
#endif

    return result;
}

GAME_UPDATE_AND_RENDER(game_update_and_render_stub) {}

GAME_FILL_SOUND_BUFFER(game_fill_sound_buffer_stub) {}

internal bool linux_game_has_changed(GameLib *game, char *filename) {
    bool result = false;

    struct stat stat_;
    if (stat(filename, &stat_) == -1) {
        // TODO(fede): logging
        assert(!"File does not exist.");
    };

    if (stat_.st_size == 0) {
        return false;
    }

    if ((game->last_modified.tv_sec != stat_.st_mtim.tv_sec) ||
        (game->last_modified.tv_nsec != stat_.st_mtim.tv_nsec)) {
        result = true;
        game->last_modified = stat_.st_mtim;
    }

    return result;
}

internal GameLib linux_load_gamelib(char *filename) {
    GameLib result = {};

    result.handle = dlopen(filename, RTLD_NOW);

    if (result.handle) {
        result.is_valid = true;
        result.update_and_render =
            dlsym(result.handle, "game_update_and_render");
        result.fill_sound_buffer =
            dlsym(result.handle, "game_fill_sound_buffer");
    } else {
        result.is_valid = false;
        result.update_and_render = game_update_and_render_stub;
        result.fill_sound_buffer = game_fill_sound_buffer_stub;
    }

    return result;
}

internal void linux_reload_gamelib(GameLib *game, char *filename) {
    // TODO(fede): checking valid handle, etc.
    dlclose(game->handle);
    game->handle = dlopen(filename, RTLD_NOW);

    if (game->handle) {
        game->is_valid = true;
        game->update_and_render = dlsym(game->handle, "game_update_and_render");
        game->fill_sound_buffer = dlsym(game->handle, "game_fill_sound_buffer");
    } else {
        game->is_valid = false;
        game->update_and_render = game_update_and_render_stub;
        game->fill_sound_buffer = game_fill_sound_buffer_stub;
    }
}

internal void linux_unload_gamelib(GameLib *game) {
    if (game->is_valid) {
        dlclose(game->handle);
        game->update_and_render = game_update_and_render_stub;
        game->fill_sound_buffer = game_fill_sound_buffer_stub;
        game->is_valid = false;
    } else {
        assert(!game->handle);
    }
}

int main(void) {
    LinuxState state = {};

    linux_get_exe_path(&state);
    char game_dll_filename[LINUX_FILEPATH_MAX_COUNT];

    linux_build_global_filename_at_exe_location(&state, game_dll_filename,
                                                "handmade.so");

#if HANDMADE_INTERNAL
    void *base_address = (void *)terabytes((u64)2);
#else
    void *base_address = 0;
#endif

    GameMemory game_memory = {};
    game_memory.permanent_storage_size = megabytes(64);
    game_memory.transient_storage_size = gigabytes((u64)1);
    {
        state.game_memory_size = game_memory.permanent_storage_size +
                                 game_memory.transient_storage_size;
        state.game_memory_block =
            mmap(base_address, state.game_memory_size, PROT_READ | PROT_WRITE,
                 MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);

        game_memory.permanent_storage = state.game_memory_block;
        game_memory.transient_storage =
            (u8 *)state.game_memory_block + game_memory.permanent_storage_size;
    }

    // NOTE(fede): This is piggy, we are mapping a file the same size as the
    //             game memory, therefore doubling the memory used.
    {
        char state_filename[LINUX_FILEPATH_MAX_COUNT];
        linux_get_input_state_filepath(&state, state_filename, 1);

        int fd = open(state_filename, O_RDWR | O_CREAT, S_IRWXU);
        if (fd == -1) {
            // TODO(fede): logging
            return 0;
        }

        int one_hour_of_frames = 30 * 60 * 60;
        state.memory_map_size =
            one_hour_of_frames * sizeof(GameInput) + state.game_memory_size;

        printf("Game memory size: %.2fGB\n",
               (f32)state.game_memory_size / (f32)gigabytes(1));
        printf("Memory map size: %.2fGB\n",
               (f32)state.memory_map_size / (f32)gigabytes(1));

        if (ftruncate(fd, state.memory_map_size) == -1) {
            printf("Could not truncate file: %s | %d\n", strerror(errno),
                   errno);
            return 1;
        }

        int offset = 0;
        state.memory_map =
            mmap(0, state.memory_map_size, PROT_READ | PROT_WRITE, MAP_PRIVATE,
                 fd, offset);

        if (state.memory_map == MAP_FAILED) {
            printf("Could not allocate memory: %s | %d\n", strerror(errno),
                   errno);
            return 1;
        }
    }

    if (!SDL_Init(SDL_INIT_VIDEO | SDL_INIT_GAMECONTROLLER | SDL_INIT_AUDIO)) {
        // TODO(fede): SDL did not work!!
    }

    performance_frequency = SDL_GetPerformanceFrequency();

    u32 width = 1280;
    u32 height = 720;
    SDL_Window *window = SDL_CreateWindow(
        "Handmade Hero", SDL_WINDOWPOS_UNDEFINED, SDL_WINDOWPOS_UNDEFINED,
        width, height, SDL_WINDOW_RESIZABLE);

    SDLBackbuffer backbuffer = {};
    backbuffer.window = window;
    backbuffer.bits_per_pixel = 32;
    // Allocate backbuffer
    {
        backbuffer.width = width;
        backbuffer.height = height;
        backbuffer.pitch = width * backbuffer.bits_per_pixel / 8;

        u32 buffer_size = width * height * backbuffer.bits_per_pixel / 8;
        backbuffer.data = mmap(0, buffer_size, PROT_READ | PROT_WRITE,
                               MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
        if (!backbuffer.data) {
            // TODO(fede): allocation failed
        }

        sdl_reset_renderer(&backbuffer);
    }

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

    int game_update_rate = refresh_rate / 2;
    f32 target_seconds_per_frame = 1.0f / (f32)game_update_rate;

    SDLControllers controllers = {};

    SDLSoundOutput sound_output = {};
    sound_output.samples_per_second = 48000;
    sound_output.tone_hz = 512;
    sound_output.running_sample_index = 0;
    sound_output.bytes_per_sample = sizeof(i16) * 2;

    sound_output.bytes_per_sound_frame = sound_output.samples_per_second *
                                         sound_output.bytes_per_sample /
                                         game_update_rate;

    // TODO(fede): Include safety bytes
    // sound_output.safety_bytes = sound_output.samples_per_second *
    //                             sound_output.bytes_per_sample / refresh_rate
    //                             / 2;
    // sound_output.safety_bytes = 0;

    SDLAudioRingBuffer ring_buffer = {};

    sdl_init_audio(&sound_output, &ring_buffer);

    bool audio_is_paused = true;

    GameSoundOutputBuffer game_sound_buffer = {};

    // NOTE(fede): allocate 1 second
    game_sound_buffer.samples =
        mmap(0, sound_output.samples_per_second * sound_output.bytes_per_sample,
             PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);

#if HANDMADE_INTERNAL
    game_memory.debug_platform_read_entire_file =
        &debug_platform_read_entire_file;
    game_memory.debug_platform_free_file_memory =
        &debug_platform_free_file_memory;
    game_memory.debug_platform_write_entire_file =
        &debug_platform_write_entire_file;
#endif

    if (!game_sound_buffer.samples || !game_memory.permanent_storage ||
        !game_memory.transient_storage) {
        // TODO(fede): allocation failed
        return 0;
    }

    GameInput old_input = {};
    GameInput new_input = {};

    new_input.dt_for_frame = target_seconds_per_frame;
    old_input.dt_for_frame = target_seconds_per_frame;

    u64 last_counter = SDL_GetPerformanceCounter();

#if HANDMADE_INTERNAL
    u32 debug_time_marker_index = 0;
    SDLDebugTimeMarker debug_time_markers[20] = {};
#endif

    bool sound_is_valid = false;

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

    GameLib game = {};
    game = linux_load_gamelib(game_dll_filename);
    if (!game.is_valid) {
        printf("Game is not valid: %s\n", dlerror());
        printf("dll filename: %s\n", game_dll_filename);
    }

    while (global_game_running) {

        if (linux_game_has_changed(&game, game_dll_filename)) {
            linux_reload_gamelib(&game, game_dll_filename);
            if (!game.is_valid) {
                printf("Game is not valid: %s\n", dlerror());
            }
        }

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

        if (sdl_handle_events(&backbuffer, &controllers, &state,
                              new_keyboard_controller)) {
            global_game_running = false;
        }

        // NOTE(fede): mouse input
        {
            // TODO(fede): Update mouse.z (scroll wheel) position
            u32 mouse_flags = SDL_GetMouseState(&new_input.mouse_input.x,
                                                &new_input.mouse_input.y);
            for (int i = 0; i < 3; i++) {
                bool is_down = (mouse_flags & SDL_BUTTON(i + 1)) > 0;
                sdl_handle_button(&old_input.mouse_input.buttons[i], is_down,
                                  &new_input.mouse_input.buttons[i]);
            }
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

                new_controller_state->is_analog =
                    old_controller_state->is_analog;
                {
                    // NOTE: Stick is said to be centered ~8000, this is
                    //       specified in the SDL Wiki:
                    //       https://wiki.libsdl.org/SDL2/SDL_GameControllerAxis
                    f32 x = sdl_normalize_stick(left_stick_x, 8000);
                    f32 y = sdl_normalize_stick(left_stick_y, 8000);

                    if (GET_BUTTON(controller_handle, DPAD_LEFT)) {
                        x = -1.0f;
                        new_controller_state->is_analog = false;
                    }
                    if (GET_BUTTON(controller_handle, DPAD_RIGHT)) {
                        x = 1.0f;
                        new_controller_state->is_analog = false;
                    }
                    if (GET_BUTTON(controller_handle, DPAD_UP)) {
                        y = -1.0f;
                        new_controller_state->is_analog = false;
                    }
                    if (GET_BUTTON(controller_handle, DPAD_DOWN)) {
                        y = 1.0f;
                        new_controller_state->is_analog = false;
                    }

                    sdl_update_controller_sticks(old_controller_state,
                                                 new_controller_state, x, y);
                }
            }
        }

        if (global_pause) {
            continue;
        }

        GameDisplayBuffer game_buffer = {};
        game_buffer.data = backbuffer.data;
        game_buffer.height = height;
        game_buffer.width = width;

        if (state.recording_index) {
            linux_record_input(&state, new_input);
        }

        if (state.playing_index) {
            linux_playback_input(&state, &new_input);
        }

        ThreadContext thread = {};
        game.update_and_render(&thread, &game_memory, &game_buffer, &new_input);

        SDLSoundWriteMarker write_marker = sdl_get_sound_write_marker(
            &sound_output, &ring_buffer, target_seconds_per_frame, last_counter,
            &sound_is_valid);

        game_sound_buffer.samples_per_second = sound_output.samples_per_second;
        game_sound_buffer.sample_count =
            write_marker.bytes_to_write / sound_output.bytes_per_sample;

        game.fill_sound_buffer(&thread, &game_memory, &game_sound_buffer);

        sdl_fill_sound_buffer(&ring_buffer, &sound_output, &game_sound_buffer,
                              write_marker);

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
            SDL_LockAudio();
            debug_time_marker.flip_play_cursor = ring_buffer.play_cursor;
            debug_time_marker.flip_write_cursor = ring_buffer.write_cursor;
            SDL_UnlockAudio();

#if 0
            sdl_debug_sync_display(
                &backbuffer, &sound_output, array_count(debug_time_markers),
                debug_time_markers, debug_time_marker_index - 1);
#endif
#endif

            SDL_RenderClear(backbuffer.renderer);

            // STUDY(fede): use streaming texture and locking functions
            if (SDL_UpdateTexture(backbuffer.texture, 0,
                                  (void *)backbuffer.data,
                                  backbuffer.pitch) < 0) {
                // TODO(fede): Update texture failure
            }

            SDL_Rect dest_rect = {};
            dest_rect.h = game_buffer.height;
            dest_rect.w = game_buffer.width;
            if (SDL_RenderCopy(backbuffer.renderer, backbuffer.texture, 0,
                               &dest_rect) < 0) {
                // TODO(fede): Render texture failure
            }

            SDL_RenderPresent(backbuffer.renderer);
        }

#if HANDMADE_INTERNAL
        if (!audio_is_paused) {
            debug_time_markers[debug_time_marker_index++] = debug_time_marker;
            if (debug_time_marker_index >= array_count(debug_time_markers)) {
                debug_time_marker_index = 0;
            }

            debug_time_marker = (SDLDebugTimeMarker){};
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
