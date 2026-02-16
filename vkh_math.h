#pragma once

struct vec2 {
    float x, y;
};
struct vec3 {
    float x, y, z;
};

typedef vec3 pos3;
typedef vec3 color3;

struct mat4 {
    float data[4][4] = {{0}};
};
