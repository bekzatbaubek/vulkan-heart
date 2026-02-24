#ifdef VKH_DEBUG
#define assert(expr) \
    if (!(expr)) { \
        __builtin_trap();\
    }
#else
#define assert(expr)
#endif

#include "vkh_game.h"

#include "vkh_memory.cpp"
#include "vkh_renderer_abstraction.cpp"

void game_update_and_render(GameMemory *game_memory, GameInput *input) {
    assert(sizeof(GameState) <= game_memory->permanent_store_size);
    GameState *game_state = (GameState *)(game_memory->permanent_store);

    MemoryArena transient_arena;
    transient_arena.base = (uint8_t*) game_memory->transient_store;
    transient_arena.size = game_memory->transient_store_size;
    transient_arena.used = 0;

    if (!game_state->is_initialised) {
        game_state->is_initialised = true;
        game_memory->permanent_store_used += sizeof(GameState);
        game_state->number_of_rectangles = 0;
    }

    u32 push_buffer_size = 1024 * 1024 * 256;
    game_state->frame_push_buffer.arena.base = arena_push(&transient_arena, push_buffer_size);
    game_state->frame_push_buffer.arena.size = push_buffer_size;
    game_state->frame_push_buffer.arena.used = 0;

    if (input->digital_inputs[D_LEFT].is_down) {
        input->digital_inputs[D_LEFT].was_down = true;

    } else {
        if (input->digital_inputs[D_LEFT].was_down) {
            input->digital_inputs[D_LEFT].was_down = false;
        }
    }

    if (input->digital_inputs[D_RIGHT].is_down) {
        input->digital_inputs[D_RIGHT].was_down = true;

    } else {
        if (input->digital_inputs[D_RIGHT].was_down) {
            input->digital_inputs[D_RIGHT].was_down = false;
        }
    }

    {
        float width = 50.0f;
        float height = 50.0f;
        // float x = input->mouse_x * input->window_pixel_density - width / 2.0f;
        // float y = input->mouse_y * input->window_pixel_density - height / 2.0f;
        float x = 200.0f;
        float y = 200.0f;
        float r = 0.0f;
        float g = 1.0f;
        float b = 0.0f;
        float a = 1.0f;

        DrawRectangle(&game_state->frame_push_buffer, x, y, width, height, r, g, b);
    }


}
