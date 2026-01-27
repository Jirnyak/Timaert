#pragma once

// Graphics types abstraction layer for Sokol migration
// This file provides compatibility types that bridge old SDL-style code to Sokol

#include <cstdint>

// Color type compatible with old Color usage
// Aligned to 4 bytes for optimal performance
struct alignas(4) Color {
    std::uint8_t r = 255;
    std::uint8_t g = 255;
    std::uint8_t b = 255;
    std::uint8_t a = 255;

    constexpr Color() = default;
    constexpr Color(std::uint8_t r_, std::uint8_t g_, std::uint8_t b_, std::uint8_t a_ = 255)
        : r(r_), g(g_), b(b_), a(a_) {}

    [[nodiscard]] constexpr float rf() const noexcept {
        return static_cast<float>(r) / 255.0f;
    }
    [[nodiscard]] constexpr float gf() const noexcept {
        return static_cast<float>(g) / 255.0f;
    }
    [[nodiscard]] constexpr float bf() const noexcept {
        return static_cast<float>(b) / 255.0f;
    }
    [[nodiscard]] constexpr float af() const noexcept {
        return static_cast<float>(a) / 255.0f;
    }
};

// Rect type compatible with old Rect usage
// Aligned to 16 bytes for optimal SIMD performance
struct alignas(16) Rect {
    int x = 0;
    int y = 0;
    int w = 0;
    int h = 0;

    constexpr Rect() = default;
    constexpr Rect(int x_, int y_, int w_, int h_) : x(x_), y(y_), w(w_), h(h_) {}

    [[nodiscard]] constexpr bool contains(int px, int py) const noexcept {
        return px >= x && px < x + w && py >= y && py < y + h;
    }
};

// Point type
// Aligned to 8 bytes for optimal performance
struct alignas(8) Point {
    int x = 0;
    int y = 0;

    constexpr Point() = default;
    constexpr Point(int x_, int y_) : x(x_), y(y_) {}
};

// FRect for floating-point rects
// Aligned to 16 bytes for optimal SIMD performance
struct alignas(16) FRect {
    float x = 0.0f;
    float y = 0.0f;
    float w = 0.0f;
    float h = 0.0f;

    constexpr FRect() = default;
    constexpr FRect(float x_, float y_, float w_, float h_) : x(x_), y(y_), w(w_), h(h_) {}
};


// Mouse button definitions
// Using uint8_t for optimal size (max value is 2)
enum class MouseButton : std::uint8_t {
    Left = 0,
    Right = 1,
    Middle = 2,
};

// Blend mode definitions
// Using uint8_t for optimal size (max value is 3)
enum class BlendMode : std::uint8_t {
    None,
    Blend,
    Add,
    Mod,
};
