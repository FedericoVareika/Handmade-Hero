#include "handmade.h"
#include "handmade_intrinsics.h" 

#include <stdio.h>

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

internal u32 get_tile_value_unchecked(
        World *world,
        TileChunk *tilechunk,
        u32 tile_x, u32 tile_y) {

    assert(tilechunk);
    assert(tile_x < world->chunk_dim);
    assert(tile_y < world->chunk_dim);

    return tilechunk->tiles[tile_y * world->chunk_dim + tile_x];
}

internal TileChunk *get_tilechunk(World *world, TileChunkPosition chunk_pos) {
    // TODO(fede): Implement
    return &world->tilechunks[0];

    // // clang-format off
    // if (tilemap_x < 0 || tilemap_x >= world->tilemap_count_x || 
    //     tilemap_y < 0 || tilemap_y >= world->tilemap_count_y) 
    //     return 0;
    // // clang-format on
    //
    // return &world->tilemaps[tilemap_y * world->tilemap_count_x + tilemap_x];
}

internal bool is_tilemap_point_empty(
        World *world,
        TileChunk *tilemap,
        u32 test_tile_x, u32 test_tile_y) {
    if (!tilemap)
        return false;

    return !get_tile_value_unchecked(world, tilemap, test_tile_x, test_tile_y);
}

// NOTE(fede): Coordinates wrap around, if you walk off of one end, 
//          you walk into the other end. (Toroidal topology :: Taurus) 
internal void recanonicalize_coord(
        World *world,
        f32 *tile_rel,
        u32 *tile) {
    f32 offset = floor_f32_to_int(*tile_rel / world->tile_side_in_meters); 

    *tile += offset;
    *tile_rel -= offset * world->tile_side_in_meters; 

    assert(*tile_rel >= 0);

    assert(*tile_rel < world->tile_side_in_meters);
}

internal WorldPosition recanonicalize_position(
        World *world,
        WorldPosition can_pos) {
    WorldPosition result = can_pos;

    recanonicalize_coord(world, &result.tile_rel_x, &result.abs_tile_x);
    recanonicalize_coord(world, &result.tile_rel_y, &result.abs_tile_y);

    return result;
}

inline TileChunkPosition get_chunk_position_for(
        World *world,
        u32 abs_tile_x,
        u32 abs_tile_y) {
    return (TileChunkPosition){
        .tile_chunk_x = abs_tile_x >> world->chunk_shift,
        .tile_chunk_y = abs_tile_y >> world->chunk_shift,
        .rel_tile_x = abs_tile_x & world->chunk_mask,
        .rel_tile_y = abs_tile_y & world->chunk_mask,
    };
}

internal u32 get_tile_value(World *world, u32 tile_x, u32 tile_y) {
    TileChunkPosition chunk_pos = get_chunk_position_for(
            world,
            tile_x,
            tile_y);

    TileChunk *tilechunk = get_tilechunk(world, chunk_pos);
    if (!tilechunk)
        return 0;

    return get_tile_value_unchecked(
            world, tilechunk, chunk_pos.rel_tile_x, chunk_pos.rel_tile_y);
}

internal bool is_world_point_empty(World *world, WorldPosition world_pos) {
    TileChunkPosition chunk_pos = get_chunk_position_for(
            world,
            world_pos.abs_tile_x,
            world_pos.abs_tile_y);

    TileChunk *tilemap = get_tilechunk(world, chunk_pos);
    if (!tilemap)
        return false;

    return is_tilemap_point_empty(world, tilemap, chunk_pos.rel_tile_x,
                                  chunk_pos.rel_tile_y);
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
            memory->debug_platform_read_entire_file(thread, __FILE__);
        if (file_result.memory) {
            if (!memory->debug_platform_write_entire_file(thread, "test.txt", file_result.size,
                                                  file_result.memory)) {
                // TODO(fede): logging
            }
            memory->debug_platform_free_file_memory(thread, file_result);
        }
#endif

        game_state->player_pos = (WorldPosition){
            .abs_tile_x = 3,
            .abs_tile_y = 3,

            .tile_rel_x = TILE_SIDE_IN_METERS / 2,
            .tile_rel_y = TILE_SIDE_IN_METERS / 2,
        };

        memory->is_initialized = true;
    }

    World world = {};

    world.chunk_shift = 8;
    world.chunk_mask = (1 << world.chunk_shift) - 1;

    world.chunk_dim = 256;

    world.tilemap_count_x = 2;
    world.tilemap_count_y = 2;

    world.tile_side_in_meters = TILE_SIDE_IN_METERS;
    world.tile_side_in_pixels = TILE_SIDE_IN_PIXELS;
    world.meters_to_pixels = 
        world.tile_side_in_pixels / world.tile_side_in_meters;

    f32 lower_left_x = -world.tile_side_in_pixels / 2;
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

    // clang-format off
    u32 tiles[TILEMAP_HEIGHT][TILEMAP_WIDTH] =
        {
            { 1, 1, 1, 1,   1, 1, 1, 1,  1,  1, 1, 1, 1,   1, 1, 1, 1,  1, 1, 1, 1,   1, 1, 1, 1,  1,  1, 1, 1, 1,   1, 1, 1, 1 },
            { 1, 1, 0, 0,   0, 0, 1, 0,  0,  0, 0, 0, 0,   0, 0, 0, 1,  1, 1, 0, 0,   0, 0, 1, 0,  0,  0, 0, 0, 0,   0, 0, 0, 1 },
            { 1, 0, 1, 1,   1, 1, 0, 0,  0,  0, 0, 0, 0,   0, 0, 0, 1,  1, 0, 1, 1,   1, 1, 0, 0,  0,  1, 0, 0, 0,   0, 0, 0, 1 },
                                                                                                                                
            { 1, 0, 0, 0,   0, 0, 1, 0,  0,  0, 0, 0, 0,   0, 0, 0, 1,  1, 0, 0, 0,   0, 0, 1, 0,  0,  0, 0, 0, 0,   0, 0, 0, 1 },
            { 1, 0, 0, 0,   0, 0, 1, 0,  0,  1, 0, 0, 0,   0, 0, 0, 0,  0, 0, 0, 0,   0, 0, 1, 0,  1,  0, 0, 0, 0,   0, 0, 0, 1 },
            { 1, 0, 0, 0,   0, 0, 1, 0,  0,  0, 0, 0, 0,   0, 0, 0, 1,  1, 0, 0, 0,   0, 0, 1, 0,  0,  0, 0, 0, 0,   0, 0, 0, 1 },
                                                                                                                                
            { 1, 0, 0, 0,   0, 0, 0, 0,  0,  0, 0, 0, 0,   0, 0, 0, 1,  1, 0, 0, 0,   0, 0, 0, 0,  0,  0, 0, 0, 0,   0, 0, 0, 1 },
            { 1, 0, 0, 0,   0, 0, 1, 0,  0,  1, 1, 1, 1,   0, 0, 0, 1,  1, 0, 0, 0,   0, 0, 0, 0,  0,  1, 1, 1, 1,   0, 0, 0, 1 },
            { 1, 1, 1, 1,   1, 1, 1, 1,  0,  1, 1, 1, 1,   1, 1, 1, 1,  1, 1, 1, 1,   1, 1, 1, 1,  0,  1, 1, 1, 1,   1, 1, 1, 1 },
                                                                                                                                
            { 1, 1, 1, 1,   1, 1, 1, 1,  0,  1, 1, 1, 1,   1, 1, 1, 1,  1, 1, 1, 1,   1, 1, 1, 1,  0,  1, 1, 1, 1,   1, 1, 1, 1 },
            { 1, 1, 0, 0,   0, 0, 1, 0,  0,  0, 0, 0, 0,   0, 0, 0, 1,  1, 1, 0, 0,   0, 0, 1, 0,  0,  0, 0, 0, 0,   0, 0, 0, 1 },
            { 1, 0, 1, 1,   1, 1, 0, 0,  0,  1, 0, 0, 0,   0, 0, 0, 1,  1, 0, 1, 1,   1, 1, 0, 0,  0,  1, 0, 0, 0,   0, 0, 0, 1 },
                                                                                                                                
            { 1, 0, 0, 0,   0, 0, 1, 0,  0,  0, 0, 0, 0,   0, 0, 0, 1,  1, 0, 0, 0,   0, 0, 1, 0,  0,  0, 0, 0, 0,   0, 0, 0, 1 },
            { 1, 0, 0, 0,   0, 0, 1, 0,  1,  0, 0, 0, 0,   0, 0, 0, 0,  0, 0, 0, 0,   0, 0, 1, 0,  1,  0, 0, 0, 0,   0, 0, 0, 1 },
            { 1, 0, 0, 0,   0, 0, 1, 0,  0,  0, 0, 0, 0,   0, 0, 0, 1,  1, 0, 0, 0,   0, 0, 1, 0,  0,  0, 0, 0, 0,   0, 0, 0, 1 },
                                                                                                                                
            { 1, 0, 0, 0,   0, 0, 0, 0,  0,  0, 0, 0, 0,   0, 0, 0, 1,  1, 0, 0, 0,   0, 0, 0, 0,  0,  0, 0, 0, 0,   0, 0, 0, 1 },
            { 1, 0, 0, 0,   0, 0, 0, 0,  0,  1, 1, 1, 1,   0, 0, 0, 1,  1, 0, 0, 0,   0, 0, 0, 0,  0,  1, 1, 1, 1,   0, 0, 0, 1 },
            { 1, 1, 1, 1,   1, 1, 1, 1,  0,  1, 1, 1, 1,   1, 1, 1, 1,  1, 1, 1, 1,   1, 1, 1, 1,  1,  1, 1, 1, 1,   1, 1, 1, 1 },
        };

    // clang-format on

    TileChunk tilechunks[1] = {};
    tilechunks[0].tiles = (u32 *)tiles;

    world.tilechunks = (TileChunk *)tilechunks;

    f32 player_width = world.tile_side_in_meters * 0.5;
    f32 player_height = world.tile_side_in_meters * 0.75;

    // NOTE(fede): Player movement and collision checking
    {
        WorldPosition new_player_pos = game_state->player_pos;
        new_player_pos.tile_rel_x += player_dx;
        new_player_pos.tile_rel_y += player_dy;

        WorldPosition new_player_left = new_player_pos;
        new_player_left.tile_rel_x -= player_width / 2;
        WorldPosition new_player_right = new_player_pos;
        new_player_right.tile_rel_x += player_width / 2;

        new_player_pos = recanonicalize_position(&world, new_player_pos);
        new_player_left = recanonicalize_position(&world, new_player_left);
        new_player_right = recanonicalize_position(&world, new_player_right);

        if (is_world_point_empty(&world, new_player_left) &&
            is_world_point_empty(&world, new_player_right)) {
            game_state->player_pos = new_player_pos;
        }
    }

    for (i32 rel_col = -10; rel_col < 10; rel_col++) {
        for (i32 rel_row = -20; rel_row < 20; rel_row++) {
    // for (u32 i = 0; i < 9; i++) {
    //     for (u32 j = 0; j < 17; j++) {

            f32 x = rel_col * world.tile_side_in_pixels;
            f32 y = -rel_row * world.tile_side_in_pixels;

            x -= world.meters_to_pixels * game_state->player_pos.tile_rel_x;
            y += world.meters_to_pixels * game_state->player_pos.tile_rel_y;

            x += display_buffer->width / 2;
            y += display_buffer->height / 2;

            u32 tile_value = get_tile_value(
                    &world,
                    rel_col + game_state->player_pos.abs_tile_x,
                    rel_row + game_state->player_pos.abs_tile_y);

            f32 gray = 0.2;
            gray += tile_value * 0.3; 

            f32 player_inside = rel_col == 0 && rel_row == 0 ? 0.3 : 0; 
            // f32 player_inside = 
            //     j == (game_state->player_pos.abs_tile_x & world.chunk_mask) && 
            //     i == (game_state->player_pos.abs_tile_y & world.chunk_mask) 
            //     ? 0.3 : 0;

            f32 green_tint = player_inside * 0.5;

            draw_rectangle(
                    display_buffer,
                    x,
                    y - world.tile_side_in_pixels,
                    x + world.tile_side_in_pixels,
                    y,
                    gray, gray + green_tint, gray);
        }
    }

    // NOTE(fede): Draw player.
    {
        f32 player_x = lower_left_x +
            world.meters_to_pixels * (
                game_state->player_pos.abs_tile_x * world.tile_side_in_meters +
                game_state->player_pos.tile_rel_x);

        f32 player_y = lower_left_y -
            world.meters_to_pixels * (
                game_state->player_pos.abs_tile_y * world.tile_side_in_meters +
                game_state->player_pos.tile_rel_y);

        player_x = display_buffer->width / 2;
        player_y = display_buffer->height / 2;

        f32 player_pixel_width = player_width * world.meters_to_pixels; 
        f32 player_pixel_height = player_height * world.meters_to_pixels; 

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
