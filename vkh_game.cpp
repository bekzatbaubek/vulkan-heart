#ifdef VKH_DEBUG
#include <stdlib.h>
#include <stdio.h>
#define ASSERT(expr) \
    if (!(expr)) { \
        fprintf(stderr, "Assertion failed: %s, at %s:%d\n", #expr, __FILE__, __LINE__); \
        __builtin_trap();\
    }
#else
#define ASSERT(expr)
#endif

#include "vkh_game.h"

#include "vkh_math.cpp"
#include "vkh_memory.cpp"
#include "vkh_renderer_abstraction.cpp"

f32 random_float() {
    return ((f32)rand() / (f32)RAND_MAX) - 0.5f;
}

void SpawnParticles(GameState *game_state, vec2 mouse_position, f32 scaling_factor) {
    game_state->particles.num_of_particles = 10000;
    // TODO: Particle storage!
    game_state->particles.positions = (vec2 *)malloc(game_state->particles.num_of_particles * sizeof(vec2));
    game_state->particles.velocities = (vec2 *)malloc(game_state->particles.num_of_particles * sizeof(vec2));
    game_state->particles.colors = (vec3 *)malloc(game_state->particles.num_of_particles * sizeof(vec3));

    for (u32 i = 0; i < game_state->particles.num_of_particles; i++) {

        game_state->particles.positions[i].x = mouse_position.x * scaling_factor;
        game_state->particles.positions[i].y = mouse_position.y * scaling_factor;
        game_state->particles.velocities[i] = {random_float(), random_float()};
        game_state->particles.colors[i] = {random_float()+0.5f, random_float()+0.5f, random_float()+0.5f};
    }
}

void UpdateParticles(GameState *game_state, f32 delta_time) {
    for (u32 i = 0; i < game_state->particles.num_of_particles; i++) {
        game_state->particles.positions[i].x += game_state->particles.velocities[i].x * delta_time;
        game_state->particles.positions[i].y += game_state->particles.velocities[i].y * delta_time;
    }
}

void DrawParticles(GameState *game_state) {
    for (u32 i = 0; i < game_state->particles.num_of_particles; i++) {
        vec2 position = game_state->particles.positions[i];
        f32 width = 100.0f;
        f32 height = 100.0f;
        f32 x = position.x - width / 2.0f;
        f32 y = position.y - height / 2.0f;
        DrawRectangle(&game_state->frame_push_buffer, position.x, position.y, 10.0f, 10.0f, game_state->particles.colors[i].x, game_state->particles.colors[i].y, game_state->particles.colors[i].z);
    }
}

void game_update_and_render(GameMemory *game_memory, GameInput *input) {
    ASSERT(sizeof(GameState) <= game_memory->permanent_store_size);
    GameState *game_state = (GameState *)(game_memory->permanent_store);

    MemoryArena transient_arena;
    transient_arena.base = (uint8_t*) game_memory->transient_store;
    transient_arena.size = game_memory->transient_store_size;
    transient_arena.used = 0;

    if (!game_state->is_initialised) {
        game_state->is_initialised = true;
        game_memory->permanent_store_used += sizeof(GameState);
        game_state->number_of_rectangles = 0;
    }

    u32 push_buffer_size = 1024 * 1024 * 256;
    game_state->frame_push_buffer.arena.base = arena_push(&transient_arena, push_buffer_size);
    game_state->frame_push_buffer.arena.size = push_buffer_size;
    game_state->frame_push_buffer.arena.used = 0;
    game_state->frame_push_buffer.number_of_entries = 0;

    if (input->digital_inputs[D_LEFT].is_down) {
        if (game_state->number_of_rectangles > 0) {
            game_state->number_of_rectangles--;
        }
    }

    if (input->digital_inputs[D_RIGHT].is_down) {
        if (game_state->number_of_rectangles < 2000) {
            game_state->number_of_rectangles++;
        }
    }

#if 0
    if (input->digital_inputs[KEY_A].is_down) {
        SpawnParticles(game_state, {input->mouse_x, input->mouse_y}, input->window_pixel_density);
    }

    {
        float width = input->window_width * input->window_pixel_density;
        float height = input->window_height * input->window_pixel_density;
        float x = 0.0f;
        float y = 0.0f;
        float r = 0.1f;
        float g = 0.2f;
        float b = 0.5f;
        float a = 1.0f;

        DrawRectangle(&game_state->frame_push_buffer, x, y, width, height, r, g, b);
    }
#endif

    {
        u32 stride = input->window_width / 50;

        for (u32 i = 0; i < game_state->number_of_rectangles; i++){

            float width = 50.0f;
            float height = 50.0f;
            float x = (f32) ((i % stride) * width);
            float y = (f32) ((i / stride) * height);
            float r = 0.0f;
            float g = 1.0f * (i % 2);
            float b = 1.0f * (1.0f - (i % 2));
            float a = 1.0f;

            DrawRectangle(&game_state->frame_push_buffer, x, y, width, height, r, g, b);

        }

    }

    {
        float width = 20.0f;
        float height = 20.0f;
        float x = input->mouse_x * input->window_pixel_density - width / 2.0f;
        float y = input->mouse_y * input->window_pixel_density - height / 2.0f;
        float r = 1.0f;
        float g = 0.0f;
        float b = 0.0f;
        float a = 1.0f;

        DrawRectangle(&game_state->frame_push_buffer, x, y, width, height, r, g, b);
    }


    UpdateParticles(game_state, input->seconds_passed_since_last_frame);
    DrawParticles(game_state);
}
