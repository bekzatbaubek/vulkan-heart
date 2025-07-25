#include <cassert>
#include <cstdint>

#include "vkh_memory.h"

enum PushBufferEntryType {
    NONE,
    TRIANGLE,
    QUAD,

    PUSH_BUFFER_ENTRY_TYPE_MAX,
};

struct PushBufferEntry {
    PushBufferEntryType type;
    union {
        struct {
            float x, y;           // Top-left corner
            float width, height;  // Bottom-right corner
        } quad;
        struct {
            float x1, y1;  // Vertex 1
            float x2, y2;  // Vertex 2
            float x3, y3;  // Vertex 3
        } triangle;
    } data;
};

struct PushBuffer {
    MemoryArena arena;
    uint32_t number_of_entries = 0;
};

inline void DrawRectangle(PushBuffer* pb, float x, float y, float width,
                          float height) {
    PushBufferEntry* pbe =
        (PushBufferEntry*)arena_push(&pb->arena, sizeof(PushBufferEntry));

    pbe->type = QUAD;
    pbe->data.quad.x = x;
    pbe->data.quad.y = y;
    pbe->data.quad.width = width;
    pbe->data.quad.height = height;

    pb->number_of_entries++;

    return;
}
