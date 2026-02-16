#pragma once

#include <SDL3/SDL_stdinc.h>

typedef float f32;
typedef double f64;
typedef uint32_t u32;
typedef int32_t i32;
typedef uint64_t u64;
typedef int64_t i64;

#include "vkh_math.h"
#include "vkh_renderer_abstraction.h"

struct key_state {
    bool is_down;
    bool was_down;
    uint32_t num_of_presses;
};

enum key {
    KEY_A,
    KEY_B,
    KEY_X,
    KEY_Y,

    D_UP,
    D_DOWN,
    D_LEFT,
    D_RIGHT,

    LEFT_BUMPER,
    RIGHT_BUMPER,

    SELECT,
    START,

    LEFT_STICK_BUTTON,
    RIGHT_STICK_BUTTON,

    KEYS_SIZE,
};

struct GameInput {
    double seconds_passed_since_last_frame;
    key_state digital_inputs[KEYS_SIZE];

    f32 mouse_x;
    f32 mouse_y;

    f32 window_pixel_density;
    i32 window_width;
    i32 window_height;
};

struct GameMemory {
    u64 permanent_store_size;
    u64 permanent_store_used;
    void *permanent_store;

    u64 transient_store_size;
    u64 transient_store_used;
    void *transient_store;
};

struct GameCamera {};

struct GameState {
    bool is_initialised = false;

    PushBuffer frame_push_buffer;

    u64 number_of_rectangles = 0;
};

typedef void (*game_update_t)(GameMemory *state, GameInput *input);

#ifdef SDL_PLATFORM_WINDOWS
extern "C"
    __declspec(dllexport)
#else
extern "C"
#endif
    void
    game_update_and_render(GameMemory *state, GameInput *input);
