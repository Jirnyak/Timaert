#pragma once

#include <array>
#include <cstdint>
#include <string>
#include <string_view>

#include "sokol_gfx.h"
#include "fontstash.h"
#include "sokol_fontstash.h"
#include "core/gfx_types.h"
#include "rendering/ra_icon.h"

enum class RaIcon : std::uint16_t;

class TextRenderer {
private:
    FONScontext* fs_ = nullptr;
    int font_normal_ = FONS_INVALID;
    int font_icon_ = FONS_INVALID;
    std::string font_path_;
    std::string icon_font_path_;
    int default_size_ = 20;
    int text_outline_px_ = 1;
    bool initialized_ = false;

    struct Utf8Glyph {
        std::array<char, 4> bytes{};
        std::uint8_t size = 0;
    };

    [[nodiscard]] static Utf8Glyph utf8_from_codepoint(std::uint32_t codepoint) noexcept {
        Utf8Glyph out{};
        if (codepoint <= 0x7F) {
            out.bytes[0] = static_cast<char>(codepoint);
            out.size = 1;
        } else if (codepoint <= 0x7FF) {
            out.bytes[0] = static_cast<char>(0xC0 | ((codepoint >> 6) & 0x1F));
            out.bytes[1] = static_cast<char>(0x80 | (codepoint & 0x3F));
            out.size = 2;
        } else if (codepoint <= 0xFFFF) {
            out.bytes[0] = static_cast<char>(0xE0 | ((codepoint >> 12) & 0x0F));
            out.bytes[1] = static_cast<char>(0x80 | ((codepoint >> 6) & 0x3F));
            out.bytes[2] = static_cast<char>(0x80 | (codepoint & 0x3F));
            out.size = 3;
        } else {
            out.bytes[0] = static_cast<char>(0xF0 | ((codepoint >> 18) & 0x07));
            out.bytes[1] = static_cast<char>(0x80 | ((codepoint >> 12) & 0x3F));
            out.bytes[2] = static_cast<char>(0x80 | ((codepoint >> 6) & 0x3F));
            out.bytes[3] = static_cast<char>(0x80 | (codepoint & 0x3F));
            out.size = 4;
        }
        return out;
    }

public:
    TextRenderer() = default;
    ~TextRenderer();

    TextRenderer(const TextRenderer&) = delete;
    TextRenderer& operator=(const TextRenderer&) = delete;
    TextRenderer(TextRenderer&&) = delete;
    TextRenderer& operator=(TextRenderer&&) = delete;

    struct IconStyle {
        Color color{255, 255, 255, 255};
        int size = 0;
        int font_size = 0;
    };

    void initialize(std::string font_path, int default_size, std::string icon_font_path = {});
    void shutdown();

    [[nodiscard]] bool preload(int font_size = 0);
    [[nodiscard]] bool preload_icon(int font_size = 0);

    void clear_cache() noexcept;

    [[nodiscard]] Point measure(std::string_view text, int font_size = 0);

    void draw(std::string_view text,
              float x,
              float y,
              float width,
              float height,
              const Color& color,
              int font_size = 0);

    void draw_icon(RaIcon icon, float x, float y, int size, const Color& color, int font_size = 0);

    void draw_icon(RaIcon icon, float x, float y, const IconStyle& style) {
        draw_icon(icon, x, y, style.size, style.color, style.font_size);
    }

    // Call at the start of each frame before any text drawing
    void begin_frame(int width, int height, float dpi_scale = 1.0f);

    // Call after all text drawing is complete
    void flush();

    [[nodiscard]] FONScontext* context() const noexcept {
        return fs_;
    }
    [[nodiscard]] bool is_initialized() const noexcept {
        return initialized_;
    }
};
