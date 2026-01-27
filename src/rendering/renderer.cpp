#include "rendering/renderer.h"

#include "rendering/texture_manager.h"

namespace {

RenderState g_render_state;

void flush_texture_batch() {
    if (g_render_state.in_batch) {
        sgl_end();
        g_render_state.in_batch = false;
        g_render_state.current_image.id = 0;
    }
}

}  // namespace

RenderState& render_state() {
    return g_render_state;
}

void renderer_init() {
    if (g_render_state.initialized)
        return;

    // Initialize sokol_gl with large buffers for big windows
    sgl_desc_t sgl_desc{};
    sgl_desc.max_vertices = 1024 * 1024;  // 1M vertices for large tile maps
    sgl_desc.max_commands = 128 * 1024;   // 128K commands
    sgl_setup(&sgl_desc);

    // Create sampler with nearest-neighbor filtering for pixel art
    sg_sampler_desc smp_desc{};
    smp_desc.min_filter = SG_FILTER_NEAREST;
    smp_desc.mag_filter = SG_FILTER_NEAREST;
    smp_desc.mipmap_filter = SG_FILTER_NEAREST;
    smp_desc.wrap_u = SG_WRAP_CLAMP_TO_EDGE;
    smp_desc.wrap_v = SG_WRAP_CLAMP_TO_EDGE;
    g_render_state.nearest_sampler = sg_make_sampler(&smp_desc);

    // Create pipeline with proper alpha blending
    sg_pipeline_desc pip_desc{};
    pip_desc.colors[0].blend.enabled = true;
    pip_desc.colors[0].blend.src_factor_rgb = SG_BLENDFACTOR_SRC_ALPHA;
    pip_desc.colors[0].blend.dst_factor_rgb = SG_BLENDFACTOR_ONE_MINUS_SRC_ALPHA;
    pip_desc.colors[0].blend.src_factor_alpha = SG_BLENDFACTOR_ONE;
    pip_desc.colors[0].blend.dst_factor_alpha = SG_BLENDFACTOR_ONE_MINUS_SRC_ALPHA;
    g_render_state.alpha_blend_pipeline = sgl_make_pipeline(&pip_desc);

    g_render_state.initialized = true;
}

void renderer_shutdown() {
    if (!g_render_state.initialized)
        return;

    if (g_render_state.alpha_blend_pipeline.id != SG_INVALID_ID) {
        sgl_destroy_pipeline(g_render_state.alpha_blend_pipeline);
    }
    if (g_render_state.nearest_sampler.id != SG_INVALID_ID) {
        sg_destroy_sampler(g_render_state.nearest_sampler);
    }

    sgl_shutdown();
    g_render_state = {};
}

void renderer_begin_frame(int width, int height, float dpi_scale) {
    g_render_state.screen_width = width;
    g_render_state.screen_height = height;
    g_render_state.dpi_scale = dpi_scale;

    sgl_defaults();
    sgl_matrix_mode_projection();
    sgl_ortho(0.0f, static_cast<float>(width), static_cast<float>(height), 0.0f, -1.0f, 1.0f);
    sgl_matrix_mode_modelview();
    sgl_load_identity();
}

void renderer_end_frame() {
    // End any open texture batch
    if (g_render_state.in_batch) {
        sgl_end();
        g_render_state.in_batch = false;
        g_render_state.current_image.id = 0;
    }
    sgl_draw();
}

void render_clear(const Color& color) {
    // Note: Actual clearing is done via sg_begin_pass clear action
    // This is here for API compatibility; actual clear happens in main loop
    (void)color;
}

void render_fill_rect(const Rect& rect, const Color& color) {
    render_fill_rect(static_cast<float>(rect.x),
                     static_cast<float>(rect.y),
                     static_cast<float>(rect.w),
                     static_cast<float>(rect.h),
                     color);
}

void render_flush_batch() {
    flush_texture_batch();
}

void render_fill_rect(float x, float y, float w, float h, const Color& color) {
    flush_texture_batch();
    sgl_disable_texture();
    sgl_load_pipeline(g_render_state.alpha_blend_pipeline);

    sgl_begin_quads();
    sgl_c4b(color.r, color.g, color.b, color.a);
    sgl_v2f(x, y);
    sgl_v2f(x + w, y);
    sgl_v2f(x + w, y + h);
    sgl_v2f(x, y + h);
    sgl_end();
}

void render_draw_rect(const Rect& rect, const Color& color) {
    render_draw_rect(static_cast<float>(rect.x),
                     static_cast<float>(rect.y),
                     static_cast<float>(rect.w),
                     static_cast<float>(rect.h),
                     color);
}

void render_draw_rect(float x, float y, float w, float h, const Color& color) {
    flush_texture_batch();
    sgl_disable_texture();
    sgl_load_pipeline(g_render_state.alpha_blend_pipeline);

    sgl_begin_line_strip();
    sgl_c4b(color.r, color.g, color.b, color.a);
    sgl_v2f(x, y);
    sgl_v2f(x + w, y);
    sgl_v2f(x + w, y + h);
    sgl_v2f(x, y + h);
    sgl_v2f(x, y);
    sgl_end();
}

void render_draw_panel(const Rect& rect, const Color& fill, const Color& border) {
    render_fill_rect(rect, fill);
    render_draw_rect(rect, border);
}

void render_texture(const Texture& texture, const Rect& dest) {
    if (!texture.valid())
        return;

    render_texture(texture,
                   static_cast<float>(dest.x),
                   static_cast<float>(dest.y),
                   static_cast<float>(dest.w),
                   static_cast<float>(dest.h));
}

void render_texture(const Texture& texture, const Rect& src, const Rect& dest) {
    if (!texture.valid())
        return;

    const float tex_w = static_cast<float>(texture.width);
    const float tex_h = static_cast<float>(texture.height);

    const float u0 = static_cast<float>(src.x) / tex_w;
    const float v0 = static_cast<float>(src.y) / tex_h;
    const float u1 = static_cast<float>(src.x + src.w) / tex_w;
    const float v1 = static_cast<float>(src.y + src.h) / tex_h;

    const float x0 = static_cast<float>(dest.x);
    const float y0 = static_cast<float>(dest.y);
    const float x1 = x0 + static_cast<float>(dest.w);
    const float y1 = y0 + static_cast<float>(dest.h);

    sgl_enable_texture();
    sgl_texture(texture.view, texture.sampler);
    sgl_load_pipeline(g_render_state.alpha_blend_pipeline);

    sgl_begin_quads();
    sgl_c4b(255, 255, 255, 255);
    sgl_v2f_t2f(x0, y0, u0, v0);
    sgl_v2f_t2f(x1, y0, u1, v0);
    sgl_v2f_t2f(x1, y1, u1, v1);
    sgl_v2f_t2f(x0, y1, u0, v1);
    sgl_end();
}

void render_texture(const Texture& texture, float x, float y, float w, float h) {
    if (!texture.valid())
        return;

    // End any existing batch first
    flush_texture_batch();
    
    sgl_enable_texture();
    sgl_texture(texture.view, texture.sampler);
    sgl_load_pipeline(g_render_state.alpha_blend_pipeline);

    sgl_begin_quads();
    sgl_c4b(255, 255, 255, 255);
    sgl_v2f_t2f(x, y, 0.0f, 0.0f);
    sgl_v2f_t2f(x + w, y, 1.0f, 0.0f);
    sgl_v2f_t2f(x + w, y + h, 1.0f, 1.0f);
    sgl_v2f_t2f(x, y + h, 0.0f, 1.0f);
    sgl_end();
}

void render_draw_line(float x1, float y1, float x2, float y2, const Color& color) {
    flush_texture_batch();
    sgl_disable_texture();
    sgl_load_pipeline(g_render_state.alpha_blend_pipeline);

    sgl_begin_lines();
    sgl_c4b(color.r, color.g, color.b, color.a);
    sgl_v2f(x1, y1);
    sgl_v2f(x2, y2);
    sgl_end();
}
