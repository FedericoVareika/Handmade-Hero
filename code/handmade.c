#include "handmade.h"
#include "handmade_random.h" 

#include "handmade_tile.c" 

internal void fill_audio_buffer(GameSoundOutputBuffer *buffer, int tone_hz) {
    i16 *sample_out = buffer->samples;
    for (u32 i = 0; i < buffer->sample_count; i++) {
        i16 sample_value = 0;
        *sample_out++ = sample_value;
        *sample_out++ = sample_value;
    }
}

internal void draw_rectangle(GameDisplayBuffer *buffer, 
        v2 real_min, v2 real_max,
        f32 r, f32 g, f32 b) {

    int min_x = round_f32_to_int(real_min.x);
    int min_y = round_f32_to_int(real_min.y);
    int max_x = round_f32_to_int(real_max.x);
    int max_y = round_f32_to_int(real_max.y);

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
    FindBitResult r_bitshift = find_least_significant_set_bit(masks->r);
    FindBitResult b_bitshift = find_least_significant_set_bit(masks->b);
    FindBitResult g_bitshift = find_least_significant_set_bit(masks->g);
    FindBitResult a_bitshift = find_least_significant_set_bit(masks->a);

    assert(r_bitshift.found); 
    assert(b_bitshift.found); 
    assert(g_bitshift.found); 
    assert(a_bitshift.found); 

    for (int y = 0; y < header->dib_header.bitmap_pixel_height; y++) {
        for (int x = 0; x < header->dib_header.bitmap_pixel_width; x++) {
            u32 pixel_encoded = *pixel;
            u32 pixel_argb = 
                ((pixel_encoded >> a_bitshift.index) & 0xFF) << 24 |
                ((pixel_encoded >> r_bitshift.index) & 0xFF) << 16 |
                ((pixel_encoded >> g_bitshift.index) & 0xFF) << 8  |
                ((pixel_encoded >> b_bitshift.index) & 0xFF) << 0;
            *(pixel++) = pixel_argb;
        }
    }

    return result;
}

internal inline f32 lerp(f32 a, f32 b, f32 t) {
    return a * (1-t) + b * t;
}

internal void debug_draw_pixel(u32 *pixel, u32 value) {
    u32 alpha = value >> 24; 

    // NOTE(fede): linear blending
    //      source * alpha + dest * (1 - alpha) 

    f32 t =  ((value >> 24) & 0xFF) / (f32)0xFF;
    f32 sr = ((value >> 16) & 0xFF);
    f32 sg = ((value >>  8) & 0xFF);
    f32 sb = ((value >>  0) & 0xFF);

    f32 dr = ((*pixel >> 16) & 0xFF);
    f32 dg = ((*pixel >>  8) & 0xFF);
    f32 db = ((*pixel >>  0) & 0xFF);

    f32 r = lerp(dr, sr, t);
    f32 g = lerp(dg, sg, t);
    f32 b = lerp(db, sb, t);

    u32 blended = 
        (value & 0xFF000000) | 
        ((u32)r & 0xFF) << 16 | 
        ((u32)g & 0xFF) <<  8 | 
        ((u32)b & 0xFF) <<  0; 

    // u32 multiplied_value = multiply_channels_rgb(value, alpha_fraction);
    // u32 multiplied_pixel = multiply_channels_rgb(*pixel, 1 - alpha_fraction);
    // // TODO(fede): test whether there is color bleeding 
    // // (channel exceedes 0xFF)
    // u32 blended = multiplied_value + multiplied_pixel; 

    *pixel = blended;
}

internal void debug_draw_bmp(
        LoadedBitmap *bitmap,
        GameDisplayBuffer *buffer,
        v2 top_left) {

    int min_x = round_f32_to_int(top_left.x);
    int min_y = round_f32_to_int(top_left.y);
    int max_x = round_f32_to_int(top_left.x + (f32)bitmap->width);
    int max_y = round_f32_to_int(top_left.y + (f32)bitmap->height);

    int bitmap_offset_x = -min(0, min_x);
    int bitmap_offset_y = -min(0, min_y);

    min_x = max(0, min_x);
    min_y = max(0, min_y);
    max_x = min((int)buffer->width, max_x);
    max_y = min((int)buffer->height, max_y);

    i32 bitmap_y = bitmap->height - 1 - bitmap_offset_y;
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

#define debug_draw_bmp_align(bitmap, buffer, top_left, align)  \
    debug_draw_bmp(bitmap, buffer, v2_sub(top_left, align))

#endif 

internal v2 closest_point_in_rectangle(
        v2 min_corner, 
        v2 max_corner, 
        v2 p) {
    v2 result = p;
    result.x = max(result.x, min_corner.x);
    result.x = min(result.x, max_corner.x);

    result.y = max(result.y, min_corner.y);
    result.y = min(result.y, max_corner.y);
    
    return result;
}

extern GAME_UPDATE_AND_RENDER(game_update_and_render) {
    assert(sizeof(GameState) <= memory->permanent_storage_size);
    GameState *game_state = (GameState *)memory->permanent_storage;

    u32 tiles_per_height = 9;
    u32 tiles_per_width = 17;

    if (!memory->is_initialized) {
        assert(&input->controllers[0]._end -
                &input->controllers[0].buttons[0] ==
                array_count(input->controllers[0].buttons));

        assert(&input->mouse_input._end -
                &input->mouse_input.buttons[0] ==
                array_count(input->mouse_input.buttons));

        // NOTE(fede): load bmp's
        {
            game_state->backdrop = debug_load_bmp(
                    (ThreadContext *)0,
                    memory->debug_platform_read_entire_file,
                    "data/test/test_background.bmp");

            {
                HeroBitmaps *bitmaps = game_state->hero_bitmaps;

                bitmaps->head = debug_load_bmp(0,
                        memory->debug_platform_read_entire_file,
                        "data/test/test_hero_front_head.bmp");
                bitmaps->cape = debug_load_bmp(0,
                        memory->debug_platform_read_entire_file,
                        "data/test/test_hero_front_cape.bmp");
                bitmaps->torso = debug_load_bmp(0,
                        memory->debug_platform_read_entire_file,
                        "data/test/test_hero_front_torso.bmp");
                bitmaps->align_x = 72;
                bitmaps->align_y = 182;
                bitmaps++;

                bitmaps->head = debug_load_bmp(0,
                        memory->debug_platform_read_entire_file,
                        "data/test/test_hero_back_head.bmp");
                bitmaps->cape = debug_load_bmp(0,
                        memory->debug_platform_read_entire_file,
                        "data/test/test_hero_back_cape.bmp");
                bitmaps->torso = debug_load_bmp(0,
                        memory->debug_platform_read_entire_file,
                        "data/test/test_hero_back_torso.bmp");
                bitmaps->align_x = 72;
                bitmaps->align_y = 182;
                bitmaps++;

                bitmaps->head = debug_load_bmp(0,
                        memory->debug_platform_read_entire_file,
                        "data/test/test_hero_left_head.bmp");
                bitmaps->cape = debug_load_bmp(0,
                        memory->debug_platform_read_entire_file,
                        "data/test/test_hero_left_cape.bmp");
                bitmaps->torso = debug_load_bmp(0,
                        memory->debug_platform_read_entire_file,
                        "data/test/test_hero_left_torso.bmp");
                bitmaps->align_x = 72;
                bitmaps->align_y = 182;
                bitmaps++;

                bitmaps->head = debug_load_bmp(0,
                        memory->debug_platform_read_entire_file,
                        "data/test/test_hero_right_head.bmp");
                bitmaps->cape = debug_load_bmp(0,
                        memory->debug_platform_read_entire_file,
                        "data/test/test_hero_right_cape.bmp");
                bitmaps->torso = debug_load_bmp(0,
                        memory->debug_platform_read_entire_file,
                        "data/test/test_hero_right_torso.bmp");
                bitmaps->align_x = 70;
                bitmaps->align_y = 182;
                bitmaps++;
            }

            game_state->hero_shadow = debug_load_bmp(0,
                    memory->debug_platform_read_entire_file,
                    "data/test/test_hero_shadow.bmp");
        }

        game_state->player_pos = (TilemapPosition){
            .abs_tile_x = 3,
            .abs_tile_y = 3,

            .offset = (v2){},
        };

        game_state->camera_pos = get_room_center(
                tiles_per_width,
                tiles_per_height,
                game_state->player_pos);

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

    v2 player_width_height = {
       0.75f * 1.4f,
       1.4f,
    };

    TilemapPosition old_player_pos = game_state->player_pos;

    for (int i = 0; i < HANDMADE_MAX_INPUTS; i++) {
        GameControllerInput *controller = get_game_controller(input, i);
        if (!controller->is_connected)
            continue;

        v2 dd_player_pos = {};
        f32 player_speed = 10;

        if (controller->is_analog) {
            // v2 controller_stick = {
            //     .x = controller->avg_stick_x,
            //     .y = controller->avg_stick_y,
            // };
            // controller_stick = v2_smul(controller_stick, 4.0f);
            // dd_player_pos = v2_add(dd_player_pos, controller_stick);
        } else {
            if (controller->move_up.ended_down) {
                dd_player_pos.y = 1; 
                game_state->hero_facing_direction = 1;
            }

            if (controller->move_left.ended_down) {
                dd_player_pos.x = -1;
                game_state->hero_facing_direction = 2;
            }

            if (controller->move_down.ended_down) {
                dd_player_pos.y = -1;
                game_state->hero_facing_direction = 0;
            }

            if (controller->move_right.ended_down) {
                dd_player_pos.x = 1;
                game_state->hero_facing_direction = 3;
            }

            if (controller->button_a.ended_down) 
                player_speed *= 5;

        }

        dd_player_pos = v2_norm(dd_player_pos);
        dd_player_pos = v2_smul(dd_player_pos, player_speed);
        dd_player_pos = v2_add(
            dd_player_pos,
            v2_smul(game_state->d_player_pos, -1.5));

        v2 d_player_pos = game_state->d_player_pos;

        v2 player_pos_delta = v2_add(
                v2_smul(d_player_pos, input->dt_for_frame),
                v2_smul(dd_player_pos, 0.5f * square(input->dt_for_frame)));

        TilemapPosition new_player_pos = game_state->player_pos;
        new_player_pos.offset = v2_add(new_player_pos.offset, player_pos_delta);

        game_state->d_player_pos = v2_add(
                d_player_pos, 
                v2_smul(dd_player_pos, input->dt_for_frame));

        new_player_pos = recanonicalize_position(tilemap, new_player_pos);
#if 0
        TilemapPosition new_player_left = new_player_pos;
        new_player_left.offset.x -= player_width_height.x / 2;
        new_player_left = recanonicalize_position(tilemap, new_player_left);

        TilemapPosition new_player_right = new_player_pos;
        new_player_right.offset.x += player_width_height.x / 2;
        new_player_right = recanonicalize_position(tilemap, new_player_right);

        bool collided = false;
        TilemapPosition collision_pos = {};

        if (!is_tilemap_point_empty(tilemap, new_player_pos)) {
            collided = true;
            collision_pos = new_player_pos;
        }

        if (!is_tilemap_point_empty(tilemap, new_player_left)) {
            collided = true;
            collision_pos = new_player_left;
        } 

        if (!is_tilemap_point_empty(tilemap, new_player_right)) {
            collided = true;
            collision_pos = new_player_right;
        } 

        if (collided) {
            v2 r = {};
            if (collision_pos.abs_tile_x < game_state->player_pos.abs_tile_x) {
                r = (v2){1, 0};
            }
            if (collision_pos.abs_tile_x > game_state->player_pos.abs_tile_x) {
                r = (v2){-1, 0};
            } 
            if (collision_pos.abs_tile_y < game_state->player_pos.abs_tile_y) {
                r = (v2){0, 1};
            } 
            if (collision_pos.abs_tile_y > game_state->player_pos.abs_tile_y) {
                r = (v2){0, -1};
            }

            game_state->d_player_pos = v2_add(
                    game_state->d_player_pos,
                    v2_smul(r, -1 * v2_dot(game_state->d_player_pos, r)));
        } else {
            game_state->player_pos = new_player_pos;
        }
#else

        // NOTE(fede): This is collision/movement for a player that is a single 
        //      point, the objective is to make him an ellipsis.
        //
        //      Also note that min_tile_* and one_past_max_tile_* do not cover 
        //      the edge cases that they wrap around the u32 limit. 
        //      For example: 
        //          old_player_pos.abs_tile_x = 0
        //          new_player_pos.abs_tile_x = 1 
        //      Therefore:
        //        min(old_player_pos.abs_tile_x - 1, new_player_pos.abs_tile_x - 1) = 0, not ~4bn
        //
        u32 min_tile_x = min(old_player_pos.abs_tile_x - 1, new_player_pos.abs_tile_x - 1);
        u32 min_tile_y = min(old_player_pos.abs_tile_y - 1, new_player_pos.abs_tile_y - 1);
        u32 one_past_max_tile_x = max(old_player_pos.abs_tile_x + 2, new_player_pos.abs_tile_x + 2);
        u32 one_past_max_tile_y = max(old_player_pos.abs_tile_y + 2, new_player_pos.abs_tile_y + 2);
        u32 abs_tile_z = old_player_pos.abs_tile_z;

        f32 best_distance2 = v2_length2(player_pos_delta);
        TilemapPosition best_player_pos = old_player_pos;

        for (u32 abs_tile_y = min_tile_y;
                abs_tile_y != one_past_max_tile_y;
                abs_tile_y++) {
            for (u32 abs_tile_x = min_tile_x;
                    abs_tile_x != one_past_max_tile_x;
                    abs_tile_x++) {

                TilemapPosition tile_center = (TilemapPosition){
                    abs_tile_x = abs_tile_x,
                    abs_tile_y = abs_tile_y,
                    abs_tile_z = abs_tile_z,
                };
                
                u32 tile_value = get_tile_value_at_pos(tilemap, tile_center);

                if (is_tile_value_empty(tile_value)) {
                    v2 min_corner = v2_smul((v2){
                            tilemap->tile_side_in_meters,
                            tilemap->tile_side_in_meters,
                        }, -0.5);

                    v2 max_corner = v2_smul((v2){
                            tilemap->tile_side_in_meters,
                            tilemap->tile_side_in_meters,
                        }, 0.5);

                    TilemapDifference rel_new_player_pos = 
                        subtract_tilemap_positions(
                            tilemap->tile_side_in_meters,
                            new_player_pos,
                            tile_center);

                    v2 test_p = closest_point_in_rectangle(
                            min_corner, max_corner, rel_new_player_pos.dxy);
                    
                    f32 test_distance2 = v2_length2(
                            v2_sub(test_p, rel_new_player_pos.dxy));

                    if (test_distance2 < best_distance2) {
                        best_distance2 = test_distance2;
                        TilemapPosition test_position = tile_center;
                        test_position.offset = test_p;
                        best_player_pos = test_position;
                        
                        // NOTE(fede): point is inside rectangle
                        if (test_p.x == rel_new_player_pos.dxy.x && 
                            test_p.y == rel_new_player_pos.dxy.y) {
                            break;
                        }
                    }
                }
            }
        }

        game_state->player_pos = best_player_pos;
#endif
    }

    if (!are_on_same_tile(game_state->player_pos, old_player_pos)) {
        u32 tile_val = get_tile_value_at_pos(tilemap, game_state->player_pos);
        if (tile_val == 3) {
            game_state->player_pos.abs_tile_z++;
            game_state->camera_pos.abs_tile_z++;
        } else if (tile_val == 4) {
            game_state->player_pos.abs_tile_z--;
            game_state->camera_pos.abs_tile_z--;
        }
    }

    if (!are_on_same_room(
                tiles_per_width,
                tiles_per_height,
                game_state->player_pos, old_player_pos)) {
        game_state->camera_pos = get_room_center(
                tiles_per_width,
                tiles_per_height,
                game_state->player_pos);
    }

#if HANDMADE_INTERNAL
    debug_draw_bmp(&game_state->backdrop, display_buffer, (v2){0, 0});
#else 
    draw_rectangle(display_buffer, 0, 0, display_buffer->width,
                   display_buffer->height, 1, 0, 1);
#endif

    f32 tile_side_in_pixels = 60;
    f32 meters_to_pixels = 
        tile_side_in_pixels / tilemap->tile_side_in_meters;
    f32 lower_left_x = -tile_side_in_pixels / 2;
    f32 lower_left_y = display_buffer->height;

    v2 screen_center = {
        .x = display_buffer->width / 2,
        .y = display_buffer->height / 2,
    };

    {

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
                            rel_col + game_state->camera_pos.abs_tile_x,
                            rel_row + game_state->camera_pos.abs_tile_y,
                            game_state->camera_pos.abs_tile_z);

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
                v2 min, max;
                {
                    v2 center = screen_center;
                    
                    // TODO(fede): port to v2
                    center.x += rel_col * tile_side_in_pixels;
                    center.y -= rel_row * tile_side_in_pixels;

                    v2 camera_offset = game_state->camera_pos.offset;
                    camera_offset = v2_smul(camera_offset, meters_to_pixels);
                    camera_offset.y *= -1;
                    center = v2_sub(center, camera_offset);

                    v2 v2_tile_side_in_pixels = {
                        tile_side_in_pixels,
                        tile_side_in_pixels,
                    };
                    v2_tile_side_in_pixels = v2_smul(v2_tile_side_in_pixels, 0.5);
                    min = v2_sub(center, v2_tile_side_in_pixels);
                    max = v2_add(center, v2_tile_side_in_pixels);
                }

                draw_rectangle(display_buffer, 
                        min, max,
                        r, g, b);
            }
        }
    }

    // NOTE(fede): Draw player.
    {
        TilemapDifference player_camera_space = subtract_tilemap_positions(
                tilemap->tile_side_in_meters,
                game_state->player_pos,
                game_state->camera_pos); 
        v2 player_screen = player_camera_space.dxy;
        player_screen = v2_smul(player_screen, meters_to_pixels);
        player_screen.y *= -1;
        player_screen = v2_add(player_screen, screen_center);
            
#if HANDMADE_INTERNAL
        // NOTE(fede): draw player
        {
            u32 facing_direction = game_state->hero_facing_direction;
            HeroBitmaps hero_bitmaps = game_state->hero_bitmaps[facing_direction];

            v2 align = {
                (f32)hero_bitmaps.align_x,
                (f32)hero_bitmaps.align_y,
            };

            debug_draw_bmp_align(
                    &game_state->hero_shadow,
                    display_buffer,
                    player_screen, align);

            debug_draw_bmp_align(
                    &hero_bitmaps.torso,
                    display_buffer,
                    player_screen, align);

            debug_draw_bmp_align(
                    &hero_bitmaps.cape,
                    display_buffer,
                    player_screen, align);

            debug_draw_bmp_align(
                    &hero_bitmaps.head,
                    display_buffer,
                    player_screen, align);
        }

        // NOTE(fede): draw player ground point
        {
            draw_rectangle(display_buffer,
                    v2_sub(player_screen, (v2){2, 2}),
                    v2_add(player_screen, (v2){2, 2}),
                    1, 0, 0);
        }

        // NOTE(fede): draw player velocity
        {
            v2 d_player_pos_screen = v2_add(
                player_screen,
                v2_smul(v2_vmul(game_state->d_player_pos, (v2){1, -1}), 10));

            draw_rectangle(display_buffer,
                    v2_sub(d_player_pos_screen, (v2){2, 2}),
                    v2_add(d_player_pos_screen, (v2){2, 2}),
                    0, 1, 0);
        }
#endif
    }
    
}

extern GAME_FILL_SOUND_BUFFER(game_fill_sound_buffer) {
    assert(sizeof(GameState) <= memory->permanent_storage_size);
    GameState *game_state = (GameState *)memory->permanent_storage;
    fill_audio_buffer(sound_buffer, 256);
}
