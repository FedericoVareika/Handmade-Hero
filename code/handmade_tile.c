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
    return value == 1 || 
           value == 3 ||
           value == 4;

}

// NOTE(fede): Coordinates wrap around, if you walk off of one end, 
//          you walk into the other end. (Toroidal topology :: Taurus) 
internal void recanonicalize_coord(
        Tilemap *tilemap,
        f32 *tile_rel,
        u32 *tile) {
    f32 offset = round_f32_to_int(*tile_rel / tilemap->tile_side_in_meters); 

    *tile += truncate_f32_to_int(offset);
    *tile_rel -= offset * tilemap->tile_side_in_meters; 

    assert(*tile_rel * 2 >= -tilemap->tile_side_in_meters);
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
    assert(*tile_rel * 2 <= tilemap->tile_side_in_meters); 
}

internal TilemapPosition recanonicalize_position(
        Tilemap *tilemap,
        TilemapPosition can_pos) {
    TilemapPosition result = can_pos;

    recanonicalize_coord(tilemap, &result.tile_rel_x, &result.abs_tile_x);
    recanonicalize_coord(tilemap, &result.tile_rel_y, &result.abs_tile_y);

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

internal bool is_tilemap_point_empty(Tilemap *tilemap, TilemapPosition tilemap_pos) {
    TileChunkPosition chunk_pos = get_chunk_position_for(
            tilemap,
            tilemap_pos.abs_tile_x,
            tilemap_pos.abs_tile_y,
            tilemap_pos.abs_tile_z);

    // TODO(fede): this crashes when trying to wrap around (i think?). 
    //      when i went left with speed, i guess that the 
    //      new_tile_left.abs_tile_x = 0xffffffff
    //      after recanonicalize_position. 
    //      This turns into an invalid chunk coord later, i dont know if this 
    //      should be handled in recanonicalization, or in get_tilechunk.
    TileChunk *tilechunk = get_tilechunk(tilemap, chunk_pos);
    // assert(tilechunk);
    if (!tilechunk || !tilechunk->tiles)
        return false;

    return is_tilechunk_point_empty(tilemap, tilechunk, chunk_pos.rel_tile_x,
                                  chunk_pos.rel_tile_y);
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
