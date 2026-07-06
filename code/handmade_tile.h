#ifndef HANDMADE_TILE
#define HANDMADE_TILE

#define TILEMAP_WIDTH 256
#define TILEMAP_HEIGHT 256

#include "handmade.h"

typedef struct {
    u32 *tiles;
} TileChunk;

typedef struct {
    u32 chunk_shift;
    u32 chunk_mask;

    f32 tile_side_in_meters;

    TileChunk *tilechunks;
    u32 chunk_dim;

    u32 tilechunk_count_x;
    u32 tilechunk_count_y;
    u32 tilechunk_count_z;
} Tilemap;

typedef struct {
    u32 tilechunk_x;
    u32 tilechunk_y;
    u32 tilechunk_z;

    u32 rel_tile_x;
    u32 rel_tile_y;
} TileChunkPosition;

typedef struct {
    // NOTE(fede): Pack the tilemap_x+tile_x and tilemap_y+tile_y and pack them 
    //          into two int32s. 
    //          
    //          +-----------+--------+
    //          |  m_bits   | n bits |
    //          +-----------+--------+
    //          | tilemap_x | tile_x | 
    //          | tilemap_y | tile_y | 
    //          +-----------+--------+
    //
    
    u32 abs_tile_x;
    u32 abs_tile_y;
    u32 abs_tile_z;

    f32 tile_rel_x;
    f32 tile_rel_y;
} TilemapPosition;


#endif //HANDMADE_TILE


