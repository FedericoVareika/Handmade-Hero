#ifndef HANDMADE_INTRINSICS 
#define HANDMADE_INTRINSICS 

#include <math.h>

inline int round_f32_to_int(f32 a) {
    return (int)(a + 0.5); 
}

inline int floor_f32_to_int(f32 a) {
    return floorf(a);
    // return a < 0 ? (int)(a)-1 : (int)(a); 
}

inline int truncate_f32_to_int(f32 a) {
    return (int)(a);
}

inline f32 sin_f32(f32 angle) {
    return sinf(angle);
} 

inline f32 cos_f32(f32 angle) {
    return cosf(angle);
} 

inline f32 atan2_f32(f32 y, f32 x) {
    return atan2f(y, x);
} 

#endif //HANDMADE_INTRINSICS
