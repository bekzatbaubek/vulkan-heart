#include <cassert>
#include <cstdint>

enum PushBufferEntryType {
    TRIANGLE,
    QUAD,

    PUSH_BUFFER_ENTRY_TYPE_MAX,
};

struct PushBufferEntry {
    PushBufferEntryType type;
    union {
        struct {
            float x1, y1;         // Top-left corner
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
    uint64_t size;
    uint64_t capacity;  // number of entries?
    PushBufferEntry* entries;
};

inline void DrawRectangle(PushBuffer* pb, float x, float y, float width,
                          float height) {
    assert(pb->size + sizeof(PushBufferEntry) <= pb->capacity);

    PushBufferEntry* entry = pb->entries + pb->size + sizeof(PushBufferEntry);
    pb->size += sizeof(PushBufferEntry);

    entry->type = QUAD;
    entry->data.quad.x1 = x;
    entry->data.quad.y1 = y;
    entry->data.quad.width = width;
    entry->data.quad.height = height;

    return;
}
