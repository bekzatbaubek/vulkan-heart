#include "vkh_game.hpp"

#include <cassert>

void game_update_and_render(GameMemory *game_memory, GameInput *input) {
    // Implement game update and rendering logic here

    assert(sizeof(GameState) <= game_memory->permanent_store_size);
    GameState *game_state = (GameState *)(game_memory->permanent_store);

    if (!game_state->is_initialised) {
        game_state->is_initialised = true;
    }

    if (input->digital_inputs[D_LEFT].is_down) {
        // Example action when left key is pressed
        input->digital_inputs[D_LEFT].was_down = true;

        game_state->rectangle_count++;

    } else {
        if (input->digital_inputs[D_LEFT].was_down) {
            // Example action when left key is released
            input->digital_inputs[D_LEFT].was_down = false;
        }
    }

    if (input->digital_inputs[D_RIGHT].is_down) {
        // Example action when A key is pressed
        input->digital_inputs[D_RIGHT].was_down = true;

        if (game_state->rectangle_count > 0) {
            game_state->rectangle_count--;
        }

    } else {
        if (input->digital_inputs[D_RIGHT].was_down) {
            // Example action when A key is released
            input->digital_inputs[D_RIGHT].was_down = false;
        }
    }

    // std::cerr << "Rectangle count: " << game_state->rectangle_count << '\n';
}
