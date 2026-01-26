#pragma once

// Graphics types abstraction layer for Sokol migration
// This file provides compatibility types that bridge old SDL-style code to Sokol

#include <cstdint>

// Color type compatible with old Color usage
struct Color {
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
struct Rect {
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
struct Point {
    int x = 0;
    int y = 0;

    constexpr Point() = default;
    constexpr Point(int x_, int y_) : x(x_), y(y_) {}
};

// FRect for floating-point rects
struct FRect {
    float x = 0.0f;
    float y = 0.0f;
    float w = 0.0f;
    float h = 0.0f;

    constexpr FRect() = default;
    constexpr FRect(float x_, float y_, float w_, float h_) : x(x_), y(y_), w(w_), h(h_) {}
};

// Keycode definitions (matching common SDL keycodes)
enum class KeyCode : int {
    Unknown = 0,
    Return = 13,
    Escape = 27,
    Backspace = 8,
    Tab = 9,
    Space = 32,
    
    Key0 = 48,
    Key1 = 49,
    Key2 = 50,
    Key3 = 51,
    Key4 = 52,
    Key5 = 53,
    Key6 = 54,
    Key7 = 55,
    Key8 = 56,
    Key9 = 57,
    
    A = 97,
    B = 98,
    C = 99,
    D = 100,
    E = 101,
    F = 102,
    G = 103,
    H = 104,
    I = 105,
    J = 106,
    K = 107,
    L = 108,
    M = 109,
    N = 110,
    O = 111,
    P = 112,
    Q = 113,
    R = 114,
    S = 115,
    T = 116,
    U = 117,
    V = 118,
    W = 119,
    X = 120,
    Y = 121,
    Z = 122,
    
    F1 = 282,
    F2 = 283,
    F3 = 284,
    F4 = 285,
    F5 = 286,
    F6 = 287,
    F7 = 288,
    F8 = 289,
    F9 = 290,
    F10 = 291,
    F11 = 292,
    F12 = 293,
    
    Up = 273,
    Down = 274,
    Right = 275,
    Left = 276,
    
    BackQuote = 96,
};

// Mouse button definitions
enum class MouseButton : int {
    Left = 0,
    Right = 1,
    Middle = 2,
};

// Blend mode definitions
enum class BlendMode {
    None,
    Blend,
    Add,
    Mod,
};
