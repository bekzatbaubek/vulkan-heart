#pragma once

#include "vkh_memory.hpp"

#include <cassert>

void arena_init(MemoryArena* arena, size_t size) {
    arena->base = (uint8_t*)malloc(size);
    arena->size = size;
    arena->used = 0;
}

uint8_t* arena_push(MemoryArena* arena, size_t size) {
    assert(arena->used + size <= arena->size);
    uint8_t* result = arena->base + arena->used;
    arena->used += size;
    return result;
}

temp_arena begin_temp_arena(MemoryArena* arena) {
    temp_arena temp;
    temp.parent = arena;
    temp.prev_used = arena->used;
    return temp;
}

void end_temp_arena(temp_arena* temp) { temp->parent->used = temp->prev_used; }
