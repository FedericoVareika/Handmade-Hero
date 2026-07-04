#include "handmade.h"

#include "handmade_tile.c" 

internal void fill_audio_buffer(GameSoundOutputBuffer *buffer, int tone_hz) {
    static f32 t_sine = 0;
    const u32 tone_volume = 3000;
    u32 wave_period = buffer->samples_per_second / tone_hz;

    i16 *sample_out = buffer->samples;
    for (u32 i = 0; i < buffer->sample_count; i++) {
        f32 sine_value = sin_f32(t_sine);

#if 0
        i16 sample_value = (i16)(sine_value * tone_volume);
#else
        i16 sample_value = 0;
#endif

        *sample_out++ = sample_value;
        *sample_out++ = sample_value;

        t_sine += 2.0f * PI * 1.0f / (f32)wave_period;
    }

    int q = truncate_f32_to_int(t_sine / (2.0f * PI));
    f32 t_sine_whole_part = 2.0f * PI * (f32)q;
    t_sine = t_sine - t_sine_whole_part;
}

internal void debug_render_player(GameDisplayBuffer *buffer, int player_x,
                                  int player_y) {
    int player_width = 20;
    int player_height = 20;

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

internal void clear_screen(GameDisplayBuffer *buffer) {
    for (u32 y = 0; y < buffer->height; y++) {
        for (u32 x = 0; x < buffer->width; x++) {
            // buffer->data[y * buffer->width + x] = 0x00FF00FF;
            buffer->data[y * buffer->width + x] = 0;
        }
    }
}

internal void draw_rectangle(GameDisplayBuffer *buffer, f32 real_min_x,
                             f32 real_min_y, f32 real_max_x, f32 real_max_y,
                             f32 r, f32 g, f32 b) {
    int min_x = round_f32_to_int(real_min_x);
    int min_y = round_f32_to_int(real_min_y);
    int max_x = round_f32_to_int(real_max_x);
    int max_y = round_f32_to_int(real_max_y);

    min_x = max(0, min_x);
    min_y = max(0, min_y);
    max_x = min((int)buffer->width, max_x);
    max_y = min((int)buffer->height, max_y);

    u8 r8 = (u8)round_f32_to_int(r * 0xFF);
    u8 g8 = (u8)round_f32_to_int(g * 0xFF);
    u8 b8 = (u8)round_f32_to_int(b * 0xFF);

    u32 color = r8;
    color = (color << 8) + g8;
    color = (color << 8) + b8;

    for (int y = min_y; y < max_y; y++) {
        for (int x = min_x; x < max_x; x++) {
            buffer->data[y * buffer->width + x] = color;
        }
    }
}

extern GAME_UPDATE_AND_RENDER(game_update_and_render) {
    assert(sizeof(GameState) <= memory->permanent_storage_size);
    GameState *game_state = (GameState *)memory->permanent_storage;

    // TODO(fede): Left off at day 34, 41:24. 
    //      Recommend watching the "start creating map procedurally" chapter 
    //      again for clarity, I feel like I am missing a step. 
    if (!memory->is_initialized) {
        assert(&input->controllers[0]._end -
                   &input->controllers[0].buttons[0] ==
               array_count(input->controllers[0].buttons));

        game_state->player_pos = (TilemapPosition){
            .abs_tile_x = 3,
            .abs_tile_y = 3,

            .tile_rel_x = 0,
            .tile_rel_y = 0,
        };

        initialize_arena(
                &game_state->world_arena,
                memory->permanent_storage_size,
                (u8 *)memory->permanent_storage + sizeof(GameState));

        World *world = push_struct(&game_state->world_arena, World); // TODO(fede): alloc
        Tilemap *tilemap = push_struct(&game_state->world_arena, Tilemap); // TODO(fede): alloc
        {
            tilemap->chunk_shift = 8;
            tilemap->chunk_mask = (1 << tilemap->chunk_shift) - 1;

            tilemap->chunk_dim = 256;

            tilemap->tilechunk_count_x = 1;
            tilemap->tilechunk_count_y = 1;

            tilemap->tile_side_in_meters = TILE_SIDE_IN_METERS;
            tilemap->tile_side_in_pixels = TILE_SIDE_IN_PIXELS;
            tilemap->meters_to_pixels = 
                tilemap->tile_side_in_pixels / tilemap->tile_side_in_meters;

            world->tilemap = tilemap;
        }
        game_state->world = world;

        u32 tiles_per_height = 9;
        u32 tiles_per_width = 17;

        // TODO(fede): alloc tilechunks automatically when set_tile_value is
        //          called
        {
            tilemap->tilechunks = push_array(
                    &game_state->world_arena,
                    TileChunk,
                    tilemap->tilechunk_count_x * tilemap->tilechunk_count_y);

            for (u32 screen_y = 0;
                    screen_y < tilemap->tilechunk_count_y;
                    screen_y++) {
                for (u32 screen_x = 0;
                        screen_x < tilemap->tilechunk_count_x;
                        screen_x++) {

                    TileChunk *tilechunk = &tilemap->tilechunks[
                        screen_y * tilemap->chunk_dim + screen_x];

                    tilechunk->tiles = push_array(&game_state->world_arena, u32,
                            tilemap->chunk_dim * tilemap->chunk_dim);
                }
            }
        }

            for (u32 screen_y = 0;
                    screen_y < tilemap->chunk_dim / tiles_per_height;
                    screen_y++) {
                for (u32 screen_x = 0;
                        screen_x < tilemap->chunk_dim / tiles_per_width;
                        screen_x++) {
                for (u32 tile_y = 0; tile_y < tiles_per_height; tile_y++) {
                    for (u32 tile_x = 0; tile_x < tiles_per_width; tile_x++) {

                        u32 abs_tile_x = screen_x * tiles_per_width + tile_x;
                        u32 abs_tile_y = screen_y * tiles_per_height + tile_y;

                        u32 val = 0;
                        if (tile_y == 0 || tile_y == tiles_per_height - 1 ||
                            tile_x == 0 || tile_x == tiles_per_width - 1) {
                            if (tile_y != (tiles_per_height - 1) / 2 &&
                                tile_x != (tiles_per_width - 1) / 2) {
                                val = 1;
                            }
                        }

                        set_tile_value(
                                &game_state->world_arena,
                                tilemap, abs_tile_x, abs_tile_y, val);
                    }
                }
            }
        }

        memory->is_initialized = true;
    }

    World *world = game_state->world;
    Tilemap *tilemap = world->tilemap;

    f32 lower_left_x = -tilemap->tile_side_in_pixels / 2;
    f32 lower_left_y = display_buffer->height;

    f32 player_dx = 0;
    f32 player_dy = 0;

    f32 player_velocity = 5;
    for (int i = 0; i < HANDMADE_MAX_INPUTS; i++) {
        GameControllerInput *controller = get_game_controller(input, i);
        if (!controller->is_connected)
            continue;

        if (controller->is_analog) {
            player_dx += (int)(controller->avg_stick_x * 4.0f);
            player_dy += (int)(controller->avg_stick_y * 4.0f);
        } else {
            if (controller->move_up.ended_down) {
                player_dy += 1; 
            }

            if (controller->move_left.ended_down) {
                player_dx -= 1;
            }

            if (controller->move_down.ended_down) {
                player_dy -= 1;
            }

            if (controller->move_right.ended_down) {
                player_dx += 1;
            }

            player_dx *= player_velocity * input->dt_for_frame;
            player_dy *= player_velocity * input->dt_for_frame;
        }
    }

    draw_rectangle(display_buffer, 0, 0, display_buffer->width,
                   display_buffer->height, 1, 0, 1);

    // // clang-format off
    // u32 tiles[TILEMAP_HEIGHT][TILEMAP_WIDTH] =
    //     {
    //         { 1, 1, 1, 1,   1, 1, 1, 1,  1,  1, 1, 1, 1,   1, 1, 1, 1,  1, 1, 1, 1,   1, 1, 1, 1,  1,  1, 1, 1, 1,   1, 1, 1, 1 },
    //         { 1, 1, 0, 0,   0, 0, 1, 0,  0,  0, 0, 0, 0,   0, 0, 0, 1,  1, 1, 0, 0,   0, 0, 1, 0,  0,  0, 0, 0, 0,   0, 0, 0, 1 },
    //         { 1, 0, 1, 1,   1, 1, 0, 0,  0,  0, 0, 0, 0,   0, 0, 0, 1,  1, 0, 1, 1,   1, 1, 0, 0,  0,  1, 0, 0, 0,   0, 0, 0, 1 },
    //
    //         { 1, 0, 0, 0,   0, 0, 1, 0,  0,  0, 0, 0, 0,   0, 0, 0, 1,  1, 0, 0, 0,   0, 0, 1, 0,  0,  0, 0, 0, 0,   0, 0, 0, 1 },
    //         { 1, 0, 0, 0,   0, 0, 1, 0,  0,  1, 0, 0, 0,   0, 0, 0, 0,  0, 0, 0, 0,   0, 0, 1, 0,  1,  0, 0, 0, 0,   0, 0, 0, 1 },
    //         { 1, 0, 0, 0,   0, 0, 1, 0,  0,  0, 0, 0, 0,   0, 0, 0, 1,  1, 0, 0, 0,   0, 0, 1, 0,  0,  0, 0, 0, 0,   0, 0, 0, 1 },
    //
    //         { 1, 0, 0, 0,   0, 0, 0, 0,  0,  0, 0, 0, 0,   0, 0, 0, 1,  1, 0, 0, 0,   0, 0, 0, 0,  0,  0, 0, 0, 0,   0, 0, 0, 1 },
    //         { 1, 0, 0, 0,   0, 0, 1, 0,  0,  1, 1, 1, 1,   0, 0, 0, 1,  1, 0, 0, 0,   0, 0, 0, 0,  0,  1, 1, 1, 1,   0, 0, 0, 1 },
    //         { 1, 1, 1, 1,   1, 1, 1, 1,  0,  1, 1, 1, 1,   1, 1, 1, 1,  1, 1, 1, 1,   1, 1, 1, 1,  0,  1, 1, 1, 1,   1, 1, 1, 1 },
    //
    //         { 1, 1, 1, 1,   1, 1, 1, 1,  0,  1, 1, 1, 1,   1, 1, 1, 1,  1, 1, 1, 1,   1, 1, 1, 1,  0,  1, 1, 1, 1,   1, 1, 1, 1 },
    //         { 1, 1, 0, 0,   0, 0, 1, 0,  0,  0, 0, 0, 0,   0, 0, 0, 1,  1, 1, 0, 0,   0, 0, 1, 0,  0,  0, 0, 0, 0,   0, 0, 0, 1 },
    //         { 1, 0, 1, 1,   1, 1, 0, 0,  0,  1, 0, 0, 0,   0, 0, 0, 1,  1, 0, 1, 1,   1, 1, 0, 0,  0,  1, 0, 0, 0,   0, 0, 0, 1 },
    //
    //         { 1, 0, 0, 0,   0, 0, 1, 0,  0,  0, 0, 0, 0,   0, 0, 0, 1,  1, 0, 0, 0,   0, 0, 1, 0,  0,  0, 0, 0, 0,   0, 0, 0, 1 },
    //         { 1, 0, 0, 0,   0, 0, 1, 0,  1,  0, 0, 0, 0,   0, 0, 0, 0,  0, 0, 0, 0,   0, 0, 1, 0,  1,  0, 0, 0, 0,   0, 0, 0, 1 },
    //         { 1, 0, 0, 0,   0, 0, 1, 0,  0,  0, 0, 0, 0,   0, 0, 0, 1,  1, 0, 0, 0,   0, 0, 1, 0,  0,  0, 0, 0, 0,   0, 0, 0, 1 },
    //
    //         { 1, 0, 0, 0,   0, 0, 0, 0,  0,  0, 0, 0, 0,   0, 0, 0, 1,  1, 0, 0, 0,   0, 0, 0, 0,  0,  0, 0, 0, 0,   0, 0, 0, 1 },
    //         { 1, 0, 0, 0,   0, 0, 0, 0,  0,  1, 1, 1, 1,   0, 0, 0, 1,  1, 0, 0, 0,   0, 0, 0, 0,  0,  1, 1, 1, 1,   0, 0, 0, 1 },
    //         { 1, 1, 1, 1,   1, 1, 1, 1,  0,  1, 1, 1, 1,   1, 1, 1, 1,  1, 1, 1, 1,   1, 1, 1, 1,  1,  1, 1, 1, 1,   1, 1, 1, 1 },
    //     };
    //
    // // clang-format on

    // TileChunk tilechunks[1] = {};
    // tilechunks[0].tiles = (u32 *)tiles;

    // world.tilemap.tilechunks = (TileChunk *)tilechunks;

    f32 player_width = tilemap->tile_side_in_meters * 0.5;
    f32 player_height = tilemap->tile_side_in_meters * 0.75;

    // NOTE(fede): Player movement and collision checking
    {
        TilemapPosition new_player_pos = game_state->player_pos;
        new_player_pos.tile_rel_x += player_dx;
        new_player_pos.tile_rel_y += player_dy;

        TilemapPosition new_player_left = new_player_pos;
        new_player_left.tile_rel_x -= player_width / 2;
        TilemapPosition new_player_right = new_player_pos;
        new_player_right.tile_rel_x += player_width / 2;

        new_player_pos = recanonicalize_position(tilemap, new_player_pos);
        new_player_left = recanonicalize_position(tilemap, new_player_left);
        new_player_right = recanonicalize_position(tilemap, new_player_right);

        if (is_tilemap_point_empty(tilemap, new_player_left) &&
            is_tilemap_point_empty(tilemap, new_player_right)) {
            game_state->player_pos = new_player_pos;
        }
    }

   f32 screen_center_x = display_buffer->width / 2;
   f32 screen_center_y = display_buffer->height / 2;

    for (i32 rel_col = -10; rel_col < 10; rel_col++) {
        for (i32 rel_row = -20; rel_row < 20; rel_row++) {

            // NOTE(fede): Color
            f32 gray, green_tint; 
            {
                gray = 0.2;

                u32 tile_value = get_tile_value(
                        tilemap,
                        rel_col + game_state->player_pos.abs_tile_x,
                        rel_row + game_state->player_pos.abs_tile_y);
                gray += tile_value * 0.3; 
                f32 player_inside = rel_col == 0 && rel_row == 0 ? 0.3 : 0; 
                green_tint = player_inside * 0.5;
            }

            // NOTE(fede): Screen position 
            f32 min_x, min_y, max_x, max_y;
            {
                f32 center_x = screen_center_x;
                f32 center_y = screen_center_y;

                center_x += rel_col * tilemap->tile_side_in_pixels;
                center_y -= rel_row * tilemap->tile_side_in_pixels;

                center_x -= tilemap->meters_to_pixels * game_state->player_pos.tile_rel_x;
                center_y += tilemap->meters_to_pixels * game_state->player_pos.tile_rel_y;

                min_x = center_x - tilemap->tile_side_in_pixels * 0.5;
                min_y = center_y + tilemap->tile_side_in_pixels * 0.5;
                max_x = center_x + tilemap->tile_side_in_pixels * 0.5;
                max_y = center_y - tilemap->tile_side_in_pixels * 0.5;
            }

            draw_rectangle(display_buffer, 
                    min_x, max_y, max_x, min_y,
                    gray, gray + green_tint, gray);
        }
    }

    // NOTE(fede): Draw player.
    {
        f32 player_x = lower_left_x +
            tilemap->meters_to_pixels * (
                game_state->player_pos.abs_tile_x * tilemap->tile_side_in_meters +
                game_state->player_pos.tile_rel_x);

        f32 player_y = lower_left_y -
            tilemap->meters_to_pixels * (
                game_state->player_pos.abs_tile_y * tilemap->tile_side_in_meters +
                game_state->player_pos.tile_rel_y);

        player_x = display_buffer->width / 2;
        player_y = display_buffer->height / 2;

        f32 player_pixel_width = player_width * tilemap->meters_to_pixels; 
        f32 player_pixel_height = player_height * tilemap->meters_to_pixels; 

        f32 player_left = player_x - player_pixel_width / 2;
        f32 player_bottom = player_y;

        f32 player_right = player_x + player_pixel_width / 2;
        f32 player_top = player_y - player_pixel_height;

        draw_rectangle(display_buffer,
                player_left,
                player_top,
                player_right,
                player_bottom,
                1, 1, 1);
    }
}

extern GAME_FILL_SOUND_BUFFER(game_fill_sound_buffer) {
    assert(sizeof(GameState) <= memory->permanent_storage_size);
    GameState *game_state = (GameState *)memory->permanent_storage;
    fill_audio_buffer(sound_buffer, 256);
}
