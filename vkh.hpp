#ifndef VKH_HPP
#define VKH_HPP

struct GameState {
    int counter;
};

typedef void (*game_update_t)(GameState*);

extern "C" void gameUpdateAndRender(GameState* state);

#endif // VKH_HPP
