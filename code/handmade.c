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
    int max_x = round_f32_to_int(top_left.x) + bitmap->width;
    int max_y = round_f32_to_int(top_left.y) + bitmap->height;

    int bitmap_offset_x = -min(0, min_x);
    int bitmap_offset_y = -min(0, min_y);

    min_x = max(0, min_x);
    min_y = max(0, min_y);
    max_x = min((int)buffer->width, max_x);
    max_y = min((int)buffer->height, max_y);

    i32 bitmap_y = bitmap->height - 1 - bitmap_offset_y;
    assert(bitmap_y + 1 >= max_y - min_y);
    for (int y = min_y; y < max_y; y++) {
        i32 bitmap_x = bitmap_offset_x;
        for (int x = min_x; x < max_x; x++) {
            u32 bmp_value = bitmap->pixels[bitmap_y * bitmap->width + bitmap_x];
            u32 *pixel = &buffer->data[y * buffer->width + x];
            debug_draw_pixel(pixel, bmp_value);
            bitmap_x++;
        }
        assert(bitmap_y >= 0);
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

internal inline EntityResidence get_entity_residence(
        GameState *game_state,
        u32 entity_idx) {
    // TODO(fede): check if this could be called before adding the entity
    assert(entity_idx <= game_state->entity_count); 

    return game_state->entity_residencies[entity_idx];
}

internal inline void change_entity_residence(
        GameState *game_state,
        u32 entity_idx,
        EntityResidence residence) {
    assert(entity_idx <= game_state->entity_count); 
    if (residence == EntityResidence_high) {
        if (get_entity_residence(game_state, entity_idx) != EntityResidence_high) {
            DormantEntity *dormant = &game_state->dormant_entities[entity_idx];
            HighEntity *high = &game_state->high_entities[entity_idx];
            TilemapDifference diff = subtract_tilemap_positions(
                    game_state->world->tilemap->tile_side_in_meters,
                    dormant->p,
                    game_state->camera_pos);

            high->p = diff.dxy;
            // TODO(fede): Updating z coord should be cleaner?
            high->abs_tile_z = dormant->p.abs_tile_z;
        }
    }

    game_state->entity_residencies[entity_idx] = residence;
}

// TODO(fede): add EntityResidence filter
internal inline Entity get_entity(
        GameState *game_state,
        u32 entity_idx,
        EntityResidence residence) {
    assert(entity_idx < array_count(game_state->entity_residencies));

    Entity entity = {
        .idx = entity_idx,
    };
    if (entity_idx <= game_state->entity_count) {
        entity.residence = game_state->entity_residencies[entity_idx];
        entity.dormant   = &game_state->dormant_entities[entity_idx];
        entity.low       = &game_state->low_entities[entity_idx];
        entity.high      = &game_state->high_entities[entity_idx];
    }

    residence = max(residence, entity.residence);
    change_entity_residence(game_state, entity_idx, residence);

    return entity;
}

internal u32 add_entity(GameState *game_state) {
    assert(game_state->entity_count < array_count(game_state->entity_residencies));
    u32 entity_idx = ++game_state->entity_count;
    game_state->entity_residencies[entity_idx] = EntityResidence_nonexistant;
    game_state->dormant_entities[entity_idx] = (DormantEntity){};
    game_state->low_entities[entity_idx] = (LowEntity){};
    game_state->high_entities[entity_idx] = (HighEntity){};

    return entity_idx;
}

internal void initialize_player(GameState *game_state, u32 entity_idx) {
    Entity player = get_entity(game_state, entity_idx, EntityResidence_dormant);
    player.dormant->type = EntityType_hero;
    player.dormant->p = game_state->camera_pos;
    player.dormant->width = 1.0f;
    player.dormant->height = 0.5f;
    player.dormant->collides = true;

    change_entity_residence(game_state, entity_idx, EntityResidence_high);
}

internal void initialize_wall(
        GameState *game_state,
        u32 entity_idx,
        u32 abs_tile_x,
        u32 abs_tile_y,
        u32 abs_tile_z) {
    Entity wall = get_entity(game_state, entity_idx, EntityResidence_dormant);
    wall.dormant->type = EntityType_wall;
    wall.dormant->p.abs_tile_x = abs_tile_x;
    wall.dormant->p.abs_tile_y = abs_tile_y;
    wall.dormant->p.abs_tile_z = abs_tile_z;
    wall.dormant->width = game_state->world->tilemap->tile_side_in_meters;
    wall.dormant->height = wall.dormant->width;
    wall.dormant->collides = true;
    change_entity_residence(game_state, entity_idx, EntityResidence_high);
}

// TODO(fede): cleanup this function, too many args maybe.
internal bool test_wall(f32 *t_min,
                        v2 p0,
                        v2 pd,
                        f32 wall_value,
                        f32 min_wall,
                        f32 max_wall,
                        u32 elem_idx) {
    if (abs(pd.e[elem_idx]) == 0) {
        return false;
    }

    f32 t_result = (wall_value - p0.e[elem_idx]) / pd.e[elem_idx];
    if (t_result < 0)
        return false;

    u32 other_elem_idx = (elem_idx + 1) % array_count(pd.e);

    f32 y = p0.e[other_elem_idx] + t_result * pd.e[other_elem_idx];
    if (y < min_wall || y > max_wall) {
        return false;
    }

    f32 t_epsilon = 0.01f;
    t_result -= t_epsilon;
    if (t_result < *t_min) {
        *t_min = t_result;
        return true;
    }

    return false;
}

internal void update_player(GameState *game_state,
                            Entity player,
                            v2 dd_p,
                            f32 dt_for_frame) {
    f32 dd_p_len2 = v2_length2(dd_p);
    if (dd_p_len2 > 1)
        dd_p = v2_sdiv(dd_p, sqrt_f32(dd_p_len2));

    // NOTE(fede): speed and friction
    dd_p = v2_smul(dd_p, player.high->speed);
    dd_p = v2_add(dd_p, v2_smul(player.high->d_p, -7.5));

    v2 d_p = player.high->d_p;
    v2 p_delta = v2_add(v2_smul(d_p, dt_for_frame),
            v2_smul(dd_p, 0.5f * square(dt_for_frame)));
    player.high->d_p = v2_add(d_p,
            v2_smul(dd_p, dt_for_frame));

    f32 t_left = 1;
    u32 max_collision_iters = 4;
    v2 old_p = player.high->p; 
    v2 new_p;
    for (u32 i = 0; i < max_collision_iters && t_left > 0; i++) {
        new_p = player.high->p;
        new_p = v2_add(new_p, p_delta);

        f32 t_min = 1;
        v2 wall_normal = {};

        u32 hit_entity_index = 0;

        for (u32 test_entity_idx = 1;
                test_entity_idx <= game_state->entity_count;
                test_entity_idx++) {
            if (test_entity_idx == player.idx) 
                continue;

            Entity test_entity = get_entity(
                    game_state,
                    test_entity_idx,
                    EntityResidence_high);

            // TODO(fede): Checking z coord should be cleaner
            if (!test_entity.dormant->collides || 
                    test_entity.high->abs_tile_z != player.high->abs_tile_z)
                continue;

            f32 diameter_w = test_entity.dormant->width + player.dormant->width; 
            f32 diameter_h = test_entity.dormant->height + player.dormant->height; 

            v2 min_corner = v2_smul((v2){
                    diameter_w,
                    diameter_h,
                    }, -0.5);

            v2 max_corner = v2_smul((v2){
                    diameter_w,
                    diameter_h,
                    }, 0.5);

            v2 rel = v2_sub(player.high->p, test_entity.high->p);

            // TODO(fede): maybe handle wall_normal elsewhere.
            if (test_wall(&t_min, rel, p_delta, min_corner.x, min_corner.y, max_corner.y, 0)) {
                wall_normal = (v2){-1, 0};
                hit_entity_index = test_entity_idx;
            }

            if (test_wall(&t_min, rel, p_delta, max_corner.x, min_corner.y, max_corner.y, 0)) {
                wall_normal = (v2){1, 0};
                hit_entity_index = test_entity_idx;
            }

            if (test_wall(&t_min, rel, p_delta, min_corner.y, min_corner.x, max_corner.x, 1)) {
                wall_normal = (v2){0, -1};
                hit_entity_index = test_entity_idx;
            }
            
            if (test_wall(&t_min, rel, p_delta, max_corner.y, min_corner.x, max_corner.x, 1)) {
                wall_normal = (v2){0, 1};
                hit_entity_index = test_entity_idx;
            }
        }

        player.high->p = v2_add(player.high->p, v2_smul(p_delta, t_min));

        if (hit_entity_index != 0) {
            v2 remaining_p_delta = v2_smul(p_delta, 1 - t_min);

            player.high->d_p = reflect(player.high->d_p, wall_normal, 0);
            p_delta = reflect(remaining_p_delta, wall_normal, 0);

            Entity hit_entity = get_entity(
                    game_state,
                    hit_entity_index,
                    EntityResidence_dormant);
            // TODO(fede): handle high->abs_tile_z change?
            player.high->abs_tile_z += hit_entity.dormant->d_abs_tile_z;
        }

        t_left -= t_min * t_left; 
    }

    player.dormant->p = map_into_tile_space(
            game_state->world->tilemap,
            game_state->camera_pos,
            player.high->p);
}

internal void set_camera(
        GameState *game_state,
        TilemapPosition new_camera_p, 
        u32 tiles_per_width, 
        u32 tiles_per_height) {

    Tilemap *tilemap = game_state->world->tilemap;
    f32 tile_side_in_meters = tilemap->tile_side_in_meters;

    v2 entity_offset = subtract_tilemap_positions(
            tile_side_in_meters,
            game_state->camera_pos,
            new_camera_p).dxy;

    u32 tile_span_x = floor_f32_to_int((f32)tiles_per_width / 2) * 2;
    u32 tile_span_y = floor_f32_to_int((f32)tiles_per_height / 2) * 2;

    // TODO(fede): changed to i32 because of wrapping, though it would not work 
    //          at the middle of the world.
    i32 min_tile_x = new_camera_p.abs_tile_x - tile_span_x; 
    i32 max_tile_x = new_camera_p.abs_tile_x + tile_span_x; 
    i32 min_tile_y = new_camera_p.abs_tile_y - tile_span_y; 
    i32 max_tile_y = new_camera_p.abs_tile_y + tile_span_y; 

    Rect2 camera_bounds = rect2_min_max(
            v2_smul((v2){tile_span_x, tile_span_y}, -tile_side_in_meters),
            v2_smul((v2){tile_span_x, tile_span_y}, tile_side_in_meters));
            
    game_state->camera_pos = new_camera_p;

    for (u32 entity_idx = 1; 
            entity_idx <= game_state->entity_count;
            entity_idx++) {
        if (get_entity_residence(game_state, entity_idx) != EntityResidence_high)
            continue;

        HighEntity *high = &game_state->high_entities[entity_idx];
        high->p = v2_add(high->p, entity_offset);

        if (!inside_rect2(camera_bounds, high->p) ||
                high->abs_tile_z != new_camera_p.abs_tile_z) {
            change_entity_residence(game_state, entity_idx, EntityResidence_dormant);
        } 
    }

    for (u32 entity_idx = 1; 
            entity_idx <= game_state->entity_count;
            entity_idx++) {
        if (get_entity_residence(game_state, entity_idx) != EntityResidence_dormant)
            continue;

        DormantEntity *dormant = &game_state->dormant_entities[entity_idx];
        // TODO(fede): changed to i32 because of wrapping, though it would not work 
        //          at the middle of the world.
        if (dormant->p.abs_tile_z == new_camera_p.abs_tile_z && 
                (i32)dormant->p.abs_tile_x >= min_tile_x &&  
                (i32)dormant->p.abs_tile_x <= max_tile_x && 
                (i32)dormant->p.abs_tile_y >= min_tile_y &&  
                (i32)dormant->p.abs_tile_y <= max_tile_y) {
            change_entity_residence(game_state, entity_idx, EntityResidence_high);
        }
    }
}

extern GAME_UPDATE_AND_RENDER(game_update_and_render) {
    assert(sizeof(GameState) <= memory->permanent_storage_size);
    GameState *game_state = (GameState *)memory->permanent_storage;

    u32 tiles_per_height = 9;
    u32 tiles_per_width = 17;

    if (!memory->is_initialized) {
        assert(&input->controllers[0].end_ -
               &input->controllers[0].buttons[0] ==
               array_count(input->controllers[0].buttons));

        assert(&input->mouse_input.end_ -
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

        // NOTE(fede): DEBUG add zombie entity
#if 0
        {
            u32 zombie_idx = add_entity(game_state);
            initialize_player(game_state, zombie_idx);
            Entity zombie = get_entity(game_state, zombie_idx, EntityResidence_high);
            zombie.high->p = v2_add(zombie.high->p, (v2){tilemap->tile_side_in_meters, tilemap->tile_side_in_meters});
            zombie.dormant->p = map_into_tile_space(
                    game_state->world->tilemap,
                    game_state->camera_pos,
                    zombie.high->p);
        }
#endif 

        // NOTE(fede): init tiles
        // TODO(fede): TERRIBLE
        {
            u32 n_screens = 5;
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
                        
                        EntityType type;
                        if (!last_screen && (border_right && center_height && door_right ||
                                             border_up && center_width && door_top) ||
                                border_left && center_height && door_left ||
                                border_down && center_width && door_bottom)
                            // NOTE(fede): is door
                            type = EntityType_none;
                        else if (border_left ||
                                 border_right ||
                                 border_up ||
                                 border_down)
                            // NOTE(fede): is border
                            type = EntityType_wall;
                        else if (tile_x == ladder_x &&
                                 tile_y == ladder_y &&
                                 (door_up || prev_door_down))
                            type = EntityType_ladder;
                        else if (tile_x == ladder_x &&
                                 tile_y == ladder_y &&
                                 (door_down || prev_door_up))
                            type = EntityType_ladder;
                        else
                            type = EntityType_none;

                        if (type) {
                            u32 wall_entity_idx = add_entity(game_state);
                            switch (type) {
                            case EntityType_none:  
                                break;
                            case EntityType_wall:  
                                initialize_wall(game_state, wall_entity_idx,
                                        abs_tile_x, abs_tile_y, abs_tile_z);
                                break;
                            case EntityType_ladder:  
                                initialize_wall(game_state, wall_entity_idx,
                                        abs_tile_x, abs_tile_y, abs_tile_z);
                                Entity ladder = get_entity(
                                        game_state,
                                        wall_entity_idx,
                                        EntityResidence_dormant);
                                ladder.dormant->d_abs_tile_z = door_up ? 1 : -1;
                                ladder.dormant->type = EntityType_ladder;
                                break;
                            default:
                                assert(false);
                            }
                        }
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

        game_state->tile_side_in_pixels = 60;

        TilemapPosition new_camera_p = get_room_center(
                tiles_per_width,
                tiles_per_height,
                (TilemapPosition){});
        set_camera(game_state, new_camera_p, tiles_per_width, tiles_per_height);

        memory->is_initialized = true;
    }

    World *world = game_state->world;
    Tilemap *tilemap = world->tilemap;

    for (u32 i = 0; i < HANDMADE_MAX_INPUTS; i++) {
        bool should_disconnect_entity = false;
        GameControllerInput *controller = get_game_controller(input, i);

#define BUTTON_TAP(button) !controller->button.ended_down && controller->button.half_transition_count
#define BUTTON_HOLD(button) controller->button.ended_down

        u32 player_idx = game_state->player_index_for_controller[i];
        EntityResidence player_residence = get_entity_residence(
                game_state,
                player_idx);

        // TODO(fede): Fix this!! I think that when we disconnect a controller,
        //      we remove it, so it does not appear as is_connected or anything.
        if (BUTTON_TAP(start)) {
            if (player_residence) {
                should_disconnect_entity = true;
            } else {
                if (player_idx == 0) {
                    player_idx = add_entity(game_state);
                    game_state->player_index_for_controller[i] = player_idx;
                }

                initialize_player(game_state, player_idx);
                player_residence = EntityResidence_high;
            }
        }

        if (!player_residence) {
            continue;
        }

        if (should_disconnect_entity) {
            change_entity_residence(game_state, player_idx, EntityResidence_nonexistant);
            game_state->camera_following_entity_index = 0;

            for (u32 i = 0;
                 i < array_count(game_state->player_index_for_controller);
                 i++) {
                u32 new_camera_entity_idx =
                    game_state->player_index_for_controller[i];
                if (get_entity_residence(game_state, new_camera_entity_idx) !=
                        EntityResidence_nonexistant) {
                    game_state->camera_following_entity_index =
                        new_camera_entity_idx;
                }
            }

            continue;
        }

        Entity player = get_entity(game_state, player_idx, EntityResidence_high);

        if (game_state->camera_following_entity_index == 0)
            game_state->camera_following_entity_index = player_idx;

        v2 dd_player = {};
        player.high->speed = 80;

        if (controller->is_analog) {
            v2 controller_stick = {
                .x = controller->avg_stick_x,
                .y = controller->avg_stick_y,
            };
            controller_stick = v2_smul(controller_stick, 4.0f);
            dd_player = v2_add(dd_player, controller_stick);
        } else {


            if (BUTTON_HOLD(move_up)) {
                dd_player.y = 1;
            }

            if (BUTTON_HOLD(move_left)) {
                dd_player.x = -1;
            }

            if (BUTTON_HOLD(move_down)) {
                dd_player.y = -1;
            }

            if (BUTTON_HOLD(move_right)) {
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
                player.high->facing_direction = i; }

            UPDATE_FACING_DIR(down, 0);
            UPDATE_FACING_DIR(up, 1);
            UPDATE_FACING_DIR(left, 2);
            UPDATE_FACING_DIR(right, 3);
#undef UPDATE_FACING_DIR

        }

        if (BUTTON_HOLD(button_a))
            player.high->speed *= 5;

        // NOTE(fede): DEBUG change controlling entity 
        if (BUTTON_TAP(button_x)) {
            game_state->player_index_for_controller[i]++;
            if (game_state->player_index_for_controller[i] > game_state->entity_count) {
                game_state->player_index_for_controller[i] = 1;
            }
        }

        if (BUTTON_TAP(button_b)) {
            game_state->tile_side_in_pixels -= 1;
        }

        if (BUTTON_TAP(button_y)) {
            game_state->tile_side_in_pixels += 1;
        }

        update_player(game_state, player, dd_player, input->dt_for_frame);
    }

    // NOTE(fede): Camera following entity movement
    if (game_state->camera_following_entity_index != 0) {

        assert(get_entity_residence(game_state, game_state->camera_following_entity_index) != EntityResidence_nonexistant);

        Entity camera_following_entity = get_entity(game_state,
                game_state->camera_following_entity_index, EntityResidence_high);

        TilemapPosition new_camera_p = game_state->camera_pos;
        v2 camera_following_p = camera_following_entity.high->p;
        f32 tile_side_in_meters = tilemap->tile_side_in_meters;

        if (-camera_following_p.x * 2 > tiles_per_width * tile_side_in_meters) {
            new_camera_p.abs_tile_x -= tiles_per_width; 
        }

        if (camera_following_p.x * 2 > tiles_per_width * tile_side_in_meters) {
            new_camera_p.abs_tile_x += tiles_per_width; 
        }

        if (-camera_following_p.y * 2 > tiles_per_height * tile_side_in_meters) {
            new_camera_p.abs_tile_y -= tiles_per_height; 
        }

        if (camera_following_p.y * 2 > tiles_per_height * tile_side_in_meters) {
            new_camera_p.abs_tile_y += tiles_per_height; 
        }

        set_camera(game_state, new_camera_p, tiles_per_width, tiles_per_height);
    }

#if HANDMADE_INTERNAL
    debug_draw_bmp(&game_state->backdrop, display_buffer, (v2){0, 0});
#else
    draw_rectangle(display_buffer, 0, 0, display_buffer->width,
                   display_buffer->height, 1, 0, 1);
#endif

    f32 tile_side_in_pixels = game_state->tile_side_in_pixels;
    f32 meters_to_pixels =
        tile_side_in_pixels / tilemap->tile_side_in_meters;
    f32 lower_left_x = -tile_side_in_pixels / 2;
    f32 lower_left_y = display_buffer->height;

    v2 screen_center = {
        .x = display_buffer->width / 2,
        .y = display_buffer->height / 2,
    };

    // TODO(fede): test multiplayer
    for (u32 entity_idx = 1;
            entity_idx <= game_state->entity_count;
            entity_idx++) {

        if (get_entity_residence(game_state, entity_idx) != EntityResidence_high) {
            continue;
        }

        Entity entity = get_entity(game_state, entity_idx, EntityResidence_high);

        v2 entity_screen = entity.high->p;
        entity_screen = v2_smul(entity_screen, meters_to_pixels);
        entity_screen.y *= -1;
        entity_screen = v2_add(entity_screen, screen_center);

#if HANDMADE_INTERNAL
        // NOTE(fede): draw entity
        if (entity.dormant->type == EntityType_hero) {
            u32 facing_direction = entity.high->facing_direction;
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

            // NOTE(fede): debug draw entity velocity
            {
                v2 pixel_d_p = v2_smul(entity.high->d_p, meters_to_pixels / 10);
                pixel_d_p.y *= -1;

                v2 screen_d_p = v2_add(entity_screen, pixel_d_p);
                draw_rectangle(display_buffer,
                        v2_sub(screen_d_p, (v2){4, 4}),
                        v2_add(screen_d_p, (v2){4, 4}),
                        1, 0, 0);
            }
        }

        if (entity.dormant->type == EntityType_wall) {
            v2 width_height_pixels = { 
                entity.dormant->width,
                entity.dormant->height,
            };
            width_height_pixels = v2_smul(
                    width_height_pixels,
                    meters_to_pixels * 0.5);

            draw_rectangle_rgba(display_buffer,
                           v2_sub(entity_screen, width_height_pixels),
                           v2_add(entity_screen, width_height_pixels),
                           0.3, 0.3, 0.3, 1);
        }

        if (entity.dormant->type == EntityType_ladder) {
            v2 width_height_pixels = { 
                entity.dormant->width,
                entity.dormant->height,
            };
            width_height_pixels = v2_smul(
                    width_height_pixels,
                    meters_to_pixels * 0.5);
            f32 r = (f32)0x66 / 0xFF;
            f32 g = (f32)0x39 / 0xFF;
            f32 b = (f32)0x00 / 0xFF;


            draw_rectangle_rgba(display_buffer,
                           v2_sub(entity_screen, width_height_pixels),
                           v2_add(entity_screen, width_height_pixels),
                           r, g, b, 1);
        }

#if 0
        // NOTE(fede): debug draw entity collision box
        {
            v2 entity_width_height_pixels = { 
                entity.dormant->width,
                entity.dormant->height,
            };
            entity_width_height_pixels = v2_smul(
                    entity_width_height_pixels,
                    meters_to_pixels * 0.5);

            draw_rectangle_rgba(display_buffer,
                           v2_sub(entity_screen, entity_width_height_pixels),
                           v2_add(entity_screen, entity_width_height_pixels),
                           1, 0, 0, 0.5);
        }
#endif

#if 0
        // NOTE(fede): debug draw entity tile
        {
            // NOTE(fede): Screen position
            f32 min_x, min_y, max_x, max_y;
            v2 min, max;
            {
                i32 rel_row = entity.dormant->p.abs_tile_y - game_state->camera_pos.abs_tile_y;
                i32 rel_col = entity.dormant->p.abs_tile_x - game_state->camera_pos.abs_tile_x;

                v2 center = screen_center;

                // TODO(fede): port to v2
                center.x += rel_col * tile_side_in_pixels;
                center.y -= rel_row * tile_side_in_pixels;

                v2 v2_tile_side_in_pixels = v2_smul((v2){
                    tile_side_in_pixels,
                    tile_side_in_pixels,
                }, 1);
                v2_tile_side_in_pixels = v2_smul(v2_tile_side_in_pixels, 0.5);
                min = v2_sub(center, v2_tile_side_in_pixels);
                max = v2_add(center, v2_tile_side_in_pixels);
            }

            draw_rectangle_rgba(display_buffer,
                           min, max,
                           1, 0, 0, 0.0);
        }
#endif

#endif
    }
}

extern GAME_FILL_SOUND_BUFFER(game_fill_sound_buffer) {
    assert(sizeof(GameState) <= memory->permanent_storage_size);
    GameState *game_state = (GameState *)memory->permanent_storage;
    fill_audio_buffer(sound_buffer, 256);
}
