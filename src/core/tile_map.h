#pragma once

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <memory>

struct TilePosition
{
    std::uint16_t x = 0;
    std::uint16_t y = 0;

    [[nodiscard]] constexpr bool operator==(TilePosition other) const noexcept
    {
        return x == other.x && y == other.y;
    }

    [[nodiscard]] constexpr bool operator!=(TilePosition other) const noexcept
    {
        return !(*this == other);
    }
};

inline constexpr std::uint16_t INVALID_COORD = std::numeric_limits<std::uint16_t>::max();
inline constexpr TilePosition INVALID_POS{INVALID_COORD, INVALID_COORD};

[[nodiscard]] constexpr bool is_valid(TilePosition p) noexcept
{
    return p.x != INVALID_COORD && p.y != INVALID_COORD;
}

template <typename TileType, std::size_t Width, std::size_t Height>
class TileMap
{
public:
    using value_type = TileType;
    static constexpr std::size_t kWidth = Width;
    static constexpr std::size_t kHeight = Height;
    static constexpr std::size_t kSize = Width * Height;

    TileMap() : tiles_(std::make_unique<TileType[]>(kSize)) {}

    explicit TileMap(const TileType& fill_value) : TileMap()
    {
        fill(fill_value);
    }

    TileMap(const TileMap&) = delete;
    TileMap& operator=(const TileMap&) = delete;
    TileMap(TileMap&&) noexcept = default;
    TileMap& operator=(TileMap&&) noexcept = default;

    [[nodiscard]] static constexpr std::size_t width() noexcept { return kWidth; }
    [[nodiscard]] static constexpr std::size_t height() noexcept { return kHeight; }
    [[nodiscard]] static constexpr std::size_t size() noexcept { return kSize; }

    [[nodiscard]] TileType* data() noexcept { return tiles_.get(); }
    [[nodiscard]] const TileType* data() const noexcept { return tiles_.get(); }

    [[nodiscard]] TileType* begin() noexcept { return tiles_.get(); }
    [[nodiscard]] const TileType* begin() const noexcept { return tiles_.get(); }
    [[nodiscard]] TileType* end() noexcept { return tiles_.get() + kSize; }
    [[nodiscard]] const TileType* end() const noexcept { return tiles_.get() + kSize; }

    [[nodiscard]] TileType& at(TilePosition p) noexcept
    {
        return tiles_[static_cast<std::size_t>(p.y) * kWidth + static_cast<std::size_t>(p.x)];
    }
    [[nodiscard]] const TileType& at(TilePosition p) const noexcept
    {
        return tiles_[static_cast<std::size_t>(p.y) * kWidth + static_cast<std::size_t>(p.x)];
    }

    [[nodiscard]] TileType& operator[](TilePosition p) noexcept
    {
        return tiles_[static_cast<std::size_t>(p.y) * kWidth + static_cast<std::size_t>(p.x)];
    }
    [[nodiscard]] const TileType& operator[](TilePosition p) const noexcept
    {
        return tiles_[static_cast<std::size_t>(p.y) * kWidth + static_cast<std::size_t>(p.x)];
    }

    void fill(const TileType& value) noexcept
    {
        std::fill_n(tiles_.get(), kSize, value);
    }

    void assign(const TileType& value) noexcept
    {
        fill(value);
    }

private:
    std::unique_ptr<TileType[]> tiles_;
};
