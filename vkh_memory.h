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

void arena_init(MemoryArena* arena, size_t size);
uint8_t* arena_push(MemoryArena* arena, size_t size);
temp_arena begin_temp_arena(MemoryArena* arena);
void end_temp_arena(temp_arena* temp);
