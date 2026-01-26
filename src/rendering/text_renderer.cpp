#include "rendering/text_renderer.h"

#include <algorithm>
#include <cstring>
#include <fstream>
#include <print>
#include <vector>

// Fontstash error callback to handle atlas overflow
static void fontstash_error_callback(void* uptr, int error, int val) {
    (void)val;
    FONScontext* fs = static_cast<FONScontext*>(uptr);
    if (error == FONS_ATLAS_FULL) {
        // Atlas is full, expand it
        int width = 0, height = 0;
        fonsGetAtlasSize(fs, &width, &height);
        if (width < 4096 && height < 4096) {
            // Double the atlas size
            fonsExpandAtlas(fs, width * 2, height * 2);
            std::println(stderr, "[TextRenderer] Expanded font atlas to {}x{}", width * 2, height * 2);
        } else {
            // Atlas is already at max size, reset it
            fonsResetAtlas(fs, width, height);
            std::println(stderr, "[TextRenderer] Reset font atlas (was full at max size)");
        }
    }
}

TextRenderer::~TextRenderer() {
    shutdown();
}

void TextRenderer::initialize(std::string font_path, int default_size, std::string icon_font_path) {
    if (initialized_) {
        shutdown();
    }

    font_path_ = std::move(font_path);
    icon_font_path_ = std::move(icon_font_path);
    default_size_ = default_size > 0 ? default_size : 20;

    // Create fontstash context using sokol_fontstash with larger texture
    sfons_desc_t desc{};
    desc.width = 2048;
    desc.height = 2048;
    fs_ = sfons_create(&desc);

    if (!fs_) {
        std::println(stderr, "Failed to create fontstash context");
        return;
    }

    // Set error callback to handle atlas overflow
    fonsSetErrorCallback(fs_, fontstash_error_callback, fs_);

    initialized_ = true;
}

void TextRenderer::shutdown() {
    if (fs_) {
        sfons_destroy(fs_);
        fs_ = nullptr;
    }
    font_normal_ = FONS_INVALID;
    font_icon_ = FONS_INVALID;
    initialized_ = false;
}

bool TextRenderer::preload(int font_size) {
    if (!initialized_ || font_path_.empty())
        return false;

    if (font_normal_ == FONS_INVALID) {
        font_normal_ = fonsAddFont(fs_, "normal", font_path_.c_str());
        if (font_normal_ == FONS_INVALID) {
            std::println(stderr, "Failed to load font: {}", font_path_);
            return false;
        }
    }

    const int size_hint = font_size > 0 ? font_size : default_size_;
    fonsSetFont(fs_, font_normal_);
    fonsSetSize(fs_, static_cast<float>(size_hint));

    return true;
}

bool TextRenderer::preload_icon(int font_size) {
    if (!initialized_ || icon_font_path_.empty())
        return false;

    if (font_icon_ == FONS_INVALID) {
        font_icon_ = fonsAddFont(fs_, "icons", icon_font_path_.c_str());
        if (font_icon_ == FONS_INVALID) {
            std::println(stderr, "Failed to load icon font: {}", icon_font_path_);
            return false;
        }
    }

    const int size_hint = font_size > 0 ? font_size : default_size_;
    fonsSetFont(fs_, font_icon_);
    fonsSetSize(fs_, static_cast<float>(size_hint));

    return true;
}

void TextRenderer::clear_cache() noexcept {
    if (fs_) {
        fonsClearState(fs_);
    }
}

Point TextRenderer::measure(std::string_view text, int font_size) {
    Point size{0, 0};
    if (!initialized_ || text.empty() || font_normal_ == FONS_INVALID)
        return size;

    const int size_hint = font_size > 0 ? font_size : default_size_;
    fonsSetFont(fs_, font_normal_);
    fonsSetSize(fs_, static_cast<float>(size_hint));

    float bounds[4];
    std::string temp{text};
    float width = fonsTextBounds(fs_, 0, 0, temp.c_str(), nullptr, bounds);

    size.x = static_cast<int>(width);
    size.y = static_cast<int>(bounds[3] - bounds[1]);

    if (text_outline_px_ > 0) {
        size.x += text_outline_px_ * 2;
        size.y += text_outline_px_ * 2;
    }

    return size;
}

void TextRenderer::begin_frame(int width, int height, float dpi_scale) {
    if (!initialized_)
        return;
    (void)width;
    (void)height;
    (void)dpi_scale;
    // Don't flush here - let text accumulate during frame
    fonsClearState(fs_);
}

void TextRenderer::flush() {
    if (!initialized_)
        return;
    sfons_flush(fs_);
}

void TextRenderer::draw(std::string_view text,
                        float x,
                        float y,
                        float width,
                        float height,
                        const Color& color,
                        int font_size) {
    if (!initialized_ || text.empty() || font_normal_ == FONS_INVALID)
        return;

    const int size_hint = font_size > 0 ? font_size : (height > 0 ? static_cast<int>(height) : default_size_);

    fonsSetFont(fs_, font_normal_);
    fonsSetSize(fs_, static_cast<float>(size_hint));
    fonsSetColor(fs_, sfons_rgba(color.r, color.g, color.b, color.a));
    fonsSetAlign(fs_, FONS_ALIGN_LEFT | FONS_ALIGN_TOP);

    // Draw shadow/outline: draw multiple offset copies for outline effect
    if (text_outline_px_ > 0) {
        fonsSetColor(fs_, sfons_rgba(0, 0, 0, color.a));
        std::string temp{text};
        const float off = static_cast<float>(text_outline_px_);
        // Draw shadow at 8 directions for thick outline
        fonsDrawText(fs_, x - off, y, temp.c_str(), nullptr);
        fonsDrawText(fs_, x + off, y, temp.c_str(), nullptr);
        fonsDrawText(fs_, x, y - off, temp.c_str(), nullptr);
        fonsDrawText(fs_, x, y + off, temp.c_str(), nullptr);
        fonsSetColor(fs_, sfons_rgba(color.r, color.g, color.b, color.a));
    }

    std::string temp{text};
    fonsDrawText(fs_, x, y, temp.c_str(), nullptr);
}

void TextRenderer::draw_icon(RaIcon icon, float x, float y, int size, const Color& color, int font_size) {
    if (!initialized_ || font_icon_ == FONS_INVALID)
        return;

    const int size_hint = font_size > 0 ? font_size : (size > 0 ? size : default_size_);
    const std::uint32_t glyph = static_cast<std::uint32_t>(icon);

    Utf8Glyph utf8 = utf8_from_codepoint(glyph);
    std::array<char, 5> buf{};
    std::memcpy(buf.data(), utf8.bytes.data(), utf8.size);
    buf[utf8.size] = '\0';

    fonsSetFont(fs_, font_icon_);
    fonsSetSize(fs_, static_cast<float>(size_hint));
    fonsSetColor(fs_, sfons_rgba(color.r, color.g, color.b, color.a));
    fonsSetAlign(fs_, FONS_ALIGN_LEFT | FONS_ALIGN_TOP);

    // Draw outline: offset copies for thick outline
    if (text_outline_px_ > 0) {
        fonsSetColor(fs_, sfons_rgba(0, 0, 0, color.a));
        const float off = static_cast<float>(text_outline_px_);
        fonsDrawText(fs_, x - off, y, buf.data(), nullptr);
        fonsDrawText(fs_, x + off, y, buf.data(), nullptr);
        fonsDrawText(fs_, x, y - off, buf.data(), nullptr);
        fonsDrawText(fs_, x, y + off, buf.data(), nullptr);
        fonsSetColor(fs_, sfons_rgba(color.r, color.g, color.b, color.a));
    }

    fonsDrawText(fs_, x, y, buf.data(), nullptr);
}
