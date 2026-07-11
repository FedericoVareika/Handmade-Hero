#ifndef HANDMADE_MATH_H
#define HANDMADE_MATH_H

typedef struct {
    union {
        struct {
            f32 x; 
            f32 y;
        };

        f32 e[2];
    };
} v2; 

internal inline v2 v2_add(v2 a, v2 b) {
    return (v2){
        a.x + b.x,
        a.y + b.y,
    };
}

internal inline v2 v2_sub(v2 a, v2 b) {
    return (v2){
        a.x - b.x,
        a.y - b.y,
    };
}

internal inline v2 v2_smul(v2 a, f32 m) {
    return (v2){
        a.x * m,
        a.y * m,
    };
}

internal inline v2 v2_vmul(v2 a, v2 b) {
    return (v2){
        a.x * b.x,
        a.y * b.y,
    };
}

internal inline v2 v2_neg(v2 a, f32 m) {
    return (v2){
        -a.x,
        -a.y,
    };
}

#include <math.h>   // NOTE(fede): for sqrt

internal inline f32 v2_dot(v2 a, v2 b) {
    return a.x * b.x + a.y * b.y;
}

internal inline f32 v2_length2(v2 v) {
    return v2_dot(v, v);
}

// TODO(fede): revisit
internal inline v2 v2_norm(v2 v) {
    f32 l = v2_length2(v);
    if (!l) 
        return v;

    return (v2){
        .x = v.x / l,
        .y = v.y / l,
    };
}

internal inline f32 square(f32 a) {
    return a * a;
}

#endif // HANDMADE_MATH_H
