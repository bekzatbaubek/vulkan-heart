#include <cstdio>

#include "vkh.hpp"

void gameUpdateAndRender(GameState* state){
    printf("Something else as well\n");
    printf("Hahah updated! From game code: %d\n", state->counter);
    state->counter++;
}
