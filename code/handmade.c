#include "handmade.h"

internal void fill_audio_buffer(GameSoundOutputBuffer *buffer, int tone_hz) {
    static f32 t_sine = 0;
    const u32 tone_volume = 3000;
    u32 wave_period = buffer->samples_per_second / tone_hz;

    i16 *sample_out = buffer->samples;
    for (int i = 0; i < buffer->sample_count; i++) {
        f32 sine_value = sinf(t_sine);
        i16 sample_value = (i16)(sine_value * tone_volume);

        *sample_out++ = sample_value;
        *sample_out++ = sample_value;

        t_sine += 2.0f * PI * 1.0f / (f32)wave_period;
    }

    int q = (int)(t_sine / (2.0f * PI));
    f32 t_sine_whole_part = 2.0f * PI * (f32)q;
    t_sine = t_sine - t_sine_whole_part;
}

internal void render_buffer(GameDisplayBuffer *buffer, i32 x_offset,
                            i32 y_offset) {
    for (int y = 0; y < buffer->height; y++) {
        for (int x = 0; x < buffer->width; x++) {
            u8 g = y + y_offset;
            u8 b = x + x_offset;
            buffer->data[y * buffer->width + x] = (g << 8) + b;
        }
    }
}

internal void game_update_and_render(GameMemory *memory,
                                     GameDisplayBuffer *display_buffer,
                                     GameSoundOutputBuffer *audio_buffer,
                                     GameInput *input) {
    assert(sizeof(GameState) <= memory->permanent_storage_size);
    GameState *game_state = (GameState *)memory->permanent_storage;

    if (!memory->is_initialized) {
        DebugReadFileResult file_result =
            debug_platform_read_entire_file(__FILE__);
        if (file_result.memory) {
            if (!debug_platform_write_entire_file("test.txt", file_result.size,
                                                  file_result.memory)) {
                // handle error
                printf("Error writing to test.txt\n");
            }
            debug_platform_free_file_memory(file_result);
        }

        game_state->x_offset = 0;
        game_state->y_offset = 0;
        game_state->tone_hz = 256;

        memory->is_initialized = true;
    }

    GameControllerInput controller = input->inputs[0];

    if (controller.is_analog) {
        game_state->x_offset += (int)(controller.end_x * 4.0f);
        game_state->y_offset += (int)(controller.end_y * 4.0f);

        game_state->tone_hz = 256 + (int)(controller.end_y * 128.0f);
    } else {
        // TODO(fede): Digital input
    }

    if (controller.button_a.ended_down) {
        game_state->x_offset += 10;
    }

    fill_audio_buffer(audio_buffer, game_state->tone_hz);
    render_buffer(display_buffer, game_state->x_offset, game_state->y_offset);
}
