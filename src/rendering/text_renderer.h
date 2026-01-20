#pragma once

#include <SDL.h>
#include <SDL_ttf.h>

#include <algorithm>
#include <cstdint>
#include <memory>
#include <string>
#include <unordered_map>
#include <utility>

class TextRenderer
{
private:
    struct TextureDeleter {
        void operator()(SDL_Texture* tex) const noexcept { if (tex) SDL_DestroyTexture(tex); }
    };

    struct FontDeleter {
        void operator()(TTF_Font* font) const noexcept { if (font) TTF_CloseFont(font); }
    };

    using TexturePtr = std::unique_ptr<SDL_Texture, TextureDeleter>;
    using FontPtr = std::unique_ptr<TTF_Font, FontDeleter>;

    struct TextKey {
        std::string text;
        int size = 0;
        std::uint32_t color = 0;

        bool operator==(const TextKey& other) const noexcept {
            return size == other.size && color == other.color && text == other.text;
        }
    };

    struct TextKeyHash {
        std::size_t operator()(const TextKey& key) const noexcept {
            std::size_t seed = std::hash<std::string>{}(key.text);
            seed ^= std::hash<int>{}(key.size) + 0x9e3779b9 + (seed << 6) + (seed >> 2);
            seed ^= std::hash<std::uint32_t>{}(key.color) + 0x9e3779b9 + (seed << 6) + (seed >> 2);
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
    int default_size_ = 20;

    std::unordered_map<int, FontPtr> fonts_;
    std::unordered_map<TextKey, CachedText, TextKeyHash> cache_;

    [[nodiscard]] static std::uint32_t pack_color(const SDL_Color& color) noexcept
    {
        return (static_cast<std::uint32_t>(color.r) << 24) |
               (static_cast<std::uint32_t>(color.g) << 16) |
               (static_cast<std::uint32_t>(color.b) << 8) |
               static_cast<std::uint32_t>(color.a);
    }

    [[nodiscard]] TTF_Font* get_font(int size)
    {
        if (size <= 0 || font_path_.empty()) return nullptr;

        auto it = fonts_.find(size);
        if (it != fonts_.end()) {
            return it->second.get();
        }

        FontPtr font{TTF_OpenFont(font_path_.c_str(), size)};
        if (!font) return nullptr;

        TTF_SetFontHinting(font.get(), TTF_HINTING_LIGHT);
        TTF_SetFontKerning(font.get(), 1);

        auto [pos, _] = fonts_.emplace(size, std::move(font));
        return pos->second.get();
    }

    [[nodiscard]] CachedText* get_cached_text(const std::string& text, int size, const SDL_Color& color)
    {
        TextKey key{ text, size, pack_color(color) };
        auto it = cache_.find(key);
        if (it != cache_.end()) {
            return &it->second;
        }

        if (!renderer_) return nullptr;

        TTF_Font* font = get_font(size);
        if (!font) return nullptr;

        SDL_Surface* surface = TTF_RenderUTF8_Blended(font, text.c_str(), color);
        if (!surface) return nullptr;

        const int surface_w = surface->w;
        const int surface_h = surface->h;

        TexturePtr texture{SDL_CreateTextureFromSurface(renderer_, surface)};
        SDL_FreeSurface(surface);

        if (!texture) return nullptr;
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

    void initialize(SDL_Renderer* renderer, std::string font_path, int default_size)
    {
        renderer_ = renderer;
        font_path_ = std::move(font_path);
        default_size_ = default_size > 0 ? default_size : 20;
        fonts_.clear();
        cache_.clear();
    }

    void set_renderer(SDL_Renderer* renderer)
    {
        if (renderer_ != renderer) {
            renderer_ = renderer;
            cache_.clear();
        }
    }

    void set_font_path(std::string path)
    {
        font_path_ = std::move(path);
        fonts_.clear();
        cache_.clear();
    }

    [[nodiscard]] bool preload(int font_size = 0)
    {
        const int size_hint = font_size > 0 ? font_size : default_size_;
        return get_font(size_hint) != nullptr;
    }

    void clear_cache() noexcept
    {
        cache_.clear();
    }

    [[nodiscard]] SDL_Point measure(const std::string& text, int font_size = 0)
    {
        SDL_Point size{0, 0};
        if (text.empty()) return size;

        const int size_hint = font_size > 0 ? font_size : default_size_;
        if (TTF_Font* font = get_font(size_hint)) {
            TTF_SizeUTF8(font, text.c_str(), &size.x, &size.y);
        }

        return size;
    }

    void draw(const std::string& text,
              int x,
              int y,
              int width,
              int height,
              const SDL_Color& color,
              int font_size = 0)
    {
        if (!renderer_ || text.empty()) return;

        const int size_hint = font_size > 0 ? font_size : (height > 0 ? height : default_size_);
        CachedText* cached = get_cached_text(text, size_hint, color);
        if (!cached || cached->width <= 0 || cached->height <= 0) return;

        float scale_x = width > 0 ? static_cast<float>(width) / static_cast<float>(cached->width) : 1.0f;
        float scale_y = height > 0 ? static_cast<float>(height) / static_cast<float>(cached->height) : 1.0f;
        float scale = std::min(scale_x, scale_y);
        if (scale <= 0.0f) return;

        SDL_Rect dest{
            x,
            y,
            static_cast<int>(static_cast<float>(cached->width) * scale),
            static_cast<int>(static_cast<float>(cached->height) * scale)
        };

        SDL_RenderCopy(renderer_, cached->texture.get(), nullptr, &dest);
    }
};
