#include "vkh_renderer_abstraction.h"

inline void DrawRectangle(PushBuffer* pb, float x, float y, float width,
                          float height, float r, float g, float b) {
    PushBufferEntry* pbe =
        (PushBufferEntry*)arena_push(&pb->arena, sizeof(PushBufferEntry));

    pbe->type = QUAD;
    pbe->data.quad.x = x;
    pbe->data.quad.y = y;
    pbe->data.quad.width = width;
    pbe->data.quad.height = height;

    pbe->color[0] = r;
    pbe->color[1] = g;
    pbe->color[2] = b;

    pb->number_of_entries++;

    return;
}
