#ifndef VKH_GAME_HPP

#include <cstdint>

struct game_input {};

struct game_memory {
    uint64_t permanent_store_size;
    void *permanent_store;

    uint64_t transient_store_size;
    void *transient_store;
};

typedef void (*game_update_t)(game_memory *state);

extern "C" {
void game_update_and_render(game_memory *state);
}

#define VKH_GAME_HPP
#endif
