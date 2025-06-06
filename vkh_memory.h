#pragma once

#include <cstddef>
#include <cstdint>

struct MemoryArena {
    uint8_t* base;
    size_t size;
    size_t used;
};

struct temp_arena {
    MemoryArena* parent;
    size_t prev_used;
};
