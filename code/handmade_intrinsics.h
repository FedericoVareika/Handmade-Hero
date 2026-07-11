#ifndef HANDMADE_INTRINSICS_H
#define HANDMADE_INTRINSICS_H

#include <math.h>

internal inline int round_f32_to_int(f32 a) {
    return roundf(a);
    // return (int)(a + 0.5); 
}

internal inline int floor_f32_to_int(f32 a) {
    return floorf(a);
    // return a < 0 ? (int)(a)-1 : (int)(a); 
}

internal inline int truncate_f32_to_int(f32 a) {
    return (int)(a);
}

internal inline f32 sin_f32(f32 angle) {
    return sinf(angle);
} 

internal inline f32 cos_f32(f32 angle) {
    return cosf(angle);
} 

internal inline f32 atan2_f32(f32 y, f32 x) {
    return atan2f(y, x);
} 

internal inline f32 abs_f32(f32 a) {
    return fabs(a);
} 

internal inline f32 sqrt_f32(f32 a) {
    return sqrtf(a);
}

// internal inline f32 inv_sqrt_f32(f32 a) {
//     return i
// }

typedef struct {
    u32 index;
    bool found;
} FindBitResult; 

internal inline FindBitResult find_least_significant_set_bit(u32 mask) {
    FindBitResult result = {};

#if defined(COMPILER_GCC)
    if (mask != 0) {
        result.index = __builtin_ctzll(mask);
        result.found = true;
    }
#elif defined(COMPILER_CLANG)
    // TODO(fede): do not know the clang intrinsic for this yet.
    while (result.index < 32) {
        if ((mask & (0x1 << result.index)) != 0) {
            result.found = true;
            return result;
        }

        result.index++;
    }
#else
    while (result.index < 32) {
        if ((mask & (0x1 << result.index)) != 0) {
            result.found = true;
            return result;
        }

        result.index++;
    }
#endif 

    return result;
}

#endif //HANDMADE_INTRINSICS_H
       
