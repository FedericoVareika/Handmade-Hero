#include "handmade.h"
#include "handmade_random.h" 

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

    u32 color = 0xFF << 8 | r8;
    color = (color << 8) | g8;
    color = (color << 8) | b8;

    for (int y = min_y; y < max_y; y++) {
        for (int x = min_x; x < max_x; x++) {
            buffer->data[y * buffer->width + x] = color;
        }
    }
}

#if HANDMADE_INTERNAL

internal u32 get_bitshift_from_mask(u32 mask) {
    u32 bitshift = 0;
    while ((mask & 1) == 0) {
        mask = mask >> 1;
        bitshift++;
    }

    return bitshift;
}

typedef struct __attribute__((packed)) {
    u16 filetype;
    u32 filesize;
    u16 reserved_1;
    u16 reserved_2;
    u32 data_offset;
} BmpFileHeader;

typedef struct __attribute__((packed)) {
    u32 dib_header_size;        // 40
    i32 bitmap_pixel_width;
    i32 bitmap_pixel_height;
    u16 n_color_planes;         // must be 1
    u16 bits_per_pixel;
    u32 compression;            // 0 if BI_RGB (most common)
    u32 raw_image_size;
    i32 horizontal_resolution;  // pixel per metre
    i32 vertical_resolution;    // pixel per metre
    u32 n_colors_in_palette;
    u32 n_important_colors;
} BmpDIBHeader;

typedef struct __attribute__((packed)) {
    u32 r, g, b, a;
} BmpBitfieldsMasks;


typedef struct __attribute__((packed)) {
    BmpFileHeader file_header;
    BmpDIBHeader dib_header;
} BmpHeader;

internal LoadedBitmap debug_load_bmp(
        ThreadContext *thread,
        DEBUGPlatformReadEntireFile *read_entire_file,
        char *filename) {
    LoadedBitmap result = {};

    DebugReadFileResult file = read_entire_file(thread, filename);
    
    if (file.size == 0) {
        // TODO(fede): handle error.
        return result;
    }

    BmpHeader *header = (BmpHeader *)file.memory;
    result.pixels = (u32 *)&((u8 *)file.memory)[header->file_header.data_offset];
    result.width = header->dib_header.bitmap_pixel_width;
    result.height = header->dib_header.bitmap_pixel_height;

    // NOTE(fede): Different masks between my GIMP compression and casey's.
    //          They both use compression 3 since they are RGBA.
    assert(header->dib_header.compression == 3);

    BmpBitfieldsMasks *masks = 
        (BmpBitfieldsMasks *)(((u8 *)file.memory) + sizeof(BmpHeader));

    u32 *pixel = result.pixels;
    u32 r_bitshift = get_bitshift_from_mask(masks->r);
    u32 b_bitshift = get_bitshift_from_mask(masks->b);
    u32 g_bitshift = get_bitshift_from_mask(masks->g);
    u32 a_bitshift = get_bitshift_from_mask(masks->a);

    for (int y = 0; y < header->dib_header.bitmap_pixel_height; y++) {
        for (int x = 0; x < header->dib_header.bitmap_pixel_width; x++) {
            u32 pixel_encoded = *pixel;
            u32 pixel_argb = 
                ((pixel_encoded & masks->a) >> a_bitshift) << 24 |
                ((pixel_encoded & masks->r) >> r_bitshift) << 16 |
                ((pixel_encoded & masks->g) >> g_bitshift) << 8 |
                ((pixel_encoded & masks->b) >> b_bitshift) << 0;
            *(pixel++) = pixel_argb;
        }
    }

    return result;
}

internal u32 multiply_channels_rgb(u32 argb, f32 k) {
    u32 r = (argb & 0xFF0000) >> 16;
    r = (u32)(r * k) & 0xFF;

    u32 g = (argb & 0x00FF00) >> 8;
    g = (u32)(g * k) & 0xFF;

    u32 b = (argb & 0x0000FF) >> 0;
    b = (u32)(b * k) & 0xFF;

    return argb & 0xFF000000 | 
        r << 16 | g << 8 | b; 
} 

internal void debug_draw_pixel(u32 *pixel, u32 value) {
    u32 alpha = value >> 24; 

    // NOTE(fede): blending
    //      source * alpha + dest * (1 - alpha) 

    f32 alpha_fraction = ((f32)alpha) / (f32)0xFF;
    u32 multiplied_value = multiply_channels_rgb(value, alpha_fraction);
    u32 multiplied_pixel = multiply_channels_rgb(*pixel, 1 - alpha_fraction);
    // TODO(fede): test whether there is color bleeding 
    // (channel exceedes 0xFF)
    u32 blended = multiplied_value + multiplied_pixel; 

    *pixel = blended;
}

internal void debug_draw_bmp(
        LoadedBitmap *bitmap,
        GameDisplayBuffer *buffer,
        f32 top_left_x,
        f32 top_left_y) {

    int min_x = round_f32_to_int(top_left_x);
    int min_y = round_f32_to_int(top_left_y);
    int max_x = round_f32_to_int(top_left_x + (f32)bitmap->width);
    int max_y = round_f32_to_int(top_left_y + (f32)bitmap->height);

    int bitmap_offset_x = -min(0, min_x);
    int bitmap_offset_y = -min(0, min_y);

    min_x = max(0, min_x);
    min_y = max(0, min_y);
    max_x = min((int)buffer->width, max_x);
    max_y = min((int)buffer->height, max_y);

    i32 bitmap_y = bitmap->height - bitmap_offset_y;
    for (int y = min_y; y < max_y; y++) {
        i32 bitmap_x = bitmap_offset_x;
        for (int x = min_x; x < max_x; x++) {
            u32 bmp_value = bitmap->pixels[bitmap_y * bitmap->width + bitmap_x];
            u32 *pixel = &buffer->data[y * buffer->width + x];
            debug_draw_pixel(pixel, bmp_value);
            bitmap_x++;
        }
        bitmap_y--;
    }
}

internal void debug_draw_bmp_centered_on_point(
        LoadedBitmap *bitmap,
        GameDisplayBuffer *display_buffer,
        f32 center_x,
        f32 center_y) {
    f32 top_left_x = center_x - (bitmap->width / 2);
    f32 top_left_y = center_y - (bitmap->height / 2);
    debug_draw_bmp(bitmap, display_buffer, top_left_x, top_left_y);
}

#endif 

extern GAME_UPDATE_AND_RENDER(game_update_and_render) {
    assert(sizeof(GameState) <= memory->permanent_storage_size);
    GameState *game_state = (GameState *)memory->permanent_storage;

    u32 tiles_per_height = 9;
    u32 tiles_per_width = 17;

    if (!memory->is_initialized) {
        game_state->backdrop = debug_load_bmp(
                (ThreadContext *)0,
                memory->debug_platform_read_entire_file,
                "data/test/test_background.bmp");

        game_state->hero_head = debug_load_bmp(
                (ThreadContext *)0,
                memory->debug_platform_read_entire_file,
                "data/test/test_hero_back_head.bmp");

        game_state->hero_cape = debug_load_bmp(
                (ThreadContext *)0,
                memory->debug_platform_read_entire_file,
                "data/test/test_hero_back_cape.bmp");

        game_state->hero_torso = debug_load_bmp(
                (ThreadContext *)0,
                memory->debug_platform_read_entire_file,
                "data/test/test_hero_back_torso.bmp");

        game_state->hero_shadow = debug_load_bmp(
                (ThreadContext *)0,
                memory->debug_platform_read_entire_file,
                "data/test/test_hero_shadow.bmp");

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

        World *world = push_struct(&game_state->world_arena, World);
        Tilemap *tilemap = push_struct(&game_state->world_arena, Tilemap);
        {
            tilemap->chunk_shift = 4;
            tilemap->chunk_mask = (1 << tilemap->chunk_shift) - 1;

            tilemap->chunk_dim = (1 << tilemap->chunk_shift);

            tilemap->tilechunk_count_x = 128;
            tilemap->tilechunk_count_y = 128;
            tilemap->tilechunk_count_z = 2;

            tilemap->tile_side_in_meters = 1.4;

            world->tilemap = tilemap;
        }
        game_state->world = world;

        // TODO(fede): alloc tilechunks automatically when set_tile_value is
        //          called. Already allocating tiles dynamically, but we should 
        //          store the tilechunks sparsely as well.
        {
            tilemap->tilechunks = push_array(
                    &game_state->world_arena,
                    TileChunk,
                    tilemap->tilechunk_count_x *
                    tilemap->tilechunk_count_y * 
                    tilemap->tilechunk_count_z);
        }

        // NOTE(fede): init tiles
        // TODO(fede): TERRIBLE
        {
            u32 n_screens = 10; 
            u32 screen_x = 0;
            u32 screen_y = 0;
            u32 screen_z = 0; 
            u32 random_number_idx = 0;

            u32 ladder_x = tiles_per_width / 2 + 1;
            u32 ladder_y = tiles_per_height / 2 + 2;

            bool door_left = false;
            bool door_bottom = false;
            bool prev_door_up = false;
            bool prev_door_down = false;

            for (u32 screen_idx = 0; screen_idx < n_screens; screen_idx++) {
                // TODO(fede): Good, real RNG.
                assert(random_number_idx < array_count(random_number_table));

                u32 random_choice = random_number_table[random_number_idx++];

                bool door_up = false, door_down = false;
                // NOTE(fede): limit to 2 floors
                if (prev_door_up || prev_door_down) {
                    random_choice %= 2;
                } else { 
                    random_choice %= 3;
                }
                
                // NOTE(fede): limit to 2 floors
                if (screen_z == 1) {
                    door_down = random_choice == 2;
                } else { 
                    door_up = random_choice == 2;
                }

                bool door_right = random_choice == 0;
                bool door_top = random_choice == 1;

                bool last_screen = screen_idx == n_screens - 1;

                for (u32 tile_y = 0; tile_y < tiles_per_height; tile_y++) {
                    for (u32 tile_x = 0; tile_x < tiles_per_width; tile_x++) {
                        u32 abs_tile_x = screen_x * tiles_per_width + tile_x;
                        u32 abs_tile_y = screen_y * tiles_per_height + tile_y;
                        u32 abs_tile_z = screen_z;

                        bool center_height = tile_y == tiles_per_height / 2;
                        bool center_width = tile_x == tiles_per_width / 2;

                        bool border_left = tile_x == 0;
                        bool border_right = tile_x == tiles_per_width - 1;

                        bool border_down = tile_y == 0;
                        bool border_up = tile_y == tiles_per_height - 1;

                        u32 val;
                        if (!last_screen && (
                                border_left && center_height && door_left || 
                                border_right && center_height && door_right ||
                                border_down && center_width && door_bottom ||
                                border_up && center_width && door_top))
                            // NOTE(fede): is door
                            val = 1;
                        else if (border_left ||
                                border_right ||
                                border_up ||
                                border_down)
                            // NOTE(fede): is border
                            val = 2;
                        else if (tile_x == ladder_x &&
                                tile_y == ladder_y &&
                                (door_up || prev_door_down))
                            val = 3;
                        else if (tile_x == ladder_x &&
                                tile_y == ladder_y &&
                                (door_down || prev_door_up))
                            val = 4;
                        else 
                            val = 1;

                        set_tile_value(
                                &game_state->world_arena,
                                tilemap, abs_tile_x, abs_tile_y, abs_tile_z,
                                val);
                    }
                }

                if (door_right) {
                    screen_x++;
                } else if (door_top) {
                    screen_y++;
                } else if (door_up) {
                    screen_z++;
                } else if (door_down) {
                    screen_z--;
                } else {
                    assert(screen_idx == n_screens - 1);
                }

                door_left = door_right;
                door_bottom = door_top;

                prev_door_down = door_down;
                prev_door_up = door_up;
            }
        }

        memory->is_initialized = true;
    }

    World *world = game_state->world;
    Tilemap *tilemap = world->tilemap;

    f32 player_dx = 0;
    f32 player_dy = 0;

    f32 player_velocity = 5;
    for (int i = 0; i < HANDMADE_MAX_INPUTS; i++) {
        GameControllerInput *controller = get_game_controller(input, i);
        if (!controller->is_connected)
            continue;

        if (controller->button_a.ended_down) 
            player_velocity *= 10;

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
            if (!are_on_same_tile(new_player_pos, game_state->player_pos)) {
                u32 tile_val = get_tile_value_at_pos(tilemap, new_player_pos);
                if (tile_val == 3)
                    new_player_pos.abs_tile_z++;
                else if (tile_val == 4)
                    new_player_pos.abs_tile_z--;
            }

            game_state->player_pos = new_player_pos;
        }

    }

#if HANDMADE_INTERNAL
    debug_draw_bmp(&game_state->backdrop, display_buffer, 0, 0);
#else 
    draw_rectangle(display_buffer, 0, 0, display_buffer->width,
                   display_buffer->height, 1, 0, 1);
#endif

    f32 tile_side_in_pixels = 60;
    f32 meters_to_pixels = 
        tile_side_in_pixels / tilemap->tile_side_in_meters;
    f32 lower_left_x = -tile_side_in_pixels / 2;
    f32 lower_left_y = display_buffer->height;

    {
        f32 screen_center_x = display_buffer->width / 2;
        f32 screen_center_y = display_buffer->height / 2;

        for (i32 rel_row = -10; rel_row < 10; rel_row++) {
            for (i32 rel_col = -10; rel_col < 10; rel_col++) {

                // NOTE(fede): Color
                f32 r, g, b;
                f32 gray, green_tint; 
                {
                    r = 0.2;
                    g = 0.2;
                    b = 0.2;

                    u32 tile_value = get_tile_value(
                            tilemap,
                            rel_col + game_state->player_pos.abs_tile_x,
                            rel_row + game_state->player_pos.abs_tile_y,
                            game_state->player_pos.abs_tile_z);

                    f32 player_inside = rel_col == 0 && rel_row == 0 ? 0.3 : 0; 
                    if (tile_value == 3 || tile_value == 4) {
                        r = (f32)0x66 / 0xFF; 
                        g = (f32)0x39 / 0xFF + player_inside * 0.3; 
                        b = (f32)0x00 / 0xFF; 
                        if (tile_value == 4) {
                            r *= 0.5;
                            g *= 0.5;
                            b *= 0.5;
                        }
                    } else if (tile_value == 1) {
                        r = 0.2;
                        g = 0.2 + player_inside * 0.3;
                        b = 0.2;
                        continue;
                    } else if (tile_value == 2) {
                        r = 0.2;
                        g = 0.2;
                        b = 0.2;
                    } else {
                        continue;
                    }
                }

                // NOTE(fede): Screen position 
                f32 min_x, min_y, max_x, max_y;
                {
                    f32 center_x = screen_center_x;
                    f32 center_y = screen_center_y;

                    center_x += rel_col * tile_side_in_pixels;
                    center_y -= rel_row * tile_side_in_pixels;

                    center_x -= meters_to_pixels * game_state->player_pos.tile_rel_x;
                    center_y += meters_to_pixels * game_state->player_pos.tile_rel_y;

                    min_x = center_x - tile_side_in_pixels * 0.5;
                    min_y = center_y + tile_side_in_pixels * 0.5;
                    max_x = center_x + tile_side_in_pixels * 0.5;
                    max_y = center_y - tile_side_in_pixels * 0.5;
                }

                draw_rectangle(display_buffer, 
                        min_x, max_y, max_x, min_y,
                        r, g, b);
            }
        }
    }

    // NOTE(fede): Draw player.
    {
        f32 player_x = lower_left_x + meters_to_pixels * (
                game_state->player_pos.abs_tile_x * tilemap->tile_side_in_meters +
                game_state->player_pos.tile_rel_x);

        f32 player_y = lower_left_y - meters_to_pixels * (
                game_state->player_pos.abs_tile_y * tilemap->tile_side_in_meters +
                game_state->player_pos.tile_rel_y);

        player_x = display_buffer->width / 2;
        player_y = display_buffer->height / 2;

        f32 player_pixel_width = player_width * meters_to_pixels; 
        f32 player_pixel_height = player_height * meters_to_pixels; 

        f32 player_left = player_x - player_pixel_width / 2;
        f32 player_bottom = player_y;

        f32 player_right = player_x + player_pixel_width / 2;
        f32 player_top = player_y - player_pixel_height;

#if HANDMADE_INTERNAL
        f32 bitmap_y = player_y - 73;
        debug_draw_bmp_centered_on_point(
                &game_state->hero_shadow,
                display_buffer,
                player_x, bitmap_y);

        debug_draw_bmp_centered_on_point(
                &game_state->hero_torso,
                display_buffer,
                player_x, bitmap_y);

        debug_draw_bmp_centered_on_point(
                &game_state->hero_cape,
                display_buffer,
                player_x, bitmap_y);

        debug_draw_bmp_centered_on_point(
                &game_state->hero_head,
                display_buffer,
                player_x, bitmap_y);

#else 
        draw_rectangle(display_buffer,
                player_left,
                player_top,
                player_right,
                player_bottom,
                1, 1, 1);
#endif
    }
    
}

extern GAME_FILL_SOUND_BUFFER(game_fill_sound_buffer) {
    assert(sizeof(GameState) <= memory->permanent_storage_size);
    GameState *game_state = (GameState *)memory->permanent_storage;
    fill_audio_buffer(sound_buffer, 256);
}
