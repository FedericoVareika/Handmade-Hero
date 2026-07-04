#include "handmade_tile.h"
#include "handmade_arena.c"

internal u32 get_tile_value_unchecked(
        Tilemap *tilemap,
        TileChunk *tilechunk,
        u32 abs_tile_x, u32 abs_tile_y) {

    assert(tilechunk);
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
    // TODO(fede): Implement
    return &tilemap->tilechunks[0];

    // // clang-format off
    // if (tilemap_x < 0 || tilemap_x >= world->tilemap_count_x || 
    //     tilemap_y < 0 || tilemap_y >= world->tilemap_count_y) 
    //     return 0;
    // // clang-format on
    //
    // return &world->tilemaps[tilemap_y * world->tilemap_count_x + tilemap_x];
}

internal bool is_tilechunk_point_empty(
        Tilemap *tilemap,
        TileChunk *tilechunk,
        u32 test_tile_x, u32 test_tile_y) {
    if (!tilechunk)
        return false;

    return !get_tile_value_unchecked(tilemap, tilechunk, test_tile_x, test_tile_y);
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

inline TileChunkPosition get_chunk_position_for(
        Tilemap *tilemap,
        u32 abs_tile_x,
        u32 abs_tile_y) {
    return (TileChunkPosition){
        .tile_chunk_x = abs_tile_x >> tilemap->chunk_shift,
        .tile_chunk_y = abs_tile_y >> tilemap->chunk_shift,
        .rel_tile_x = abs_tile_x & tilemap->chunk_mask,
        .rel_tile_y = abs_tile_y & tilemap->chunk_mask,
    };
}

internal bool is_tilemap_point_empty(Tilemap *tilemap, TilemapPosition tilemap_pos) {
    TileChunkPosition chunk_pos = get_chunk_position_for(
            tilemap,
            tilemap_pos.abs_tile_x,
            tilemap_pos.abs_tile_y);

    TileChunk *tilechunk = get_tilechunk(tilemap, chunk_pos);
    if (!tilechunk)
        return false;

    return is_tilechunk_point_empty(tilemap, tilechunk, chunk_pos.rel_tile_x,
                                  chunk_pos.rel_tile_y);
}

internal u32 get_tile_value(Tilemap *tilemap, u32 abs_tile_x, u32 abs_tile_y) {
    TileChunkPosition chunk_pos = get_chunk_position_for(tilemap, abs_tile_x, abs_tile_y);

    TileChunk *tilechunk = get_tilechunk(tilemap, chunk_pos);
    if (!tilechunk)
        return 0;

    return get_tile_value_unchecked(
            tilemap, tilechunk, chunk_pos.rel_tile_x, chunk_pos.rel_tile_y);
}

internal void set_tile_value(
        Arena *arena,
        Tilemap *tilemap,
        u32 abs_tile_x, u32 abs_tile_y,
        u32 val) {
    
    TileChunkPosition chunk_pos = get_chunk_position_for(tilemap, abs_tile_x, abs_tile_y);

    TileChunk *tilechunk = get_tilechunk(tilemap, chunk_pos);
    assert(tilechunk);
    /* TODO(fede): something like this.
    if (!tilechunk) {
        tilechunk = push_struct(arena, TileChunk);
        set_tilechunk(tilechunk, chunk_pos);
    }
    */

    return set_tile_value_unchecked(
            tilemap, tilechunk,
            chunk_pos.rel_tile_x, chunk_pos.rel_tile_y,
            val);
}

