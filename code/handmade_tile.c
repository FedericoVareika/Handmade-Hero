#include "handmade_tile.h"
#include "handmade_arena.c"

internal u32 get_tile_value_unchecked(
        Tilemap *tilemap,
        TileChunk *tilechunk,
        u32 abs_tile_x, u32 abs_tile_y) {

    assert(tilechunk);
    assert(tilechunk->tiles);
    assert(abs_tile_x < tilemap->chunk_dim);
    assert(abs_tile_y < tilemap->chunk_dim);

    return tilechunk->tiles[abs_tile_y * tilemap->chunk_dim + abs_tile_x];
}

internal void set_tile_value_unchecked(
        Tilemap *tilemap,
        TileChunk *tilechunk,
        u32 abs_tile_x, u32 abs_tile_y, u32 val) {

    assert(tilechunk);
    assert(abs_tile_x < tilemap->chunk_dim);
    assert(abs_tile_y < tilemap->chunk_dim);

    tilechunk->tiles[abs_tile_y * tilemap->chunk_dim + abs_tile_x] = val;
}

internal TileChunk *get_tilechunk(Tilemap *tilemap, TileChunkPosition chunk_pos) {
    if (chunk_pos.tilechunk_x >= tilemap->tilechunk_count_x || 
        chunk_pos.tilechunk_y >= tilemap->tilechunk_count_y || 
        chunk_pos.tilechunk_z >= tilemap->tilechunk_count_z) 
        return 0;
    u32 idx = 
        (chunk_pos.tilechunk_z * 
        tilemap->tilechunk_count_y *
        tilemap->tilechunk_count_x) +
        (chunk_pos.tilechunk_y *
        tilemap->tilechunk_count_x) +
        (chunk_pos.tilechunk_x);

    return &tilemap->tilechunks[idx];
}

internal bool is_tilechunk_point_empty(
        Tilemap *tilemap,
        TileChunk *tilechunk,
        u32 test_tile_x, u32 test_tile_y) {
    assert(tilechunk);

    u32 value = get_tile_value_unchecked(tilemap, tilechunk, test_tile_x, test_tile_y);

}

// NOTE(fede): Coordinates wrap around, if you walk off of one end, 
//          you walk into the other end. (Toroidal topology :: Taurus) 
internal void recanonicalize_coord(
        Tilemap *tilemap,
        f32 *tile_rel,
        u32 *tile) {
    int offset = round_f32_to_int(*tile_rel / tilemap->tile_side_in_meters); 

    *tile += offset;
    *tile_rel -= offset * tilemap->tile_side_in_meters; 

    assert(*tile_rel >= -0.5 * tilemap->tile_side_in_meters);
    // TODO(fede): STUDY this crashed once 
    //      This is a floating point precision error. Values to recreate:
    //          tilemap->tile_side_in_meters = 1.39999998   // 1.4 input cast to f32
    //          *tile_rel = -2.0999999
    //          *tile = -7                                  // (i dont think this matters)
    //
    //      Therefore: 
    //          offset = -2 
    //          *tile_rel -= (-2) * 1.39999998              // -2.0999999 - (-2) * 1.39999998
    //          *tile_rel = 0.700000048
    //          0.700000048 * 2 = 1.4000001 
    //          1.4000001 <= 1.39999998         = false
    //
    assert(*tile_rel <= 0.5 * tilemap->tile_side_in_meters); 
}

internal TilemapPosition recanonicalize_position(
        Tilemap *tilemap,
        TilemapPosition can_pos) {
    TilemapPosition result = can_pos;

    recanonicalize_coord(tilemap, &result.offset.x, &result.abs_tile_x);
    recanonicalize_coord(tilemap, &result.offset.y, &result.abs_tile_y);

    return result;
}

internal inline TileChunkPosition get_chunk_position_for(
        Tilemap *tilemap,
        u32 abs_tile_x,
        u32 abs_tile_y,
        u32 abs_tile_z) {
    return (TileChunkPosition){
        .tilechunk_x = abs_tile_x >> tilemap->chunk_shift,
        .tilechunk_y = abs_tile_y >> tilemap->chunk_shift,
        .tilechunk_z = abs_tile_z,
        .rel_tile_x = abs_tile_x & tilemap->chunk_mask,
        .rel_tile_y = abs_tile_y & tilemap->chunk_mask,
    };
}

internal u32 get_tile_value(
        Tilemap *tilemap,
        u32 abs_tile_x,
        u32 abs_tile_y,
        u32 abs_tile_z) {
    TileChunkPosition chunk_pos = 
        get_chunk_position_for(tilemap, abs_tile_x, abs_tile_y, abs_tile_z);

    TileChunk *tilechunk = get_tilechunk(tilemap, chunk_pos);
    if (!tilechunk || !tilechunk->tiles)
        return 0;

    return get_tile_value_unchecked(
            tilemap, tilechunk, chunk_pos.rel_tile_x, chunk_pos.rel_tile_y);
}

internal u32 get_tile_value_at_pos(Tilemap *tilemap, TilemapPosition pos) {
    return get_tile_value(
            tilemap,
            pos.abs_tile_x,
            pos.abs_tile_y,
            pos.abs_tile_z);
}

internal bool is_tile_value_empty(u32 tile_value) {
    return tile_value == 1 || 
           tile_value == 3 ||
           tile_value == 4;
}

internal bool is_tilemap_point_empty(Tilemap *tilemap, TilemapPosition tilemap_pos) {
    u32 value = get_tile_value_at_pos(tilemap, tilemap_pos);
    return is_tile_value_empty(value);
}

internal void set_tile_value(
        Arena *arena,
        Tilemap *tilemap,
        u32 abs_tile_x, u32 abs_tile_y, u32 abs_tile_z,
        u32 val) {
    
    TileChunkPosition chunk_pos = 
        get_chunk_position_for(tilemap, abs_tile_x, abs_tile_y, abs_tile_z);

    TileChunk *tilechunk = get_tilechunk(tilemap, chunk_pos);
    assert(tilechunk);
    if (!tilechunk->tiles) {
        u32 tile_count = tilemap->chunk_dim * tilemap->chunk_dim;
        tilechunk->tiles = push_array(arena, u32, tile_count);
        
        for (u32 i = 0; i < tile_count; i++) {
            tilechunk->tiles[i] = 1;
        }
    }

    return set_tile_value_unchecked(
            tilemap, tilechunk,
            chunk_pos.rel_tile_x, chunk_pos.rel_tile_y,
            val);
}

internal bool are_on_same_tile(
        TilemapPosition pos_a,
        TilemapPosition pos_b) {
    return pos_a.abs_tile_x == pos_b.abs_tile_x &&
           pos_a.abs_tile_y == pos_b.abs_tile_y &&
           pos_a.abs_tile_z == pos_b.abs_tile_z;
} 

internal bool are_on_same_room(
        u32 room_width, u32 room_height,
        TilemapPosition pos_a,
        TilemapPosition pos_b) {
    return pos_a.abs_tile_x / room_width == pos_b.abs_tile_x / room_width &&
           pos_a.abs_tile_y / room_height == pos_b.abs_tile_y / room_height &&
           pos_a.abs_tile_z == pos_b.abs_tile_z;
} 

internal TilemapPosition get_room_center(
        u32 room_width, u32 room_height,
        TilemapPosition position_in_room) {
    u32 room_x = position_in_room.abs_tile_x / room_width;
    u32 room_y = position_in_room.abs_tile_y / room_height;
    return (TilemapPosition){
        .abs_tile_x = room_x * room_width + room_width / 2,
        .abs_tile_y = room_y * room_height + room_height / 2,
        .abs_tile_z = position_in_room.abs_tile_z,
    };
}

internal TilemapDifference subtract_tilemap_positions(
        f32 tile_side_in_meters, 
        TilemapPosition pos_a, 
        TilemapPosition pos_b) {

    v2 dxy = {
        (f32)pos_a.abs_tile_x - (f32)pos_b.abs_tile_x,
        (f32)pos_a.abs_tile_y - (f32)pos_b.abs_tile_y,
    };

    f32 dz = (f32)pos_a.abs_tile_z - (f32)pos_b.abs_tile_z;

    dxy = v2_smul(dxy, tile_side_in_meters);
    dxy = v2_add(dxy, v2_sub(pos_a.offset, pos_b.offset));

    return (TilemapDifference){
        .dxy = dxy,
        .dz = dz,
    };
}
