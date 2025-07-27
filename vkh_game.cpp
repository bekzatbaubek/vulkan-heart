#include "vkh_game.h"

#include <cassert>

#include "vkh_memory.cpp"

void game_update_and_render(GameMemory *game_memory, GameInput *input) {
    assert(sizeof(GameState) <= game_memory->permanent_store_size);
    GameState *game_state = (GameState *)(game_memory->permanent_store);

    MemoryArena transient_arena;

    if (!game_state->is_initialised) {
        game_state->is_initialised = true;
        game_memory->permanent_store_used += sizeof(GameState);
        game_state->number_of_rectangles = 0;

        arena_init(&transient_arena, game_memory->transient_store_size);
        arena_init(&game_state->frame_push_buffer.arena,
                   1024 * 1024 * 256);  // 256 MB Per Frame arena

    } else {
        transient_arena.base = (uint8_t *)game_memory->transient_store;
        transient_arena.size = game_memory->transient_store_size;
        transient_arena.used = game_memory->transient_store_used;
        game_state->frame_push_buffer.arena.used = 0;
        game_state->frame_push_buffer.number_of_entries = 0;
    }

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

    for (int iY = 0; iY < 100; iY++) {
        for (int iX = 0; iX < 100; iX++) {
            float x = 0.0f + (float)iX * 12.0f;
            float y = 0.0f + (float)iY * 12.0f;
            float width = 12.0f;
            float height = 12.0f;

            float r = (float)iX / 100.0f;
            float g = (float)iY / 100.0f;
            float b = 0.5f;

            DrawRectangle(&game_state->frame_push_buffer, x, y, width, height, r, g, b);
        }
    }
}
