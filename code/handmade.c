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

internal LoadedBitmap debug_load_bmp(ThreadContext *thread,
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

internal void debug_draw_bmp(LoadedBitmap *bitmap,
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

internal void draw_rectangle_rgba(GameDisplayBuffer *buffer, 
                             v2 real_min, v2 real_max,
                             f32 r, f32 g, f32 b, f32 a) {
    
    int min_x = round_f32_to_int(real_min.x);
    int min_y = round_f32_to_int(real_min.y);
    int max_x = round_f32_to_int(real_max.x);
    int max_y = round_f32_to_int(real_max.y);
    
    min_x = max(0, min_x);
    min_y = max(0, min_y);
    max_x = min((int)buffer->width, max_x);
    max_y = min((int)buffer->height, max_y);
    
    u8 a8 = (u8)round_f32_to_int(a * 0xFF);
    u8 r8 = (u8)round_f32_to_int(r * 0xFF);
    u8 g8 = (u8)round_f32_to_int(g * 0xFF);
    u8 b8 = (u8)round_f32_to_int(b * 0xFF);
    
    u32 color = a8 << 8  | r8;
    color = (color << 8) | g8;
    color = (color << 8) | b8;
    
    for (int y = min_y; y < max_y; y++) {
        for (int x = min_x; x < max_x; x++) {
            debug_draw_pixel(&buffer->data[y * buffer->width + x], color);
        }
    }
}


internal v2 closest_point_in_rectangle(v2 min_corner, 
                                       v2 max_corner, 
                                       v2 p) {
    v2 result = p;
    result.x = max(result.x, min_corner.x);
    result.x = min(result.x, max_corner.x);
    
    result.y = max(result.y, min_corner.y);
    result.y = min(result.y, max_corner.y);
    
    return result;
}

internal inline Entity *get_entity(GameState *game_state, u32 i) {
    assert(i < array_count(game_state->entities));
    
    return &game_state->entities[i];
}

#define get_player(game_state, i) \
get_entity(game_state, game_state->player_index_for_controller[i])

internal void initialize_player(GameState *game_state, Entity *entity) {
    *entity = (Entity){};
    entity->exists = true;
    entity->p = game_state->camera_pos;
    entity->height = 1.4f;
    entity->width = 0.75 * entity->height;
}

// TODO(fede): cleanup this function, too many args maybe.
//      r and r_val are for the reflection velocity, maybe we should handle this 
//      elsewhere.
internal void test_wall(f32 *t_min,
                        v2 p0,
                        v2 pd,
                        f32 wall_value,
                        f32 min_wall, 
                        f32 max_wall,
                        u32 elem_idx,
                        v2 *r, f32 r_val) {
    if (abs(pd.e[elem_idx]) == 0) {
        return;
    }
    
    f32 t_result = (wall_value - p0.e[elem_idx]) / pd.e[elem_idx];
    if (t_result < 0) 
        return;
    
    u32 other_elem_idx = (elem_idx + 1) % array_count(pd.e);
    
    f32 y = p0.e[other_elem_idx] + t_result * pd.e[other_elem_idx];
    if (y < min_wall || y > max_wall) {
        return;
    } 
    
    f32 t_epsilon = 0.01f;
    t_result -= t_epsilon;
    if (t_result < *t_min) {
        *t_min = t_result;
        r->e[elem_idx] = r_val;
        r->e[other_elem_idx] = 0;
    }
}

internal void update_player(GameState *game_state,
                            Entity *player,
                            v2 dd_p,
                            f32 dt_for_frame) {
    Tilemap *tilemap = game_state->world->tilemap;

    f32 dd_p_len2 = v2_length2(dd_p);
    if (dd_p_len2 > 1)
        dd_p = v2_sdiv(dd_p, sqrt_f32(dd_p_len2));

    // NOTE(fede): speed and friction
    dd_p = v2_smul(dd_p, player->speed);
    dd_p = v2_add(dd_p, v2_smul(player->d_p, -7.5));

    v2 d_p = player->d_p;
    v2 p_delta = v2_add(v2_smul(d_p, dt_for_frame),
            v2_smul(dd_p, 0.5f * square(dt_for_frame)));
    player->d_p = v2_add(d_p, 
            v2_smul(dd_p, dt_for_frame));


    f32 t_left = 1;
    u32 max_collision_iters = 2;
    TilemapPosition old_p, new_p;
    for (u32 i = 0; i < max_collision_iters; i++) {

        old_p = player->p;

        new_p = old_p;
        new_p.offset = v2_add(old_p.offset, p_delta);
        new_p = recanonicalize_position(tilemap, new_p);

        u32 start_tile_x = old_p.abs_tile_x;
        u32 start_tile_y = old_p.abs_tile_y;
        u32 end_tile_x = new_p.abs_tile_x;
        u32 end_tile_y = new_p.abs_tile_y;

        u32 delta_x = sign_i32((int)(end_tile_x - start_tile_x));
        u32 delta_y = sign_i32((int)(end_tile_y - start_tile_y));

        f32 t_min = 1;
        v2 r = {};

        u32 abs_tile_z = old_p.abs_tile_z;
        u32 abs_tile_y = start_tile_y;
        while (true) {
            u32 abs_tile_x = start_tile_x;
            while (true) {

                TilemapPosition tile_center = (TilemapPosition){
                    abs_tile_x = abs_tile_x,
                    abs_tile_y = abs_tile_y,
                    abs_tile_z = abs_tile_z,
                };

                u32 tile_value = get_tile_value_at_pos(tilemap, tile_center);

                if (!is_tile_value_empty(tile_value)) {
                    v2 min_corner = v2_smul((v2){
                            tilemap->tile_side_in_meters,
                            tilemap->tile_side_in_meters,
                            }, -0.5);

                    v2 max_corner = v2_smul((v2){
                            tilemap->tile_side_in_meters,
                            tilemap->tile_side_in_meters,
                            }, 0.5);

                    TilemapDifference rel_old_p = 
                        subtract_tilemap_positions(tilemap->tile_side_in_meters,
                                old_p, tile_center);
                    v2 rel = rel_old_p.dxy;

                    // TODO(fede): maybe handle r elsewhere.
                    test_wall(&t_min, rel, p_delta, min_corner.x, min_corner.y, max_corner.y, 0, &r, 1);
                    test_wall(&t_min, rel, p_delta, max_corner.x, min_corner.y, max_corner.y, 0, &r, -1);
                    test_wall(&t_min, rel, p_delta, min_corner.y, min_corner.x, max_corner.x, 1, &r, 1);
                    test_wall(&t_min, rel, p_delta, max_corner.y, min_corner.x, max_corner.x, 1, &r, -1);
                }

                if (abs_tile_x == end_tile_x)
                    break;
                else 
                    abs_tile_x += delta_x;
            }

            if (abs_tile_y == end_tile_y)
                break;
            else
                abs_tile_y += delta_y;
        }

        // TODO(fede): I feel that this is clunky, review.
        p_delta = v2_smul(p_delta, t_min);
        player->p.offset = v2_add(player->p.offset, p_delta);
        player->p = recanonicalize_position(tilemap, player->p);

        player->d_p = v2_add(
                player->d_p,
                v2_smul(r, -1 * v2_dot(player->d_p, r)));

        t_left = lerp(0, t_left, 1 - t_min);
        // STUDY(fede): Is this correct? i am doing `x1 = v*t + x0`
        p_delta = v2_smul(player->d_p, t_left * dt_for_frame);

        if (t_left <= 0.01)
            break;
    }

    // NOTE(fede): climb ladders
    if (!are_on_same_tile(player->p, old_p)) {
        u32 tile_val = get_tile_value_at_pos(tilemap, player->p);
        if (tile_val == 3) {
            player->p.abs_tile_z++;
        } else if (tile_val == 4) {
            player->p.abs_tile_z--;
        }
    }  
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
            game_state->backdrop = debug_load_bmp((ThreadContext *)0,
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
        
        game_state->camera_pos = get_room_center(tiles_per_width,
                                                 tiles_per_height,
                                                 (TilemapPosition){});
        
        initialize_arena(&game_state->world_arena,
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
            tilemap->tilechunks = push_array(&game_state->world_arena,
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
                door_left = true;
                door_bottom = true;

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
                        if (!last_screen && (border_left && center_height && door_left || 
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
                        
                        set_tile_value(&game_state->world_arena,
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
    
    for (u32 i = 0; i < HANDMADE_MAX_INPUTS; i++) {
        bool should_disconnect_entity = false;
        GameControllerInput *controller = get_game_controller(input, i);
        
        u32 player_idx = game_state->player_index_for_controller[i];
        Entity *player = get_entity(game_state, player_idx);
        
        // TODO(fede): Fix this!! I think that when we disconnect a controller, 
        //      we remove it, so it does not appear as is_connected or anything.
        if (!controller->is_connected) {
            if (player->exists)
                should_disconnect_entity = true;
            else 
                continue;
        }
        
        
        // TODO(fede): test multiplayer, this should not work because we do not 
        //      set the player entity index.
        if (!controller->start.ended_down && 
            controller->start.half_transition_count != 0) {
            if (player->exists) {
                should_disconnect_entity = true;
            } else {
                if (player_idx == 0) {
                    assert(game_state->entity_count < array_count(game_state->entities));
                    game_state->player_index_for_controller[i] = 
                        ++game_state->entity_count;
                    player = get_entity(game_state, game_state->player_index_for_controller[i]);
                }
                initialize_player(game_state, player);
            }
        }
        
        if (should_disconnect_entity) {
            player->exists = false; 
            for (u32 i = 0;
                 i < array_count(game_state->player_index_for_controller);
                 i++) {
                u32 new_camera_entity_idx =
                    game_state->player_index_for_controller[i];
                if (get_entity(game_state, new_camera_entity_idx)->exists) {
                    game_state->camera_following_entity_index = 
                        new_camera_entity_idx;
                }
            }
            
            continue;
        }
        
        if (game_state->camera_following_entity_index == 0) 
            game_state->camera_following_entity_index = player_idx;
        
        v2 dd_player = {};
        player->speed = 80;
        
        if (controller->is_analog) {
            v2 controller_stick = {
                .x = controller->avg_stick_x,
                .y = controller->avg_stick_y,
            };
            controller_stick = v2_smul(controller_stick, 4.0f);
            dd_player = v2_add(dd_player, controller_stick);
        } else {
            

            if (controller->move_up.ended_down) {
                dd_player.y = 1; 
            }
            
            if (controller->move_left.ended_down) {
                dd_player.x = -1;
            }
            
            if (controller->move_down.ended_down) {
                dd_player.y = -1;
            }
            
            if (controller->move_right.ended_down) {
                dd_player.x = 1;
            }

            // TODO(fede): Fix facing direction.
            //      inputs: 
            //          (hold)<- and (tap)->
            //      give facing direction '->'
            //
#define UPDATE_FACING_DIR(dir, i) \
            if (controller->move_##dir.ended_down && \
                    controller->move_##dir.half_transition_count != 0) { \
                player->facing_direction = i; }
            
            UPDATE_FACING_DIR(down, 0);
            UPDATE_FACING_DIR(up, 1);
            UPDATE_FACING_DIR(left, 2);
            UPDATE_FACING_DIR(right, 3);
#undef UPDATE_FACING_DIR

            if (controller->button_a.ended_down) 
                player->speed *= 5;
            
        }
        
        update_player(game_state, player, dd_player, input->dt_for_frame);
    }
    
    // NOTE(fede): Camera following entity movement
    Entity *camera_following_entity = get_entity(game_state, game_state->camera_following_entity_index);
    if (camera_following_entity) {
        TilemapPosition camera_following_p = camera_following_entity->p;
        if (!are_on_same_room(tiles_per_width,
                    tiles_per_height,
                    camera_following_p,
                    game_state->camera_pos)) {
            game_state->camera_pos = get_room_center(tiles_per_width,
                    tiles_per_height,
                    camera_following_p);
        }
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
    
    for (i32 rel_row = -10; rel_row < 10; rel_row++) {
        for (i32 rel_col = -10; rel_col < 10; rel_col++) {
            
            // NOTE(fede): Color
            f32 r, g, b;
            f32 gray, green_tint; 
            {
                r = 0.2;
                g = 0.2;
                b = 0.2;
                
                u32 tile_value = get_tile_value(tilemap,
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
                
                v2 v2_tile_side_in_pixels = v2_smul((v2){
                    tile_side_in_pixels,
                    tile_side_in_pixels,
                }, 1); // TODO(fede): remove *1 after full collision detection impl
                v2_tile_side_in_pixels = v2_smul(v2_tile_side_in_pixels, 0.5);
                min = v2_sub(center, v2_tile_side_in_pixels);
                max = v2_add(center, v2_tile_side_in_pixels);
            }
            
            draw_rectangle(display_buffer, 
                           min, max,
                           r, g, b);
        }
    }
    
    // TODO(fede): test multiplayer
    Entity *entity = &game_state->entities[1];
    for (u32 i = 0; i < game_state->entity_count; i++, entity++) {
        if (!entity->exists)
            continue;
        
        TilemapDifference entity_camera_space = subtract_tilemap_positions(tilemap->tile_side_in_meters,
                                                                           entity->p,
                                                                           game_state->camera_pos); 
        v2 entity_screen = entity_camera_space.dxy;
        entity_screen = v2_smul(entity_screen, meters_to_pixels);
        entity_screen.y *= -1;
        entity_screen = v2_add(entity_screen, screen_center);
        
#if HANDMADE_INTERNAL
        // NOTE(fede): draw entity
        {
            u32 facing_direction = entity->facing_direction;
            HeroBitmaps hero_bitmaps = game_state->hero_bitmaps[facing_direction];
            
            v2 align = {
                (f32)hero_bitmaps.align_x,
                (f32)hero_bitmaps.align_y,
            };
            
            debug_draw_bmp_align(&game_state->hero_shadow,
                                 display_buffer,
                                 entity_screen, align);
            
            debug_draw_bmp_align(&hero_bitmaps.torso,
                                 display_buffer,
                                 entity_screen, align);
            
            debug_draw_bmp_align(&hero_bitmaps.cape,
                                 display_buffer,
                                 entity_screen, align);
            
            debug_draw_bmp_align(&hero_bitmaps.head,
                                 display_buffer,
                                 entity_screen, align);
        }
        
        // NOTE(fede): debug draw entity ground point
        {
            draw_rectangle(display_buffer,
                           v2_sub(entity_screen, (v2){2, 2}),
                           v2_add(entity_screen, (v2){2, 2}),
                           1, 0, 0);
        }
        
        // NOTE(fede): debug draw entity velocity
        {
            v2 d_entity_pos_screen = v2_add(entity_screen,
                                            v2_smul(v2_vmul(entity->d_p, (v2){1, -1}), 10));
            
            draw_rectangle(display_buffer,
                           v2_sub(d_entity_pos_screen, (v2){2, 2}),
                           v2_add(d_entity_pos_screen, (v2){2, 2}),
                           0, 1, 0);
            
            draw_rectangle(display_buffer,
                           (v2){0, 0},
                           (v2){v2_length2(entity->d_p), 5},
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
