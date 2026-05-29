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

internal u32 get_tile_value_unchecked(World *world, Tilemap *tilemap, int x,
                                      int y) {
    assert(tilemap);
    assert(x >= 0);
    assert(x < world->count_x);
    assert(y >= 0);
    assert(y < world->count_y);

    return tilemap->tiles[y * world->count_x + x];
}

internal Tilemap *get_tilemap(World *world, int tilemap_x, int tilemap_y) {
    // clang-format off
    if (tilemap_x < 0 || tilemap_x >= world->tilemap_count_x || 
        tilemap_y < 0 || tilemap_y >= world->tilemap_count_y) 
        return 0;
    // clang-format on

    return &world->tilemaps[tilemap_y * world->tilemap_count_x + tilemap_x];
}

#define TILEMAP_WIDTH 17
#define TILEMAP_HEIGHT 9

internal bool is_tilemap_point_empty(World *world, Tilemap *tilemap, int x,
                                     int y) {
    if (!tilemap)
        return false;

    return !get_tile_value_unchecked(world, tilemap, x, y);
}

internal void recanonicalize_coord(
        World *world,
        int tile_count, 
        f32 *tile_rel,
        int *tile,
        int *tilemap) {
    f32 offset = floor_f32_to_int(*tile_rel / world->tile_side_in_meters); 

    *tile += offset;
    *tile_rel -= offset * world->tile_side_in_meters; 

    if (*tile >= tile_count) {
        (*tile) -= tile_count;
        ++*tilemap;
    }

    if (*tile < 0) {
        *tile += tile_count;
        --*tilemap;
    }

    assert(*tile_rel >= 0);
    assert(*tile >= 0);
    assert(*tilemap >= 0);

    assert(*tile_rel < world->tile_side_in_meters);
}

internal CanonicalPosition recanonicalize_position(
        World *world,
        CanonicalPosition can_pos) {
    CanonicalPosition result = can_pos;

    recanonicalize_coord(
            world,
            world->count_x,
            &result.tile_rel_x,
            &result.tile_x,
            &result.tilemap_x);
    recanonicalize_coord(
            world,
            world->count_y,
            &result.tile_rel_y,
            &result.tile_y,
            &result.tilemap_y);

    return result;
}

internal RawPosition get_raw_position(World *world, CanonicalPosition can_pos) {
    RawPosition result;
    result.tilemap_x = can_pos.tilemap_x;
    result.tilemap_y = can_pos.tilemap_y;

    result.x = can_pos.tile_x * world->tile_side_in_meters,
    result.y = can_pos.tile_y * world->tile_side_in_meters,

    result.x += can_pos.tile_rel_x;
    result.y += can_pos.tile_rel_y;

    result.x *= world->meters_to_pixels; 
    result.y *= world->meters_to_pixels;

    result.x += world->upper_left_x;
    result.y += world->upper_left_y;

    return result;
}

internal bool is_world_point_empty(World *world, CanonicalPosition can_pos) {
    Tilemap *tilemap = get_tilemap(world, can_pos.tilemap_x, can_pos.tilemap_y);
    if (!tilemap)
        return false;

    return is_tilemap_point_empty(world, tilemap, can_pos.tile_x,
                                  can_pos.tile_y);
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

        game_state->player_pos = (CanonicalPosition){
            .tilemap_x = 0,
            .tilemap_y = 0,

            .tile_x = TILEMAP_WIDTH / 2,
            .tile_y = TILEMAP_HEIGHT / 2,

            .tile_rel_x = TILE_SIDE_IN_METERS / 2,
            .tile_rel_y = TILE_SIDE_IN_METERS / 2,
        };

        memory->is_initialized = true;
    }

    World world = {};
    world.count_x = TILEMAP_WIDTH;
    world.count_y = TILEMAP_HEIGHT;
    world.tilemap_count_x = 2;
    world.tilemap_count_y = 2;

    world.tile_side_in_meters = TILE_SIDE_IN_METERS;
    world.tile_side_in_pixels = TILE_SIDE_IN_PIXELS;
    world.meters_to_pixels = 
        world.tile_side_in_pixels / world.tile_side_in_meters;

    world.upper_left_x = -world.tile_side_in_pixels / 2;
    world.upper_left_y = 0;

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
                player_dy -= 1; 
            }

            if (controller->move_left.ended_down) {
                player_dx -= 1;
            }

            if (controller->move_down.ended_down) {
                player_dy += 1;
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
    u32 tiles00[TILEMAP_HEIGHT][TILEMAP_WIDTH] =
        {
            { 1, 1, 1, 1,   1, 1, 1, 1,  1,  1, 1, 1, 1,   1, 1, 1, 1 },
            { 1, 1, 0, 0,   0, 0, 1, 0,  0,  0, 0, 0, 0,   0, 0, 0, 1 },
            { 1, 0, 1, 1,   1, 1, 0, 0,  0,  0, 0, 0, 0,   0, 0, 0, 1 },

            { 1, 0, 0, 0,   0, 0, 1, 0,  0,  0, 0, 0, 0,   0, 0, 0, 1 },
            { 1, 0, 0, 0,   0, 0, 1, 0,  0,  1, 0, 0, 0,   0, 0, 0, 0 },
            { 1, 0, 0, 0,   0, 0, 1, 0,  0,  0, 0, 0, 0,   0, 0, 0, 1 },

            { 1, 0, 0, 0,   0, 0, 0, 0,  0,  0, 0, 0, 0,   0, 0, 0, 1 },
            { 1, 0, 0, 0,   0, 0, 1, 0,  0,  1, 1, 1, 1,   0, 0, 0, 1 },
            { 1, 1, 1, 1,   1, 1, 1, 1,  0,  1, 1, 1, 1,   1, 1, 1, 1 },
        };

    u32 tiles10[TILEMAP_HEIGHT][TILEMAP_WIDTH] =
        {
            { 1, 1, 1, 1,   1, 1, 1, 1,  0,  1, 1, 1, 1,   1, 1, 1, 1 },
            { 1, 1, 0, 0,   0, 0, 1, 0,  0,  0, 0, 0, 0,   0, 0, 0, 1 },
            { 1, 0, 1, 1,   1, 1, 0, 0,  0,  1, 0, 0, 0,   0, 0, 0, 1 },

            { 1, 0, 0, 0,   0, 0, 1, 0,  0,  0, 0, 0, 0,   0, 0, 0, 1 },
            { 1, 0, 0, 0,   0, 0, 1, 0,  1,  0, 0, 0, 0,   0, 0, 0, 0 },
            { 1, 0, 0, 0,   0, 0, 1, 0,  0,  0, 0, 0, 0,   0, 0, 0, 1 },

            { 1, 0, 0, 0,   0, 0, 0, 0,  0,  0, 0, 0, 0,   0, 0, 0, 1 },
            { 1, 0, 0, 0,   0, 0, 0, 0,  0,  1, 1, 1, 1,   0, 0, 0, 1 },
            { 1, 1, 1, 1,   1, 1, 1, 1,  1,  1, 1, 1, 1,   1, 1, 1, 1 },
        };

    u32 tiles01[TILEMAP_HEIGHT][TILEMAP_WIDTH] =
        {
            { 1, 1, 1, 1,   1, 1, 1, 1,  1,  1, 1, 1, 1,   1, 1, 1, 1 },
            { 1, 1, 0, 0,   0, 0, 1, 0,  0,  0, 0, 0, 0,   0, 0, 0, 1 },
            { 1, 0, 1, 1,   1, 1, 0, 0,  0,  1, 0, 0, 0,   0, 0, 0, 1 },

            { 1, 0, 0, 0,   0, 0, 1, 0,  0,  0, 0, 0, 0,   0, 0, 0, 1 },
            { 0, 0, 0, 0,   0, 0, 1, 0,  1,  0, 0, 0, 0,   0, 0, 0, 1 },
            { 1, 0, 0, 0,   0, 0, 1, 0,  0,  0, 0, 0, 0,   0, 0, 0, 1 },

            { 1, 0, 0, 0,   0, 0, 0, 0,  0,  0, 0, 0, 0,   0, 0, 0, 1 },
            { 1, 0, 0, 0,   0, 0, 0, 0,  0,  1, 1, 1, 1,   0, 0, 0, 1 },
            { 1, 1, 1, 1,   1, 1, 1, 1,  0,  1, 1, 1, 1,   1, 1, 1, 1 },
        };

    u32 tiles11[TILEMAP_HEIGHT][TILEMAP_WIDTH] =
        {
            { 1, 1, 1, 1,   1, 1, 1, 1,  0,  1, 1, 1, 1,   1, 1, 1, 1 },
            { 1, 1, 0, 0,   0, 0, 1, 0,  0,  0, 0, 0, 0,   0, 0, 0, 1 },
            { 1, 0, 1, 1,   1, 1, 0, 0,  0,  1, 0, 0, 0,   0, 0, 0, 1 },

            { 1, 0, 0, 0,   0, 0, 1, 0,  0,  0, 0, 0, 0,   0, 0, 0, 1 },
            { 0, 0, 0, 0,   0, 0, 1, 0,  1,  0, 0, 0, 0,   0, 0, 0, 1 },
            { 1, 0, 0, 0,   0, 0, 1, 0,  0,  0, 0, 0, 0,   0, 0, 0, 1 },

            { 1, 0, 0, 0,   0, 0, 0, 0,  0,  0, 0, 0, 0,   0, 0, 0, 1 },
            { 1, 0, 0, 0,   0, 0, 0, 0,  0,  1, 1, 1, 1,   0, 0, 0, 1 },
            { 1, 1, 1, 1,   1, 1, 1, 1,  1,  1, 1, 1, 1,   1, 1, 1, 1 },
        };
    // clang-format on

    Tilemap tilemaps[2][2] = {};
    tilemaps[0][0].tiles = (u32 *)tiles00;
    tilemaps[1][0].tiles = (u32 *)tiles10;
    tilemaps[0][1].tiles = (u32 *)tiles01;
    tilemaps[1][1].tiles = (u32 *)tiles11;

    world.tilemaps = (Tilemap *)tilemaps;

    Tilemap *tilemap = get_tilemap(&world, game_state->player_pos.tilemap_x,
                                   game_state->player_pos.tilemap_y);

    for (int i = 0; i < world.count_y; i++) {
        for (int j = 0; j < world.count_x; j++) {
            f32 x = j * world.tile_side_in_pixels + world.upper_left_x;
            f32 y = i * world.tile_side_in_pixels + world.upper_left_y;

            u32 tilemap_value = get_tile_value_unchecked(
                    &world, tilemap, j, i);

            f32 gray = 0.2;
            gray += tilemap_value * 0.3; 

            f32 player_inside = 
                j == game_state->player_pos.tile_x && 
                i == game_state->player_pos.tile_y ? 0.3 : 0;

            f32 green_tint = player_inside * 0.5;

            draw_rectangle(
                    display_buffer,
                    x, y,
                    x + world.tile_side_in_pixels,
                    y + world.tile_side_in_pixels,
                    gray, gray + green_tint, gray);
        }
    }

    f32 player_width = world.tile_side_in_meters * 0.5;
    f32 player_height = world.tile_side_in_meters * 0.75;

    // NOTE(fede): Player movement and collision checking
    {
        CanonicalPosition new_player_pos = game_state->player_pos;
        new_player_pos.tile_rel_x += player_dx;
        new_player_pos.tile_rel_y += player_dy;

        CanonicalPosition new_player_left = new_player_pos;
        new_player_left.tile_rel_x -= player_width / 2;
        CanonicalPosition new_player_right = new_player_pos;
        new_player_right.tile_rel_x += player_width / 2;

        new_player_pos = recanonicalize_position(&world, new_player_pos);
        new_player_left = recanonicalize_position(&world, new_player_left);
        new_player_right = recanonicalize_position(&world, new_player_right);

        if (is_world_point_empty(&world, new_player_left) &&
            is_world_point_empty(&world, new_player_right)) {
            game_state->player_pos = new_player_pos;
        }
    }


    // NOTE(fede): Draw player.
    {
        RawPosition player_raw_pos =
            get_raw_position(&world, game_state->player_pos);

        f32 player_pixel_width = world.meters_to_pixels * player_width;
        f32 player_pixel_height = world.meters_to_pixels * player_height;

        f32 player_left = player_raw_pos.x - player_pixel_width / 2;
        f32 player_bottom = player_raw_pos.y;

        f32 player_right = player_raw_pos.x + player_pixel_width / 2;
        f32 player_top = player_raw_pos.y - player_pixel_height;

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
