#pragma once

#include <SDL_pixels.h>
#include <SDL_rect.h>
#include <SDL_render.h>
#include <SDL_ttf.h>

#include <algorithm>
#include <array>
#include <cstdint>
#include <memory>
#include <string>
#include <string_view>
#include <unordered_map>
#include <utility>

#include "rendering/ra_icon.h"

class TextRenderer {
private:
    struct TextureDeleter {
        void operator()(SDL_Texture* tex) const noexcept {
            if (tex)
                SDL_DestroyTexture(tex);
        }
    };

    struct FontDeleter {
        void operator()(TTF_Font* font) const noexcept {
            if (font)
                TTF_CloseFont(font);
        }
    };

    using TexturePtr = std::unique_ptr<SDL_Texture, TextureDeleter>;
    using FontPtr = std::unique_ptr<TTF_Font, FontDeleter>;

    enum class FontKind : std::uint8_t { Text, Icon };

    struct TextKey {
        std::string text;
        int size = 0;
        std::uint32_t color = 0;
        FontKind kind = FontKind::Text;
        std::uint32_t glyph = 0;

        bool operator==(const TextKey& other) const noexcept {
            return size == other.size && color == other.color && kind == other.kind
                   && glyph == other.glyph && text == other.text;
        }
    };

    struct TextKeyHash {
        std::size_t operator()(const TextKey& key) const noexcept {
            std::size_t seed = std::hash<std::string>{}(key.text);
            seed ^= std::hash<int>{}(key.size) + 0x9e3779b9 + (seed << 6) + (seed >> 2);
            seed ^= std::hash<std::uint32_t>{}(key.color) + 0x9e3779b9 + (seed << 6) + (seed >> 2);
            seed ^= std::hash<int>{}(static_cast<int>(key.kind)) + 0x9e3779b9 + (seed << 6)
                    + (seed >> 2);
            seed ^= std::hash<std::uint32_t>{}(key.glyph) + 0x9e3779b9 + (seed << 6) + (seed >> 2);
            return seed;
        }
    };

    struct CachedText {
        TexturePtr texture{};
        int width = 0;
        int height = 0;
    };

    SDL_Renderer* renderer_ = nullptr;
    std::string font_path_;
    std::string icon_font_path_;
    int default_size_ = 20;
    int text_outline_px_ = 2;

    std::unordered_map<int, FontPtr> fonts_;
    std::unordered_map<int, FontPtr> icon_fonts_;
    std::unordered_map<TextKey, CachedText, TextKeyHash> cache_;

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

    [[nodiscard]] static std::uint32_t pack_color(const SDL_Color& color) noexcept {
        return (static_cast<std::uint32_t>(color.r) << 24)
               | (static_cast<std::uint32_t>(color.g) << 16)
               | (static_cast<std::uint32_t>(color.b) << 8) | static_cast<std::uint32_t>(color.a);
    }

    [[nodiscard]] TTF_Font* get_font(FontKind kind, int size) {
        if (size <= 0)
            return nullptr;

        const std::string& path = (kind == FontKind::Text) ? font_path_ : icon_font_path_;
        if (path.empty())
            return nullptr;

        auto& map = (kind == FontKind::Text) ? fonts_ : icon_fonts_;
        auto it = map.find(size);
        if (it != map.end()) {
            return it->second.get();
        }

        FontPtr font{TTF_OpenFont(path.c_str(), size)};
        if (!font)
            return nullptr;

        if (kind == FontKind::Text) {
            TTF_SetFontHinting(font.get(), TTF_HINTING_LIGHT);
            TTF_SetFontKerning(font.get(), 1);
        } else {
            TTF_SetFontHinting(font.get(), TTF_HINTING_NORMAL);
            TTF_SetFontKerning(font.get(), 0);
        }

        auto [pos, _] = map.emplace(size, std::move(font));
        return pos->second.get();
    }

    [[nodiscard]] CachedText*
    get_cached_text(FontKind kind, std::string_view text, int size, const SDL_Color& color) {
        TextKey key{std::string{text}, size, pack_color(color), kind, 0};
        auto it = cache_.find(key);
        if (it != cache_.end()) {
            return &it->second;
        }

        if (!renderer_)
            return nullptr;

        TTF_Font* font = get_font(kind, size);
        if (!font)
            return nullptr;

        SDL_Surface* surface = nullptr;
        if (text_outline_px_ > 0) {
            const SDL_Color outline_color{0, 0, 0, 255};
            TTF_SetFontOutline(font, text_outline_px_);
            SDL_Surface* outline = TTF_RenderUTF8_Blended(font, key.text.c_str(), outline_color);
            TTF_SetFontOutline(font, 0);
            if (!outline)
                return nullptr;

            SDL_Surface* fill = TTF_RenderUTF8_Blended(font, key.text.c_str(), color);
            if (!fill) {
                SDL_FreeSurface(outline);
                return nullptr;
            }

            surface = SDL_CreateRGBSurfaceWithFormat(0,
                                                     outline->w,
                                                     outline->h,
                                                     32,
                                                     outline->format->format);
            if (!surface) {
                SDL_FreeSurface(outline);
                SDL_FreeSurface(fill);
                return nullptr;
            }

            SDL_SetSurfaceBlendMode(outline, SDL_BLENDMODE_BLEND);
            SDL_SetSurfaceBlendMode(fill, SDL_BLENDMODE_BLEND);
            SDL_BlitSurface(outline, nullptr, surface, nullptr);
            SDL_Rect fill_rect{(outline->w - fill->w) / 2,
                               (outline->h - fill->h) / 2,
                               fill->w,
                               fill->h};
            SDL_BlitSurface(fill, nullptr, surface, &fill_rect);
            SDL_FreeSurface(outline);
            SDL_FreeSurface(fill);
        } else {
            surface = TTF_RenderUTF8_Blended(font, key.text.c_str(), color);
        }
        if (!surface)
            return nullptr;

        const int surface_w = surface->w;
        const int surface_h = surface->h;

        TexturePtr texture{SDL_CreateTextureFromSurface(renderer_, surface)};
        SDL_FreeSurface(surface);

        if (!texture)
            return nullptr;
        SDL_SetTextureBlendMode(texture.get(), SDL_BLENDMODE_BLEND);

        CachedText cached;
        cached.width = surface_w;
        cached.height = surface_h;
        cached.texture = std::move(texture);

        auto [pos, inserted] = cache_.emplace(std::move(key), std::move(cached));
        return inserted ? &pos->second : nullptr;
    }

    [[nodiscard]] CachedText*
    get_cached_glyph(FontKind kind, std::uint32_t glyph, int size, const SDL_Color& color) {
        TextKey key{std::string{}, size, pack_color(color), kind, glyph};
        auto it = cache_.find(key);
        if (it != cache_.end()) {
            return &it->second;
        }

        if (!renderer_)
            return nullptr;

        TTF_Font* font = get_font(kind, size);
        if (!font)
            return nullptr;

        SDL_Surface* surface = nullptr;
        if (text_outline_px_ > 0) {
            const SDL_Color outline_color{0, 0, 0, 255};
            TTF_SetFontOutline(font, text_outline_px_);
            SDL_Surface* outline = TTF_RenderGlyph32_Blended(font, glyph, outline_color);
            TTF_SetFontOutline(font, 0);
            if (!outline)
                return nullptr;

            SDL_Surface* fill = TTF_RenderGlyph32_Blended(font, glyph, color);
            if (!fill) {
                SDL_FreeSurface(outline);
                return nullptr;
            }

            surface = SDL_CreateRGBSurfaceWithFormat(0,
                                                     outline->w,
                                                     outline->h,
                                                     32,
                                                     outline->format->format);
            if (!surface) {
                SDL_FreeSurface(outline);
                SDL_FreeSurface(fill);
                return nullptr;
            }

            SDL_SetSurfaceBlendMode(outline, SDL_BLENDMODE_BLEND);
            SDL_SetSurfaceBlendMode(fill, SDL_BLENDMODE_BLEND);
            SDL_BlitSurface(outline, nullptr, surface, nullptr);
            SDL_Rect fill_rect{(outline->w - fill->w) / 2,
                               (outline->h - fill->h) / 2,
                               fill->w,
                               fill->h};
            SDL_BlitSurface(fill, nullptr, surface, &fill_rect);
            SDL_FreeSurface(outline);
            SDL_FreeSurface(fill);
        } else {
            surface = TTF_RenderGlyph32_Blended(font, glyph, color);
        }
        if ((!surface || surface->w <= 0 || surface->h <= 0) && font) {
            if (surface) {
                SDL_FreeSurface(surface);
                surface = nullptr;
            }
            const std::uint32_t glyph_index = TTF_GlyphIsProvided32(font, glyph);
            if (glyph_index > 0 && glyph_index <= 0xFFFF) {
                if (text_outline_px_ > 0) {
                    const SDL_Color outline_color{0, 0, 0, 255};
                    TTF_SetFontOutline(font, text_outline_px_);
                    SDL_Surface* outline =
                        TTF_RenderGlyph_Blended(font,
                                                static_cast<std::uint16_t>(glyph_index),
                                                outline_color);
                    TTF_SetFontOutline(font, 0);
                    if (outline) {
                        SDL_Surface* fill =
                            TTF_RenderGlyph_Blended(font,
                                                    static_cast<std::uint16_t>(glyph_index),
                                                    color);
                        if (fill) {
                            surface = SDL_CreateRGBSurfaceWithFormat(0,
                                                                     outline->w,
                                                                     outline->h,
                                                                     32,
                                                                     outline->format->format);
                            if (surface) {
                                SDL_SetSurfaceBlendMode(outline, SDL_BLENDMODE_BLEND);
                                SDL_SetSurfaceBlendMode(fill, SDL_BLENDMODE_BLEND);
                                SDL_BlitSurface(outline, nullptr, surface, nullptr);
                                SDL_Rect fill_rect{(outline->w - fill->w) / 2,
                                                   (outline->h - fill->h) / 2,
                                                   fill->w,
                                                   fill->h};
                                SDL_BlitSurface(fill, nullptr, surface, &fill_rect);
                            }
                            SDL_FreeSurface(fill);
                        }
                        SDL_FreeSurface(outline);
                    }
                } else {
                    surface = TTF_RenderGlyph_Blended(font,
                                                      static_cast<std::uint16_t>(glyph_index),
                                                      color);
                }
            }
            if ((!surface || surface->w <= 0 || surface->h <= 0) && glyph <= 0xFFFF) {
                if (surface) {
                    SDL_FreeSurface(surface);
                    surface = nullptr;
                }
                if (text_outline_px_ > 0) {
                    const SDL_Color outline_color{0, 0, 0, 255};
                    TTF_SetFontOutline(font, text_outline_px_);
                    SDL_Surface* outline =
                        TTF_RenderGlyph_Blended(font,
                                                static_cast<std::uint16_t>(glyph),
                                                outline_color);
                    TTF_SetFontOutline(font, 0);
                    if (outline) {
                        SDL_Surface* fill =
                            TTF_RenderGlyph_Blended(font, static_cast<std::uint16_t>(glyph), color);
                        if (fill) {
                            surface = SDL_CreateRGBSurfaceWithFormat(0,
                                                                     outline->w,
                                                                     outline->h,
                                                                     32,
                                                                     outline->format->format);
                            if (surface) {
                                SDL_SetSurfaceBlendMode(outline, SDL_BLENDMODE_BLEND);
                                SDL_SetSurfaceBlendMode(fill, SDL_BLENDMODE_BLEND);
                                SDL_BlitSurface(outline, nullptr, surface, nullptr);
                                SDL_Rect fill_rect{(outline->w - fill->w) / 2,
                                                   (outline->h - fill->h) / 2,
                                                   fill->w,
                                                   fill->h};
                                SDL_BlitSurface(fill, nullptr, surface, &fill_rect);
                            }
                            SDL_FreeSurface(fill);
                        }
                        SDL_FreeSurface(outline);
                    }
                } else {
                    surface =
                        TTF_RenderGlyph_Blended(font, static_cast<std::uint16_t>(glyph), color);
                }
            }
        }
        if (!surface)
            return nullptr;

        const int surface_w = surface->w;
        const int surface_h = surface->h;

        TexturePtr texture{SDL_CreateTextureFromSurface(renderer_, surface)};
        SDL_FreeSurface(surface);

        if (!texture)
            return nullptr;
        SDL_SetTextureBlendMode(texture.get(), SDL_BLENDMODE_BLEND);

        CachedText cached;
        cached.width = surface_w;
        cached.height = surface_h;
        cached.texture = std::move(texture);

        auto [pos, inserted] = cache_.emplace(std::move(key), std::move(cached));
        return inserted ? &pos->second : nullptr;
    }

public:
    TextRenderer() = default;

    struct IconStyle {
        SDL_Color color{255, 255, 255, 255};
        int size = 0;
        int font_size = 0;
    };

    void initialize(SDL_Renderer* renderer,
                    std::string font_path,
                    int default_size,
                    std::string icon_font_path = {}) {
        renderer_ = renderer;
        font_path_ = std::move(font_path);
        icon_font_path_ = std::move(icon_font_path);
        default_size_ = default_size > 0 ? default_size : 20;
        fonts_.clear();
        icon_fonts_.clear();
        cache_.clear();
    }

    void set_renderer(SDL_Renderer* renderer) {
        if (renderer_ != renderer) {
            renderer_ = renderer;
            cache_.clear();
        }
    }

    void set_font_path(std::string path) {
        font_path_ = std::move(path);
        fonts_.clear();
        cache_.clear();
    }

    void set_icon_font_path(std::string path) {
        icon_font_path_ = std::move(path);
        icon_fonts_.clear();
        cache_.clear();
    }

    [[nodiscard]] bool preload(int font_size = 0) {
        const int size_hint = font_size > 0 ? font_size : default_size_;
        return get_font(FontKind::Text, size_hint) != nullptr;
    }

    [[nodiscard]] bool preload_icon(int font_size = 0) {
        const int size_hint = font_size > 0 ? font_size : default_size_;
        return get_font(FontKind::Icon, size_hint) != nullptr;
    }

    void clear_cache() noexcept {
        cache_.clear();
    }

    [[nodiscard]] SDL_Point measure(std::string_view text, int font_size = 0) {
        SDL_Point size{0, 0};
        if (text.empty())
            return size;

        const int size_hint = font_size > 0 ? font_size : default_size_;
        if (TTF_Font* font = get_font(FontKind::Text, size_hint)) {
            if (text.size() <= 256) {
                std::array<char, 257> buffer{};
                std::copy(text.begin(), text.end(), buffer.begin());
                buffer[text.size()] = '\0';
                TTF_SizeUTF8(font, buffer.data(), &size.x, &size.y);
            } else {
                std::string temp{text};
                TTF_SizeUTF8(font, temp.c_str(), &size.x, &size.y);
            }
        }

        if (text_outline_px_ > 0) {
            size.x += text_outline_px_ * 2;
            size.y += text_outline_px_ * 2;
        }

        return size;
    }

    void draw(std::string_view text,
              int x,
              int y,
              int width,
              int height,
              const SDL_Color& color,
              int font_size = 0) {
        if (!renderer_ || text.empty())
            return;

        const int size_hint = font_size > 0 ? font_size : (height > 0 ? height : default_size_);
        CachedText* cached = get_cached_text(FontKind::Text, text, size_hint, color);
        if (!cached || cached->width <= 0 || cached->height <= 0)
            return;

        float scale_x =
            width > 0 ? static_cast<float>(width) / static_cast<float>(cached->width) : 1.0f;
        float scale_y =
            height > 0 ? static_cast<float>(height) / static_cast<float>(cached->height) : 1.0f;
        float scale = std::min(scale_x, scale_y);
        if (scale <= 0.0f)
            return;

        SDL_Rect dest{x,
                      y,
                      static_cast<int>(static_cast<float>(cached->width) * scale),
                      static_cast<int>(static_cast<float>(cached->height) * scale)};

        SDL_RenderCopy(renderer_, cached->texture.get(), nullptr, &dest);
    }

    void draw_icon(RaIcon icon, int x, int y, int size, const SDL_Color& color, int font_size = 0) {
        if (!renderer_)
            return;

        const int size_hint = font_size > 0 ? font_size : (size > 0 ? size : default_size_);
        const std::uint32_t glyph = static_cast<std::uint32_t>(icon);
        CachedText* cached = get_cached_glyph(FontKind::Icon, glyph, size_hint, color);
        if (!cached || cached->width <= 0 || cached->height <= 0)
            return;

        const int target = size > 0 ? size : std::max(cached->width, cached->height);
        if (target <= 0)
            return;

        const float scale = static_cast<float>(target)
                            / static_cast<float>(std::max(cached->width, cached->height));
        const int dest_w = static_cast<int>(static_cast<float>(cached->width) * scale);
        const int dest_h = static_cast<int>(static_cast<float>(cached->height) * scale);

        SDL_Rect dest{x + (target - dest_w) / 2, y + (target - dest_h) / 2, dest_w, dest_h};

        SDL_RenderCopy(renderer_, cached->texture.get(), nullptr, &dest);
    }

    void draw_icon(RaIcon icon, int x, int y, const IconStyle& style) {
        draw_icon(icon, x, y, style.size, style.color, style.font_size);
    }
};
