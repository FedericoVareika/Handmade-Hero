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

void game_update_and_render(GameDisplayBuffer *display_buffer, i32 x_offset,
                            i32 y_offset, GameSoundOutputBuffer *audio_buffer,
                            int tone_hz) {
    fill_audio_buffer(audio_buffer, tone_hz);
    render_buffer(display_buffer, x_offset, y_offset);
}
