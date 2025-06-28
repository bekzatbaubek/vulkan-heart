#include "vkh_game.h"

#include <cassert>

void game_update_and_render(GameMemory *game_memory, GameInput *input) {
    assert(sizeof(GameState) <= game_memory->permanent_store_size);
    GameState *game_state = (GameState *)(game_memory->permanent_store);

    if (!game_state->is_initialised) {
        game_state->is_initialised = true;
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
}
