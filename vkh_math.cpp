#include "vkh_math.h"

#include <math.h>

vec3 normalize(const vec3 &v) {
    float length = sqrt(v.x * v.x + v.y * v.y + v.z * v.z);
    return {v.x / length, v.y / length, v.z / length};
}

vec3 cross(const vec3 &a, const vec3 &b) {
    return {a.y * b.z - a.z * b.y, a.z * b.x - a.x * b.z,
            a.x * b.y - a.y * b.x};
}

float dot(const vec3 &a, const vec3 &b) {
    return a.x * b.x + a.y * b.y + a.z * b.z;
}

vec3 operator*(const vec3 &v, float scalar) {
    return {v.x * scalar, v.y * scalar, v.z * scalar};
}

vec3 operator*(float scalar, const vec3 &v) {
    return {v.x * scalar, v.y * scalar, v.z * scalar};
}

vec3 operator+(const vec3 &a, const vec3 &b) {
    return {a.x + b.x, a.y + b.y, a.z + b.z};
}

vec3 operator-(const vec3 &a, const vec3 &b) {
    return {a.x - b.x, a.y - b.y, a.z - b.z};
};

mat4 identity() {
    mat4 result = {{{0.0f, 0.0f, 0.0f, 0.0f},
                    {0.0f, 0.0f, 0.0f, 0.0f},
                    {0.0f, 0.0f, 0.0f, 0.0f},
                    {0.0f, 0.0f, 0.0f, 0.0f}}};
    for (int i = 0; i < 4; ++i) {
        result.data[i][i] = 1.0f;
    }
    return result;
}

mat4 translate(float x, float y, float z) {
    mat4 result = identity();
    result.data[3][0] = x;
    result.data[3][1] = y;
    result.data[3][2] = z;
    return result;
}

mat4 scale(float x, float y, float z) {
    mat4 result = identity();
    result.data[0][0] = x;
    result.data[1][1] = y;
    result.data[2][2] = z;
    return result;
}

mat4 rotate(float angle, const vec3 &axis) {
    mat4 result = identity();
    float c = cos(angle);
    float s = sin(angle);
    float t = 1.0f - c;

    result.data[0][0] = t * axis.x * axis.x + c;
    result.data[0][1] = t * axis.x * axis.y - s * axis.z;
    result.data[0][2] = t * axis.x * axis.z + s * axis.y;

    result.data[1][0] = t * axis.x * axis.y + s * axis.z;
    result.data[1][1] = t * axis.y * axis.y + c;
    result.data[1][2] = t * axis.y * axis.z - s * axis.x;

    result.data[2][0] = t * axis.x * axis.z - s * axis.y;
    result.data[2][1] = t * axis.y * axis.z + s * axis.x;
    result.data[2][2] = t * axis.z * axis.z + c;

    return result;
}

mat4 multiply(const mat4 &a, const mat4 &b) {
    mat4 result = {};
    for (int i = 0; i < 4; ++i) {
        for (int j = 0; j < 4; ++j) {
            result.data[i][j] =
                a.data[i][0] * b.data[0][j] + a.data[i][1] * b.data[1][j] +
                a.data[i][2] * b.data[2][j] + a.data[i][3] * b.data[3][j];
        }
    }
    return result;
}

mat4 lookAt(const vec3 &eye, const vec3 &center, const vec3 &up) {
    vec3 f = normalize(center - eye);
    vec3 s = normalize(cross(f, up));
    vec3 u = cross(s, f);

    mat4 result = identity();
    result.data[0][0] = s.x;
    result.data[0][1] = u.x;
    result.data[0][2] = -f.x;
    result.data[1][0] = s.y;
    result.data[1][1] = u.y;
    result.data[1][2] = -f.y;
    result.data[2][0] = s.z;
    result.data[2][1] = u.z;
    result.data[2][2] = -f.z;
    result.data[3][0] = -dot(s, eye);
    result.data[3][1] = -dot(u, eye);
    result.data[3][2] = dot(f, eye);
    return result;
}

mat4 perspective(float fov, float aspect, float znear, float zfar) {
    mat4 result = identity();

    float tanHalfFov = tan(fov / 2.0f);

    result.data[0][0] = 1.0f / (aspect * tanHalfFov);
    result.data[1][1] =
        -1.0f / tanHalfFov;  // Note the negative sign for Y-flip
    result.data[2][2] = zfar / (znear - zfar);
    result.data[2][3] = -1.0f;
    result.data[3][2] = -(zfar * znear) / (zfar - znear);
    result.data[3][3] = 0.0f;

    return result;
}

mat4 createOrthographicProjection(float left, float right, float bottom,
                                  float top, float znear, float zfar) {
    mat4 result = {};

    // Set diagonal elements
    result.data[0][0] = 2.0f / (right - left);
    result.data[1][1] = 2.0f / (top - bottom);
    result.data[2][2] = -2.0f / (zfar - znear);
    result.data[3][3] = 1.0f;

    // Set translation data
    result.data[3][0] = -(right + left) / (right - left);
    result.data[3][1] = -(top + bottom) / (top - bottom);
    result.data[3][2] = -(zfar + znear) / (zfar - znear);

    return result;
}
