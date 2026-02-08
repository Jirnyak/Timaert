#pragma once

// Sokol-based rendering utilities
// Replaces SDL_Render* functions with sokol_gl equivalents

#include <cstdint>

#include "sokol_gfx.h"
#include "sokol_gl.h"
#include "core/gfx_types.h"
#include "rendering/texture_manager.h"

struct Texture;

// Batched quad for texture rendering
struct BatchedQuad {
    float x, y, w, h;
    float u0, v0, u1, v1;
};

// Global rendering state
struct RenderState {
    sg_sampler nearest_sampler = {0};
    sgl_pipeline alpha_blend_pipeline = {0};
    int screen_width = 0;
    int screen_height = 0;
    float dpi_scale = 1.0f;
    bool initialized = false;
    
    // Batching state - track current texture to avoid switches
    sg_image current_image = {0};
    sg_sampler current_sampler = {0};
    bool in_batch = false;
};

RenderState& render_state();

// Initialize rendering subsystem (call after sg_setup)
void renderer_init();

// Shutdown rendering subsystem (call before sg_shutdown)
void renderer_shutdown();

// Begin a new frame
void renderer_begin_frame(int width, int height, float dpi_scale = 1.0f);

// End frame and commit
void renderer_end_frame();

// Clear screen with color
void render_clear(const Color& color);

// Draw a filled rectangle
void render_fill_rect(const Rect& rect, const Color& color);
void render_fill_rect(float x, float y, float w, float h, const Color& color);

// Draw a rectangle outline
void render_draw_rect(const Rect& rect, const Color& color);
void render_draw_rect(float x, float y, float w, float h, const Color& color);

// Draw a panel (filled rect with border)
void render_draw_panel(const Rect& rect, const Color& fill, const Color& border);

// Draw a textured quad
void render_texture(const Texture& texture, const Rect& dest);
void render_texture(const Texture& texture, const Rect& src, const Rect& dest);
void render_texture(const Texture& texture, float x, float y, float w, float h);

// Draw a line
void render_draw_line(float x1, float y1, float x2, float y2, const Color& color);

// Flush any pending texture batch (call before switching to fontstash)
void render_flush_batch();

// Coordinate conversion helpers
[[nodiscard]] inline float to_ndc_x(float x, int screen_width) {
    return (x / static_cast<float>(screen_width)) * 2.0f - 1.0f;
}

[[nodiscard]] inline float to_ndc_y(float y, int screen_height) {
    return 1.0f - (y / static_cast<float>(screen_height)) * 2.0f;
}

// Color utilities
[[nodiscard]] constexpr std::uint8_t hex_digit(char c) noexcept {
    if (c >= '0' && c <= '9') {
        return c - '0';
    }
    if (c >= 'a' && c <= 'f') {
        return c - 'a' + 10;
    }
    if (c >= 'A' && c <= 'F') {
        return c - 'A' + 10;
    }
    return 0;
}

[[nodiscard]] constexpr std::uint8_t hex_byte(char hi, char lo) noexcept {
    return (hex_digit(hi) << 4) | hex_digit(lo);
}

[[nodiscard]] constexpr Color ui_color(const char* s) noexcept {
    const std::uint8_t r = hex_byte(s[1], s[2]);
    const std::uint8_t g = hex_byte(s[3], s[4]);
    const std::uint8_t b = hex_byte(s[5], s[6]);
    const std::uint8_t a = (s[7] && s[8]) ? hex_byte(s[7], s[8]) : 255;
    return Color{r, g, b, a};
}

[[nodiscard]] inline bool ui_point_in_rect(int x, int y, const Rect& rect) noexcept {
    return x >= rect.x && x < rect.x + rect.w && y >= rect.y && y < rect.y + rect.h;
}

[[nodiscard]] inline Rect ui_centered_rect(int window_w, int window_h, int w, int h) noexcept {
    return Rect{window_w / 2 - w / 2, window_h / 2 - h / 2, w, h};
}
