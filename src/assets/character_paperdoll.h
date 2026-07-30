#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

namespace sm::character {

inline constexpr int kTilesPerSheet = 160;
inline constexpr int kLogicalTileSize = 48;
inline constexpr int kMaxPaletteColors = 6;
inline constexpr int kMaxSpritesPerCategory = 64;

enum class Category : std::uint8_t {
    Body = 0,
    Head,
    Arms,
    BottomA,
    BottomB,
    TopA,
    TopB,
    HairA,
    HairB,
    HairC,
    HairD,
    AccessoryA,
    AccessoryB,
    AccessoryC,
    AccessoryD,
    JacketA,
    JacketB,
    Shoes,
    Socks,
    Gloves,
    Eyes,
    Eyebrows,
    Face,
    Ears,
    Horns,
    Mid,
    BackA,
    BackB,
    ShoulderA,
    ShoulderB,
    ChopA,
    ChopB,
    StrikeA,
    StrikeB,
    Reap,
    Water,
    Seed,
    Count,
};

enum class Direction : std::uint8_t {
    Front = 0,
    Back,
    Left,
    Right,
    Count,
};

enum class AnimationType : std::uint8_t {
    Idle = 0,
    Walk,
    Run,
    Pickup,
    Strike,
    Chop,
    Seed,
    Water,
    Reap,
    Count,
};

enum class AppearancePreset : std::uint8_t {
    None = 0,
    Backpack,
    ShoulderArmor,
    Horns,
    Count,
};

inline constexpr std::size_t kCategoryCount = std::size_t(Category::Count);

struct AtlasEntry {
    std::uint16_t u0 = 0;
    std::uint16_t v0 = 0;
    std::uint16_t w = 0;
    std::uint16_t h = 0;
    std::uint16_t ox = 0;
    std::uint16_t oy = 0;
};

struct TransparentStringHash {
    using is_transparent = void;

    std::size_t operator()(std::string_view value) const noexcept {
        return std::hash<std::string_view>{}(value);
    }
};

struct TransparentStringEqual {
    using is_transparent = void;

    bool operator()(std::string_view lhs,
                    std::string_view rhs) const noexcept {
        return lhs == rhs;
    }
};

struct AtlasData {
    std::uint16_t atlasWidth = 0;
    std::uint16_t atlasHeight = 0;
    std::uint16_t sheetCount = 0;
    std::uint32_t entryCount = 0;
    std::vector<AtlasEntry> entries;
    std::vector<std::string> sheetNames;
    std::unordered_map<std::string,
                       std::uint16_t,
                       TransparentStringHash,
                       TransparentStringEqual> nameToOrdinal;
    std::array<std::array<std::int16_t, kMaxSpritesPerCategory>,
               kCategoryCount> characterSheetOrdinals{};
    bool characterSheetOrdinalsReady = false;

    bool load_bin(const char* path);
    int sheet_ordinal(std::string_view relativePath) const;
    int sheet_ordinal(Category category, int spriteIndex) const;
    const AtlasEntry* entry(std::size_t index) const;
};

enum class PaletteSlot : std::uint8_t {
    AccessoryA = 0,
    AccessoryB,
    AccessoryC,
    AccessoryD,
    Bottom,
    Eye,
    Hair,
    Jacket,
    Shoe,
    Sock,
    Skintone,
    Top,
    Shoulder,
    Back,
    Eyebrows,
    Face,
    Horns,
    Mid,
    Gloves,
    Chop,
    Strike,
    Reap,
    Water,
    Seed,
    Count,
};

inline constexpr std::size_t kPaletteSlotCount = std::size_t(PaletteSlot::Count);

struct CharacterDescriptor {
    std::uint32_t seed = 1u;
    std::array<std::uint8_t, kCategoryCount> sprites{};
    std::uint64_t hiddenMask = 0u;
    std::array<std::uint8_t, kPaletteSlotCount> paletteRows{};
};

// Packed structure for GPU SSBO. Matches 18 uints (72 bytes).
// Must be tightly packed for std430/std140.
struct alignas(16) GpuCharacterDescriptor {
    std::array<std::uint8_t, 40> sprites{};
    std::uint32_t hiddenMask[2]{0, 0};
    std::array<std::uint8_t, 24> paletteRows{};
};


struct AnimationState {
    AnimationType animation = AnimationType::Idle;
    Direction direction = Direction::Front;
    std::uint8_t frame = 0;
    float frameTimerMs = 0.0f;
    bool playing = true;
};

struct PaletteConfig {
    std::array<std::uint32_t, kMaxPaletteColors> grayscale{};
    std::array<std::uint32_t, kMaxPaletteColors> colors{};
    std::uint8_t colorCount = 0;
};

struct RenderLayer {
    Category category = Category::Body;
    std::size_t entryIndex = 0;
    AtlasEntry entry{};
    PaletteConfig palette{};
};

const char* category_name(Category category);
const char* animation_name(AnimationType animation);
const char* direction_name(Direction direction);

int animation_frame_count(AnimationType animation);
int animation_start_index(AnimationType animation);
const std::array<std::uint16_t, 6>& animation_delays_ms(AnimationType animation);

AnimationState make_animation_state(AnimationType animation,
                                    Direction direction,
                                    float elapsedMs);
void update_animation(AnimationState& state, float deltaMs, bool loop = true);
void set_animation(AnimationState& state, AnimationType animation);
void set_direction(AnimationState& state, Direction direction);
bool is_animation_complete(const AnimationState& state);
void reset_animation(AnimationState& state);

int calculate_tile_index(AnimationType animation, Direction direction, std::uint8_t frame);
CharacterDescriptor make_default_character();
CharacterDescriptor generate_character(std::uint32_t seed);
CharacterDescriptor generate_character(std::uint32_t seed,
                                        AppearancePreset preset);
void apply_appearance_preset(CharacterDescriptor& descriptor,
                             AppearancePreset preset);
std::uint64_t descriptor_hash(const CharacterDescriptor& descriptor);

GpuCharacterDescriptor make_gpu_descriptor(const CharacterDescriptor& descriptor);

bool is_hidden(const CharacterDescriptor& descriptor, Category category);
void set_hidden(CharacterDescriptor& descriptor, Category category, bool hidden);

int sprite_count(Category category);
int clamp_sprite_index(Category category, int index);
bool build_sprite_relative_path(Category category,
                                int spriteIndex,
                                char* out,
                                std::size_t outSize);

std::size_t build_render_plan(const AtlasData& atlas,
                              const CharacterDescriptor& descriptor,
                              const AnimationState& animation,
                              RenderLayer* out,
                              std::size_t outCapacity);

std::size_t count_missing_required_assets(const AtlasData& atlas,
                                          const CharacterDescriptor& descriptor);

// Stable 64-bit identity of a single composited frame: descriptor appearance
// plus (animation, direction, frame). Two frames that composite to identical
// pixels share a key; anything visually different does not. Used as the cache
// key by both the GL texture cache and the Vulkan sprite pool.
std::uint64_t paperdoll_frame_key(const CharacterDescriptor& descriptor,
                                  const AnimationState& animation);

// Compose one 48x48 (kLogicalTileSize) RGBA8 frame: runs build_render_plan,
// then palette-swaps and alpha-blends each layer from the source atlas pixels
// into `outPixels` (must hold kLogicalTileSize*kLogicalTileSize*4 bytes).
// `atlasPixels` is the decoded atlas.png (atlasW*atlasH*4 RGBA8). Pure CPU, no
// GPU dependency — the single compositor shared by every paperdoll consumer.
// Returns false if the frame has no drawable layers (caller leaves it blank).
bool compose_paperdoll_rgba8(const AtlasData& atlas,
                             const std::uint8_t* atlasPixels,
                             int atlasW,
                             int atlasH,
                             const CharacterDescriptor& descriptor,
                             const AnimationState& animation,
                             std::uint8_t* outPixels);

} // namespace sm::character
