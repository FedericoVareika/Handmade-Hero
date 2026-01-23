#include "handmade.h"

internal void fill_audio_buffer(GameSoundOutputBuffer *buffer, int tone_hz) {
    static f32 t_sine = 0;
    const u32 tone_volume = 3000;
    u32 wave_period = buffer->samples_per_second / tone_hz;

    i16 *sample_out = buffer->samples;
    for (u32 i = 0; i < buffer->sample_count; i++) {
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

internal void render_buffer(GameDisplayBuffer *buffer, int x_offset,
                            int y_offset) {
    for (u32 y = 0; y < buffer->height; y++) {
        for (u32 x = 0; x < buffer->width; x++) {
            u8 g = y + y_offset;
            u8 b = x + x_offset;
            buffer->data[y * buffer->width + x] = (g << 8) + b;
            buffer->data[y * buffer->width + x] = 0;
        }
    }
}

internal void debug_render_player(GameDisplayBuffer *buffer, int player_x,
                                  int player_y) {
    int player_width = 10;
    int player_height = 10;

    int top = min(player_y, (int)buffer->height - player_height);
    top = max(player_y, 0);
    int bottom = player_y + player_height;

    int left = min(player_x, (int)buffer->width - player_width);
    left = max(player_x, 0);
    int right = player_x + player_width;

    u32 player_color = 0xFFFFFFFF;
    for (int y = top; y < bottom; y++) {
        for (int x = left; x < right; x++) {
            buffer->data[y * buffer->width + x] = player_color;
        }
    }
}

extern GAME_UPDATE_AND_RENDER(game_update_and_render) {
    assert(sizeof(GameState) <= memory->permanent_storage_size);
    GameState *game_state = (GameState *)memory->permanent_storage;

    if (!memory->is_initialized) {
        assert(&input->controllers[0]._end -
                   &input->controllers[0].buttons[0] ==
               array_count(input->controllers[0].buttons));

#if 0
        DebugReadFileResult file_result =
            memory->debug_platform_read_entire_file(__FILE__);
        if (file_result.memory) {
            if (!memory->debug_platform_write_entire_file("test.txt", file_result.size,
                                                  file_result.memory)) {
                // handle error
                printf("Error writing to test.txt\n");
            }
            memory->debug_platform_free_file_memory(file_result);
        }
#endif

        game_state->x_offset = 0;
        game_state->y_offset = 0;
        game_state->tone_hz = 256;

        memory->is_initialized = true;
    }

    for (int i = 0; i < HANDMADE_MAX_INPUTS; i++) {
        GameControllerInput *controller = get_game_controller(input, i);
        if (!controller->is_connected)
            continue;

        if (controller->is_analog) {
            game_state->x_offset += (int)(controller->avg_stick_x * 4.0f);
            game_state->y_offset += (int)(controller->avg_stick_y * 4.0f);

            game_state->player_x += (int)(controller->avg_stick_x * 4.0f);
            game_state->player_y += (int)(controller->avg_stick_y * 4.0f);

            game_state->tone_hz = 256 + (int)(controller->avg_stick_y * 128.0f);
        } else {
            if (controller->move_up.ended_down) {
                game_state->y_offset -= 4;
                game_state->player_y -= 4;
            }

            if (controller->move_left.ended_down) {
                game_state->x_offset -= 4;
                game_state->player_x -= 4;
            }

            if (controller->move_down.ended_down) {
                game_state->y_offset += 4;
                game_state->player_y += 4;
            }

            if (controller->move_right.ended_down) {
                game_state->x_offset += 4;
                game_state->player_x += 4;
            }

            if (controller->move_up.ended_down) {
                game_state->tone_hz = 256 + 128;
            } else {
                game_state->tone_hz = 256;
            }
        }

        if (controller->button_a.ended_down) {
            game_state->x_offset += 10;
        }
    }

    render_buffer(display_buffer, game_state->x_offset, game_state->y_offset);
    debug_render_player(display_buffer, game_state->player_x,
                        game_state->player_y);
}

extern GAME_FILL_SOUND_BUFFER(game_fill_sound_buffer) {
    assert(sizeof(GameState) <= memory->permanent_storage_size);
    GameState *game_state = (GameState *)memory->permanent_storage;
    fill_audio_buffer(sound_buffer, game_state->tone_hz);
}
