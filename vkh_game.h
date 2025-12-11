#pragma once

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

    float mouse_x;
    float mouse_y;
};

struct GameMemory {
    uint64_t permanent_store_size;
    uint64_t permanent_store_used;
    void *permanent_store;

    uint64_t transient_store_size;
    uint64_t transient_store_used;
    void *transient_store;
};

struct GameCamera {};

struct GameState {
    bool is_initialised = false;

    PushBuffer frame_push_buffer;

    uint64_t number_of_rectangles = 0;
};

typedef void (*game_update_t)(GameMemory *state, GameInput *input);

#ifdef _WIN64
extern "C"
    __declspec(dllexport)
#else
extern "C"
#endif
    void
    game_update_and_render(GameMemory *state, GameInput *input);
