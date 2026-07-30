#include "assets/character_paperdoll.h"

#include "core/rng.h"

#include <algorithm>
#include <cstdio>
#include <cstring>

namespace sm::character {
namespace {

constexpr std::uint32_t rgb(std::uint32_t hex) { return hex & 0x00FFFFFFu; }

constexpr std::size_t ci(Category c) { return std::size_t(c); }
constexpr std::size_t pi(PaletteSlot p) { return std::size_t(p); }

constexpr std::array<const char*, kCategoryCount> kCategoryNames = {{
    "Body", "Head", "Arms", "BottomA", "BottomB", "TopA", "TopB",
    "HairA", "HairB", "HairC", "HairD", "AccessoryA", "AccessoryB",
    "AccessoryC", "AccessoryD", "JacketA", "JacketB", "Shoes", "Socks",
    "Gloves", "Eyes", "Eyebrows", "Face", "Ears", "Horns", "Mid",
    "BackA", "BackB", "ShoulderA", "ShoulderB", "ChopA", "ChopB",
    "StrikeA", "StrikeB", "Reap", "Water", "Seed",
}};

constexpr std::array<const char*, kCategoryCount> kFileStems = {{
    "body", "head", "arms", "bottoma", "bottomb", "topa", "topb",
    "haira", "hairb", "hairc", "haird", "accessorya", "accessoryb",
    "accessoryc", "accessoryd", "jacketa", "jacketb", "shoes", "socks",
    "gloves", "eyes", "eyebrows", "face", "ears", "horns", "mid",
    "backa", "backb", "shouldera", "shoulderb", "chopa", "chopb",
    "strikea", "strikeb", "reap", "water", "seed",
}};

constexpr std::array<int, kCategoryCount> kSpriteCounts = {{
    1, 1, 3, 7, 10, 11, 31,
    16, 21, 23, 21, 7, 11,
    10, 11, 10, 10, 4, 4,
    2, 14, 7, 4, 5, 4, 6,
    3, 3, 4, 4, 2, 2,
    4, 4, 2, 2, 2,
}};
static_assert(kMaxSpritesPerCategory >= 32,
              "paper-doll sheet ordinal cache must cover current TS sprite counts");

constexpr std::array<bool, kCategoryCount> kZeroIndexed = {{
    false, false, false, false, true, false, true,
    false, true, true, true, true, true,
    true, true, true, true, false, true,
    true, false, true, true, true, true, true,
    true, true, true, true, true, true,
    true, true, true, true, true,
}};

constexpr std::array<int, kCategoryCount> kGenerationChance = {{
    0, 0, 10, 0, 0, 0, 0,
    0, 10, 10, 0, 10, 10,
    10, 10, 10, 0, 0, 0,
    10, 0, 10, 10, 10, 10, 10,
    10, 0, 10, 0, 100, 0,
    100, 0, 100, 100, 100,
}};

constexpr std::array<bool, kCategoryCount> kSecondaryOnly = {{
    false, false, false, false, false, false, false,
    false, false, false, true, false, false,
    false, false, false, false, false, false,
    false, false, false, false, false, false, false,
    false, true, false, true, false, true,
    false, true, false, false, false,
}};

constexpr std::array<Category, kCategoryCount> kSecondaryMirror = {{
    Category::Count, Category::Count, Category::Count, Category::Count,
    Category::Count, Category::Count, Category::Count,
    Category::Count, Category::HairD, Category::Count, Category::Count,
    Category::Count, Category::Count, Category::Count, Category::Count,
    Category::Count, Category::Count, Category::Count, Category::Count,
    Category::Count, Category::Count, Category::Count, Category::Count,
    Category::Count, Category::Count, Category::Count,
    Category::BackB, Category::Count, Category::ShoulderB, Category::Count,
    Category::ChopB, Category::Count, Category::StrikeB, Category::Count,
    Category::Count, Category::Count, Category::Count,
}};

constexpr std::array<bool, kCategoryCount> kForbidZero = {{
    false, false, false, true, true, true, true,
    true, false, false, false, false, false,
    false, false, false, false, false, false,
    false, false, false, false, false, false, false,
    false, false, false, false, false, false,
    false, false, false, false, false,
}};

constexpr std::array<Category, kCategoryCount> kSpriteLayers = {{
    Category::Body, Category::Head, Category::Arms, Category::BottomA,
    Category::BottomB, Category::TopA, Category::TopB, Category::HairA,
    Category::HairB, Category::HairC, Category::HairD, Category::AccessoryA,
    Category::AccessoryB, Category::AccessoryC, Category::AccessoryD,
    Category::JacketA, Category::JacketB, Category::Shoes, Category::Socks,
    Category::Gloves, Category::Eyes, Category::Eyebrows, Category::Face,
    Category::Ears, Category::Horns, Category::Mid, Category::BackA,
    Category::BackB, Category::ShoulderA, Category::ShoulderB,
    Category::ChopA, Category::ChopB, Category::StrikeA, Category::StrikeB,
    Category::Reap, Category::Water, Category::Seed,
}};

constexpr std::array<Category, kCategoryCount> kSpriteOrder = {{
    Category::AccessoryA, Category::AccessoryB, Category::AccessoryC,
    Category::AccessoryD, Category::Arms, Category::Body, Category::BottomA,
    Category::BottomB, Category::Eyes, Category::HairA, Category::HairB,
    Category::HairC, Category::HairD, Category::Head, Category::JacketA,
    Category::JacketB, Category::Shoes, Category::Socks, Category::TopA,
    Category::TopB, Category::BackA, Category::BackB, Category::ShoulderA,
    Category::ShoulderB, Category::Ears, Category::Eyebrows, Category::Face,
    Category::Gloves, Category::Horns, Category::Mid, Category::StrikeA,
    Category::StrikeB, Category::ChopA, Category::ChopB, Category::Water,
    Category::Reap, Category::Seed,
}};

// Direction order in TS z-index JSON is front/right/back/left. Store as
// Front, Back, Left, Right to match Direction.
constexpr std::array<std::array<std::uint8_t, 4>, kCategoryCount> kZIndex = {{
    {{11, 11, 10, 10}}, // Body
    {{4, 6, 2, 2}}, // Head
    {{19, 12, 18, 18}}, // Arms
    {{14, 18, 13, 13}}, // BottomA
    {{16, 20, 15, 15}}, // BottomB
    {{15, 19, 14, 14}}, // TopA
    {{20, 15, 19, 19}}, // TopB
    {{7, 9, 5, 5}}, // HairA
    {{8, 27, 6, 6}}, // HairB
    {{26, 8, 27, 27}}, // HairC
    {{31, 29, 31, 31}}, // HairD
    {{29, 2, 22, 22}}, // AccessoryA
    {{27, 23, 28, 28}}, // AccessoryB
    {{23, 25, 23, 23}}, // AccessoryC
    {{28, 24, 29, 29}}, // AccessoryD
    {{17, 21, 16, 16}}, // JacketA
    {{21, 16, 20, 20}}, // JacketB
    {{13, 14, 12, 12}}, // Shoes
    {{12, 13, 11, 11}}, // Socks
    {{22, 17, 21, 21}}, // Gloves
    {{5, 4, 3, 3}}, // Eyes
    {{6, 5, 4, 4}}, // Eyebrows
    {{25, 3, 26, 26}}, // Face
    {{9, 7, 7, 7}}, // Ears
    {{30, 28, 30, 30}}, // Horns
    {{18, 22, 17, 17}}, // Mid
    {{2, 31, 25, 25}}, // BackA
    {{3, 30, 9, 9}}, // BackB
    {{24, 26, 24, 24}}, // ShoulderA
    {{10, 10, 8, 8}}, // ShoulderB
    {{32, 32, 32, 32}}, // ChopA
    {{1, 1, 1, 1}}, // ChopB
    {{32, 32, 32, 32}}, // StrikeA
    {{1, 1, 1, 1}}, // StrikeB
    {{32, 1, 32, 32}}, // Reap
    {{32, 1, 32, 32}}, // Water
    {{32, 1, 32, 32}}, // Seed
}};

constexpr std::array<int, std::size_t(AnimationType::Count)> kFrameCounts = {{
    4, 6, 6, 4, 4, 4, 3, 4, 4,
}};

constexpr std::array<int, std::size_t(AnimationType::Count)> kStartIndices = {{
    0, 16, 40, 64, 80, 96, 112, 124, 140,
}};

constexpr std::array<std::array<std::uint16_t, 6>, std::size_t(AnimationType::Count)> kDelays = {{
    {{500, 300, 200, 200, 0, 0}},
    {{125, 125, 125, 125, 125, 125}},
    {{100, 100, 100, 100, 100, 100}},
    {{125, 125, 125, 500, 0, 0}},
    {{200, 125, 125, 200, 0, 0}},
    {{200, 125, 150, 200, 0, 0}},
    {{300, 125, 200, 0, 0, 0}},
    {{500, 125, 125, 500, 125, 125}},
    {{200, 125, 125, 200, 0, 0}},
}};

constexpr std::array<int, std::size_t(AnimationType::Count)> kDelayCounts = {{
    4, 6, 6, 4, 4, 4, 3, 6, 4,
}};

constexpr std::array<std::array<std::uint32_t, 4>, 36> kBasePalettes = {{
    {{rgb(0x411310), rgb(0x5b1610), rgb(0x8b1b16), rgb(0xb22c2e)}},
    {{rgb(0x410e1f), rgb(0x62172c), rgb(0x901739), rgb(0xbe3c46)}},
    {{rgb(0x531707), rgb(0x741706), rgb(0x9e2108), rgb(0xdb4b45)}},
    {{rgb(0x622700), rgb(0x833700), rgb(0xa54e0a), rgb(0xd37005)}},
    {{rgb(0x622700), rgb(0x8a3f0e), rgb(0xb56410), rgb(0xd18c07)}},
    {{rgb(0x452101), rgb(0x6b391a), rgb(0x875614), rgb(0xaf7540)}},
    {{rgb(0x513601), rgb(0x8b5c00), rgb(0xa07a01), rgb(0xc89a08)}},
    {{rgb(0x784e0f), rgb(0xa87f20), rgb(0xd8b039), rgb(0xeed767)}},
    {{rgb(0x574d0e), rgb(0x6a7613), rgb(0x74a62e), rgb(0xa1cf4c)}},
    {{rgb(0x363008), rgb(0x4d441e), rgb(0x7a6c37), rgb(0xb98a44)}},
    {{rgb(0x2f1b0c), rgb(0x353011), rgb(0x605d20), rgb(0x8e8336)}},
    {{rgb(0x2a3c10), rgb(0x2f5b0b), rgb(0x407c08), rgb(0x67a425)}},
    {{rgb(0x081f0b), rgb(0x0c2c1a), rgb(0x16422f), rgb(0x347154)}},
    {{rgb(0x12451b), rgb(0x1d7157), rgb(0x208e95), rgb(0x3fb0b6)}},
    {{rgb(0x155029), rgb(0x287a68), rgb(0x469e8e), rgb(0x64c1ab)}},
    {{rgb(0x1e3c4d), rgb(0x15586e), rgb(0x257c80), rgb(0x43c0c3)}},
    {{rgb(0x1c2848), rgb(0x172b5f), rgb(0x253c7c), rgb(0x2f52b2)}},
    {{rgb(0x0c3b5f), rgb(0x144c8b), rgb(0x1c669c), rgb(0x4388d3)}},
    {{rgb(0x1b3063), rgb(0x3c3584), rgb(0x4f51aa), rgb(0x6970cb)}},
    {{rgb(0x17213a), rgb(0x2a2b5f), rgb(0x393471), rgb(0x4e58ab)}},
    {{rgb(0x241242), rgb(0x36196d), rgb(0x572c8c), rgb(0x7746bc)}},
    {{rgb(0x27103c), rgb(0x431b4b), rgb(0x6f3172), rgb(0x944b9a)}},
    {{rgb(0x5b145f), rgb(0x7c2868), rgb(0xaf397c), rgb(0xd05bb4)}},
    {{rgb(0x711848), rgb(0x952452), rgb(0xc13856), rgb(0xe26095)}},
    {{rgb(0x671b38), rgb(0x87363d), rgb(0xb15d5d), rgb(0xd67d85)}},
    {{rgb(0x494c53), rgb(0x6b7083), rgb(0x8486a8), rgb(0xb2aec9)}},
    {{rgb(0x5f4d51), rgb(0x8f7a77), rgb(0xaa9d99), rgb(0xc7b8b4)}},
    {{rgb(0x565960), rgb(0x808497), rgb(0x9ea0c2), rgb(0xc1c4d9)}},
    {{rgb(0x5c5764), rgb(0x9e97b6), rgb(0xd3cbf2), rgb(0xe5deef)}},
    {{rgb(0x171212), rgb(0x19191e), rgb(0x1d1d22), rgb(0x3a2d38)}},
    {{rgb(0x0f0c0a), rgb(0x131316), rgb(0x291e2d), rgb(0x552855)}},
    {{rgb(0x0b0a0f), rgb(0x12121e), rgb(0x1b2030), rgb(0x1c4370)}},
    {{rgb(0x34201c), rgb(0x4d2727), rgb(0x6d3d2e), rgb(0x8f5142)}},
    {{rgb(0x1a0e03), rgb(0x291606), rgb(0x351c09), rgb(0x5b3f25)}},
    {{rgb(0x1a0e03), rgb(0x2f0f02), rgb(0x3d1104), rgb(0x6d2c06)}},
    {{rgb(0x44281b), rgb(0x624031), rgb(0x976b57), rgb(0xcb947c)}},
}};

constexpr std::array<std::array<std::uint32_t, 4>, 8> kSkintonePalettes = {{
    {{rgb(0x834545), rgb(0xec9e9e), rgb(0xfbc3c3), rgb(0xf6ddd6)}},
    {{rgb(0x874d36), rgb(0xe2ab83), rgb(0xf0ccb4), rgb(0xf6e2d6)}},
    {{rgb(0x6c3f2a), rgb(0xe99a7a), rgb(0xf8b49d), rgb(0xf8cbc1)}},
    {{rgb(0x653925), rgb(0xb9724b), rgb(0xd89866), rgb(0xe9b689)}},
    {{rgb(0x65381d), rgb(0xae7732), rgb(0xcd965a), rgb(0xddb277)}},
    {{rgb(0x5a2c17), rgb(0x8f5629), rgb(0xa57047), rgb(0xc29068)}},
    {{rgb(0x3c1c0c), rgb(0x622f14), rgb(0x784124), rgb(0x925638)}},
    {{rgb(0x220909), rgb(0x431220), rgb(0x52232c), rgb(0x6d3740)}},
}};

constexpr std::array<std::array<std::uint32_t, 4>, 8> kToolPalettes = {{
    {{rgb(0x391c13), rgb(0x5c2c20), rgb(0x804932), rgb(0xab7152)}},
    {{rgb(0x3b434a), rgb(0x4b5c61), rgb(0x697f88), rgb(0x9db6c2)}},
    {{rgb(0x5c2e16), rgb(0x884928), rgb(0xb06c36), rgb(0xcf8c4c)}},
    {{rgb(0x4b4644), rgb(0x6e6a69), rgb(0x9a9290), rgb(0xcfc5c3)}},
    {{rgb(0x654921), rgb(0x9e7030), rgb(0xdca93d), rgb(0xf6d252)}},
    {{rgb(0x183546), rgb(0x21526a), rgb(0x2e889a), rgb(0x42c6c3)}},
    {{rgb(0x2c1846), rgb(0x45216a), rgb(0x7d2e9a), rgb(0xb642c6)}},
    {{rgb(0x48122e), rgb(0x932350), rgb(0xbd377f), rgb(0xff6eae)}},
}};

constexpr std::array<std::uint32_t, 4> kGray4 = {{
    rgb(0x000000), rgb(0x404040), rgb(0x808080), rgb(0xffffff),
}};
constexpr std::array<std::uint32_t, 4> kSkinGray4 = {{
    rgb(0x000000), rgb(0x545454), rgb(0xaaaaaa), rgb(0xffffff),
}};
constexpr std::array<std::uint32_t, 3> kShoeGray3 = {{
    rgb(0x000000), rgb(0x404040), rgb(0x808080),
}};
constexpr std::array<std::uint32_t, 3> kSockGray3 = {{
    rgb(0x404040), rgb(0x808080), rgb(0xffffff),
}};

std::uint16_t le16(const std::vector<std::uint8_t>& bytes, std::size_t off) {
    if (off + 1 >= bytes.size()) return 0;
    return std::uint16_t(bytes[off]) | (std::uint16_t(bytes[off + 1]) << 8);
}

std::uint32_t le32(const std::vector<std::uint8_t>& bytes, std::size_t off) {
    if (off + 3 >= bytes.size()) return 0;
    return std::uint32_t(bytes[off])
        | (std::uint32_t(bytes[off + 1]) << 8)
        | (std::uint32_t(bytes[off + 2]) << 16)
        | (std::uint32_t(bytes[off + 3]) << 24);
}

int delay_count(AnimationType animation) {
    const std::size_t i = std::size_t(animation);
    return i < kDelayCounts.size() ? kDelayCounts[i] : 0;
}

PaletteSlot palette_slot(Category category) {
    switch (category) {
        case Category::AccessoryA: return PaletteSlot::AccessoryA;
        case Category::AccessoryB: return PaletteSlot::AccessoryB;
        case Category::AccessoryC: return PaletteSlot::AccessoryC;
        case Category::AccessoryD: return PaletteSlot::AccessoryD;
        case Category::BottomA:
        case Category::BottomB: return PaletteSlot::Bottom;
        case Category::Eyes: return PaletteSlot::Eye;
        case Category::HairA:
        case Category::HairB:
        case Category::HairC:
        case Category::HairD: return PaletteSlot::Hair;
        case Category::JacketA:
        case Category::JacketB: return PaletteSlot::Jacket;
        case Category::Shoes: return PaletteSlot::Shoe;
        case Category::Socks: return PaletteSlot::Sock;
        case Category::Body:
        case Category::Head:
        case Category::Arms:
        case Category::Ears: return PaletteSlot::Skintone;
        case Category::TopA:
        case Category::TopB: return PaletteSlot::Top;
        case Category::ShoulderA:
        case Category::ShoulderB: return PaletteSlot::Shoulder;
        case Category::BackA:
        case Category::BackB: return PaletteSlot::Back;
        case Category::Eyebrows: return PaletteSlot::Eyebrows;
        case Category::Face: return PaletteSlot::Face;
        case Category::Horns: return PaletteSlot::Horns;
        case Category::Mid: return PaletteSlot::Mid;
        case Category::Gloves: return PaletteSlot::Gloves;
        case Category::ChopA:
        case Category::ChopB: return PaletteSlot::Chop;
        case Category::StrikeA:
        case Category::StrikeB: return PaletteSlot::Strike;
        case Category::Reap: return PaletteSlot::Reap;
        case Category::Water: return PaletteSlot::Water;
        case Category::Seed: return PaletteSlot::Seed;
        default: return PaletteSlot::Top;
    }
}

int palette_count(PaletteSlot slot) {
    switch (slot) {
        case PaletteSlot::Skintone: return int(kSkintonePalettes.size());
        case PaletteSlot::Eye: return int(kBasePalettes.size());
        case PaletteSlot::Chop:
        case PaletteSlot::Strike:
        case PaletteSlot::Reap:
        case PaletteSlot::Water:
        case PaletteSlot::Seed: return int(kToolPalettes.size());
        default: return int(kBasePalettes.size());
    }
}

PaletteConfig palette_config(Category category, const CharacterDescriptor& d) {
    PaletteConfig cfg{};
    const PaletteSlot slot = palette_slot(category);
    const int row = d.paletteRows[pi(slot)] % std::uint8_t(palette_count(slot));

    auto put4 = [&cfg](const std::array<std::uint32_t, 4>& gray,
                      const std::array<std::uint32_t, 4>& colors) {
        cfg.colorCount = 4;
        for (int i = 0; i < 4; ++i) {
            cfg.grayscale[std::size_t(i)] = gray[std::size_t(i)];
            cfg.colors[std::size_t(i)] = colors[std::size_t(i)];
        }
    };
    auto put3 = [&cfg](const std::array<std::uint32_t, 3>& gray,
                      const std::array<std::uint32_t, 4>& colors) {
        cfg.colorCount = 3;
        for (int i = 0; i < 3; ++i) {
            cfg.grayscale[std::size_t(i)] = gray[std::size_t(i)];
            cfg.colors[std::size_t(i)] = colors[std::size_t(i)];
        }
    };

    switch (slot) {
        case PaletteSlot::Skintone:
            put4(kSkinGray4, kSkintonePalettes[std::size_t(row)]);
            break;
        case PaletteSlot::Eye: {
            std::array<std::uint32_t, 4> eye = {{
                rgb(0xfff0f7),
                kBasePalettes[std::size_t(row)][0],
                kBasePalettes[std::size_t(row)][1],
                kBasePalettes[std::size_t(row)][2],
            }};
            put4(kGray4, eye);
            break;
        }
        case PaletteSlot::Shoe:
            put3(kShoeGray3, kBasePalettes[std::size_t(row)]);
            break;
        case PaletteSlot::Sock:
            put3(kSockGray3, kBasePalettes[std::size_t(row)]);
            break;
        case PaletteSlot::Chop:
        case PaletteSlot::Strike:
        case PaletteSlot::Reap:
        case PaletteSlot::Water:
        case PaletteSlot::Seed:
            put4(kGray4, kToolPalettes[std::size_t(row)]);
            break;
        default:
            put4(kGray4, kBasePalettes[std::size_t(row)]);
            break;
    }
    return cfg;
}

void mirror_secondary(CharacterDescriptor& d, Category primary) {
    const Category secondary = kSecondaryMirror[ci(primary)];
    if (secondary == Category::Count) return;
    d.sprites[ci(secondary)] =
        std::uint8_t(clamp_sprite_index(secondary, d.sprites[ci(primary)]));
}

void apply_jacket_topb_rule(CharacterDescriptor& d) {
    if (d.sprites[ci(Category::JacketA)] == 0) {
        d.sprites[ci(Category::JacketB)] = 0;
    }
    set_hidden(d, Category::TopB, d.sprites[ci(Category::JacketB)] != 0);
}

std::uint8_t random_row(Rng& rng, PaletteSlot slot) {
    const int count = palette_count(slot);
    if (count <= 0) return 0;
    return std::uint8_t(rng.next_u32() % std::uint32_t(count));
}

bool generation_chance_failed(Rng& rng, int chance) {
    if (chance <= 0) return false;
    if (chance >= 100) return false;
    constexpr std::uint64_t kRngRange = 1ull << 32;
    const std::uint64_t roll = std::uint64_t(rng.next_u32()) * 100ull;
    const std::uint64_t threshold = std::uint64_t(chance) * kRngRange;
    return roll >= threshold;
}

std::uint8_t weighted_eye(Rng& rng) {
    constexpr int kEyeCount = 14;
    int total = 0;
    for (int i = 1; i <= kEyeCount; ++i) total += i <= 7 ? 100 : 10;
    int roll = int(rng.next_u32() % std::uint32_t(total));
    for (int i = 1; i <= kEyeCount; ++i) {
        roll -= i <= 7 ? 100 : 10;
        if (roll < 0) return std::uint8_t(i - 1);
    }
    return 0;
}

constexpr std::uint8_t z_index(Category category, Direction direction) {
    return kZIndex[ci(category)][std::size_t(direction)];
}

constexpr std::array<std::array<Category, kCategoryCount>, 4> make_render_orders() {
    std::array<std::array<Category, kCategoryCount>, 4> orders{};
    for (std::size_t di = 0; di < orders.size(); ++di) {
        auto& order = orders[di];
        order = kSpriteOrder;
        const Direction dir = Direction(std::uint8_t(di));
        for (std::size_t i = 1; i < order.size(); ++i) {
            const Category cur = order[i];
            std::size_t j = i;
            while (j > 0 && z_index(cur, dir) < z_index(order[j - 1], dir)) {
                order[j] = order[j - 1];
                --j;
            }
            order[j] = cur;
        }
    }
    return orders;
}

constexpr auto kRenderOrders = make_render_orders();

const std::array<Category, kCategoryCount>& render_order(Direction direction) {
    const std::size_t i = std::size_t(direction);
    return kRenderOrders[i < kRenderOrders.size() ? i : 0];
}

} // namespace

int calculate_tile_index(AnimationType animation,
                         Direction direction,
                         std::uint8_t frame) {
    const int directionIndex = int(direction);
    const int frameCount = animation_frame_count(animation);
    const int start = animation_start_index(animation);
    if (directionIndex < 0 || directionIndex >= 4 || frameCount <= 0) return 0;
    const int out = start + directionIndex * frameCount + int(frame % std::uint8_t(frameCount));
    return out < kTilesPerSheet ? out : kTilesPerSheet - 1;
}

bool AtlasData::load_bin(const char* path) {
    atlasWidth = 0;
    atlasHeight = 0;
    sheetCount = 0;
    entryCount = 0;
    entries.clear();
    sheetNames.clear();
    nameToOrdinal.clear();
    for (auto& row : characterSheetOrdinals) row.fill(-1);
    characterSheetOrdinalsReady = false;

    std::FILE* f = std::fopen(path, "rb");
    if (!f) return false;
    if (std::fseek(f, 0, SEEK_END) != 0) {
        std::fclose(f);
        return false;
    }
    const long sizeLong = std::ftell(f);
    if (sizeLong <= 0) {
        std::fclose(f);
        return false;
    }
    if (std::fseek(f, 0, SEEK_SET) != 0) {
        std::fclose(f);
        return false;
    }

    std::vector<std::uint8_t> bytes;
    bytes.resize(std::size_t(sizeLong));
    const std::size_t read = std::fread(bytes.data(), 1, bytes.size(), f);
    std::fclose(f);
    if (read != bytes.size() || bytes.size() < 20) return false;
    if (bytes[0] != 'A' || bytes[1] != 'T' || bytes[2] != 'L' || bytes[3] != 'S') {
        return false;
    }
    if (le16(bytes, 4) != 1) return false;

    sheetCount = le16(bytes, 6);
    entryCount = le32(bytes, 8);
    atlasWidth = le16(bytes, 12);
    atlasHeight = le16(bytes, 14);
    const std::uint32_t tableSize = le32(bytes, 16);
    std::size_t off = 20;
    if (off + tableSize > bytes.size()) return false;
    const std::size_t tableOff = off;
    off += tableSize;
    off += (4u - (off % 4u)) % 4u;
    if (off + std::size_t(sheetCount) * 2u > bytes.size()) return false;

    sheetNames.reserve(sheetCount);
    nameToOrdinal.reserve(sheetCount * 2u);
    for (std::uint16_t i = 0; i < sheetCount; ++i) {
        const std::uint16_t nameOff = le16(bytes, off);
        off += 2;
        if (nameOff >= tableSize) return false;
        std::size_t end = std::size_t(nameOff);
        while (end < tableSize && bytes[tableOff + end] != 0) ++end;
        if (end >= tableSize) return false;
        std::string name(reinterpret_cast<const char*>(bytes.data() + tableOff + nameOff),
                         end - std::size_t(nameOff));
        nameToOrdinal.emplace(name, i);
        sheetNames.push_back(std::move(name));
    }

    off += (16u - (off % 16u)) % 16u;
    if (off + std::size_t(entryCount) * 16u > bytes.size()) return false;

    entries.resize(entryCount);
    for (std::uint32_t i = 0; i < entryCount; ++i) {
        const std::size_t base = off + std::size_t(i) * 16u;
        entries[std::size_t(i)] = {
            le16(bytes, base + 0),
            le16(bytes, base + 2),
            le16(bytes, base + 4),
            le16(bytes, base + 6),
            le16(bytes, base + 8),
            le16(bytes, base + 10),
        };
    }
    char rel[96];
    for (std::size_t cat = 0; cat < kCategoryCount; ++cat) {
        const Category category = Category(std::uint8_t(cat));
        const int count = std::min(sprite_count(category), kMaxSpritesPerCategory);
        for (int sprite = 0; sprite < count; ++sprite) {
            if (!build_sprite_relative_path(category, sprite, rel, sizeof rel)) {
                continue;
            }
            characterSheetOrdinals[cat][std::size_t(sprite)] =
                std::int16_t(sheet_ordinal(std::string_view(rel)));
        }
    }
    characterSheetOrdinalsReady = true;
    return true;
}

int AtlasData::sheet_ordinal(std::string_view relativePath) const {
    const auto it = nameToOrdinal.find(relativePath);
    if (it == nameToOrdinal.end()) return -1;
    return int(it->second);
}

int AtlasData::sheet_ordinal(Category category, int spriteIndex) const {
    const std::size_t cat = ci(category);
    if (!characterSheetOrdinalsReady || cat >= characterSheetOrdinals.size()) return -1;
    const int clamped = clamp_sprite_index(category, spriteIndex);
    if (clamped < 0 || clamped >= kMaxSpritesPerCategory) return -1;
    return int(characterSheetOrdinals[cat][std::size_t(clamped)]);
}

const AtlasEntry* AtlasData::entry(std::size_t index) const {
    if (index >= entries.size()) return nullptr;
    return &entries[index];
}

const char* category_name(Category category) {
    const std::size_t i = ci(category);
    return i < kCategoryNames.size() ? kCategoryNames[i] : "";
}

const char* animation_name(AnimationType animation) {
    switch (animation) {
        case AnimationType::Idle: return "idle";
        case AnimationType::Walk: return "walk";
        case AnimationType::Run: return "run";
        case AnimationType::Pickup: return "pickup";
        case AnimationType::Strike: return "strike";
        case AnimationType::Chop: return "chop";
        case AnimationType::Seed: return "seed";
        case AnimationType::Water: return "water";
        case AnimationType::Reap: return "reap";
        default: return "";
    }
}

const char* direction_name(Direction direction) {
    switch (direction) {
        case Direction::Front: return "front";
        case Direction::Back: return "back";
        case Direction::Left: return "left";
        case Direction::Right: return "right";
        default: return "";
    }
}

int animation_frame_count(AnimationType animation) {
    const std::size_t i = std::size_t(animation);
    return i < kFrameCounts.size() ? kFrameCounts[i] : 0;
}

int animation_start_index(AnimationType animation) {
    const std::size_t i = std::size_t(animation);
    return i < kStartIndices.size() ? kStartIndices[i] : 0;
}

const std::array<std::uint16_t, 6>& animation_delays_ms(AnimationType animation) {
    const std::size_t i = std::size_t(animation);
    return kDelays[i < kDelays.size() ? i : 0];
}

AnimationState make_animation_state(AnimationType animation,
                                    Direction direction,
                                    float elapsedMs) {
    AnimationState state{};
    state.animation = animation;
    state.direction = direction;
    state.playing = true;
    const auto& delays = animation_delays_ms(animation);
    const int count = delay_count(animation);
    int total = 0;
    for (int i = 0; i < count; ++i) total += delays[std::size_t(i)];
    if (total <= 0) return state;
    int t = int(elapsedMs);
    t %= total;
    if (t < 0) t += total;
    for (int i = 0; i < count; ++i) {
        const int d = delays[std::size_t(i)];
        if (t < d) {
            state.frame = std::uint8_t(i);
            state.frameTimerMs = float(t);
            return state;
        }
        t -= d;
    }
    state.frame = 0;
    state.frameTimerMs = 0.0f;
    return state;
}

void update_animation(AnimationState& state, float deltaMs, bool loop) {
    if (!state.playing) return;
    const auto& delays = animation_delays_ms(state.animation);
    const int count = delay_count(state.animation);
    if (count <= 0) return;
    const int frame = int(state.frame);
    const float frameDelay = float(delays[std::size_t(frame < count ? frame : 0)]);
    state.frameTimerMs += deltaMs;
    if (state.frameTimerMs >= frameDelay) {
        if (!loop && frame == count - 1) {
            state.frameTimerMs = frameDelay;
        } else {
            state.frameTimerMs = 0.0f;
            state.frame = std::uint8_t((frame + 1) % count);
        }
    }
}

void set_animation(AnimationState& state, AnimationType animation) {
    if (state.animation == animation) return;
    state.animation = animation;
    state.frame = 0;
    state.frameTimerMs = 0.0f;
}

void set_direction(AnimationState& state, Direction direction) {
    if (state.direction == direction) return;
    state.direction = direction;
}

bool is_animation_complete(const AnimationState& state) {
    const int count = delay_count(state.animation);
    if (count <= 0) return true;
    const auto& delays = animation_delays_ms(state.animation);
    const int frame = int(state.frame);
    return frame == count - 1
        && state.frameTimerMs >= float(delays[std::size_t(frame)]);
}

void reset_animation(AnimationState& state) {
    state.frame = 0;
    state.frameTimerMs = 0.0f;
}

CharacterDescriptor make_default_character() {
    CharacterDescriptor d{};
    d.seed = 1u;
    d.hiddenMask = 0u;
    for (Category c : kSpriteLayers) {
        d.sprites[ci(c)] = 0;
    }
    d.paletteRows[pi(PaletteSlot::Skintone)] = 0;
    d.paletteRows[pi(PaletteSlot::Eye)] = 0;
    d.paletteRows[pi(PaletteSlot::Chop)] = 3;
    d.paletteRows[pi(PaletteSlot::Strike)] = 3;
    d.paletteRows[pi(PaletteSlot::Reap)] = 3;
    d.paletteRows[pi(PaletteSlot::Water)] = 3;
    d.paletteRows[pi(PaletteSlot::Seed)] = 3;
    return d;
}

void apply_appearance_preset(CharacterDescriptor& descriptor,
                             AppearancePreset preset) {
    switch (preset) {
        case AppearancePreset::Backpack:
            if (descriptor.sprites[ci(Category::BackA)] == 0) {
                descriptor.sprites[ci(Category::BackA)] = 1;
            }
            mirror_secondary(descriptor, Category::BackA);
            break;
        case AppearancePreset::ShoulderArmor:
            if (descriptor.sprites[ci(Category::ShoulderA)] == 0) {
                descriptor.sprites[ci(Category::ShoulderA)] = 1;
            }
            mirror_secondary(descriptor, Category::ShoulderA);
            break;
        case AppearancePreset::Horns:
            if (descriptor.sprites[ci(Category::Horns)] == 0) {
                descriptor.sprites[ci(Category::Horns)] = 1;
            }
            break;
        case AppearancePreset::None:
        case AppearancePreset::Count:
        default:
            break;
    }
}

CharacterDescriptor generate_character(std::uint32_t seed) {
    return generate_character(seed, AppearancePreset::None);
}

CharacterDescriptor generate_character(std::uint32_t seed,
                                       AppearancePreset preset) {
    CharacterDescriptor d = make_default_character();
    d.seed = seed ? seed : 1u;
    Rng rng(d.seed);

    for (std::size_t i = 0; i < kPaletteSlotCount; ++i) {
        d.paletteRows[i] = random_row(rng, PaletteSlot(std::uint8_t(i)));
    }

    for (Category category : kSpriteLayers) {
        if (category == Category::Body || category == Category::Head) continue;
        if (kSecondaryOnly[ci(category)]) continue;

        const int chance = kGenerationChance[ci(category)];
        if (generation_chance_failed(rng, chance)) {
            if (kZeroIndexed[ci(category)] || category == Category::Arms) {
                d.sprites[ci(category)] = 0;
            } else {
                set_hidden(d, category, true);
            }
            mirror_secondary(d, category);
            continue;
        }

        if (category == Category::Eyes) {
            d.sprites[ci(category)] = weighted_eye(rng);
        } else {
            const int minIndex = kForbidZero[ci(category)] ? 1 : 0;
            const int count = sprite_count(category);
            const int range = std::max(count - minIndex, 0);
            d.sprites[ci(category)] = range > 0
                ? std::uint8_t(int(rng.next_u32() % std::uint32_t(range)) + minIndex)
                : 0;
        }
        mirror_secondary(d, category);
    }

    apply_jacket_topb_rule(d);
    apply_appearance_preset(d, preset);
    return d;
}

std::uint64_t descriptor_hash(const CharacterDescriptor& descriptor) {
    std::uint64_t h = 1469598103934665603ull;
    auto mix = [&h](std::uint64_t v) {
        h ^= v;
        h *= 1099511628211ull;
    };
    mix(descriptor.seed);
    mix(descriptor.hiddenMask);
    for (std::uint8_t v : descriptor.sprites) mix(v);
    for (std::uint8_t v : descriptor.paletteRows) mix(v);
    return h;
}

GpuCharacterDescriptor make_gpu_descriptor(const CharacterDescriptor& descriptor) {
    GpuCharacterDescriptor g{};
    for (std::size_t i = 0; i < kCategoryCount; ++i) {
        g.sprites[i] = descriptor.sprites[i];
    }
    g.hiddenMask[0] = std::uint32_t(descriptor.hiddenMask);
    g.hiddenMask[1] = std::uint32_t(descriptor.hiddenMask >> 32);
    for (std::size_t i = 0; i < kPaletteSlotCount; ++i) {
        g.paletteRows[i] = descriptor.paletteRows[i];
    }
    return g;
}

bool is_hidden(const CharacterDescriptor& descriptor, Category category) {
    return (descriptor.hiddenMask & (std::uint64_t(1) << ci(category))) != 0u;
}

void set_hidden(CharacterDescriptor& descriptor, Category category, bool hidden) {
    const std::uint64_t bit = std::uint64_t(1) << ci(category);
    if (hidden) descriptor.hiddenMask |= bit;
    else descriptor.hiddenMask &= ~bit;
}

int sprite_count(Category category) {
    const std::size_t i = ci(category);
    return i < kSpriteCounts.size() ? kSpriteCounts[i] : 0;
}

int clamp_sprite_index(Category category, int index) {
    const int count = sprite_count(category);
    if (count <= 0) return 0;
    if (index < 0) return 0;
    if (index >= count) return count - 1;
    return index;
}

bool build_sprite_relative_path(Category category,
                                int spriteIndex,
                                char* out,
                                std::size_t outSize) {
    if (!out || outSize == 0) return false;
    const std::size_t i = ci(category);
    if (i >= kCategoryNames.size()) return false;
    const int clamped = clamp_sprite_index(category, spriteIndex);
    const int spriteNumber = kZeroIndexed[i] ? clamped : std::max(1, clamped + 1);
    const int n = std::snprintf(out, outSize, "%s/%s_%03d.png",
                                kCategoryNames[i], kFileStems[i], spriteNumber);
    return n > 0 && std::size_t(n) < outSize;
}

std::size_t build_render_plan(const AtlasData& atlas,
                              const CharacterDescriptor& descriptor,
                              const AnimationState& animation,
                              RenderLayer* out,
                              std::size_t outCapacity) {
    if (!out || outCapacity == 0) return 0;
    const std::array<Category, kCategoryCount>& order = render_order(animation.direction);

    const int tileIndex = calculate_tile_index(animation.animation,
                                               animation.direction,
                                               animation.frame);
    std::size_t count = 0;
    for (Category category : order) {
        if (is_hidden(descriptor, category)) continue;
        const int sheet = atlas.sheet_ordinal(category, descriptor.sprites[ci(category)]);
        if (sheet < 0) continue;
        const std::size_t entryIndex = std::size_t(sheet) * std::size_t(kTilesPerSheet)
                                     + std::size_t(tileIndex);
        const AtlasEntry* e = atlas.entry(entryIndex);
        if (!e || e->w == 0 || e->h == 0) continue;
        out[count++] = RenderLayer{category, entryIndex, *e, palette_config(category, descriptor)};
        if (count >= outCapacity) break;
    }
    return count;
}

std::size_t count_missing_required_assets(const AtlasData& atlas,
                                          const CharacterDescriptor& descriptor) {
    constexpr std::array<Category, 4> kRequired = {{
        Category::Body, Category::Head, Category::Arms, Category::Eyes,
    }};
    std::size_t missing = 0;
    const int tileIndex = 0;
    for (Category category : kRequired) {
        const int sheet = atlas.sheet_ordinal(category, descriptor.sprites[ci(category)]);
        if (sheet < 0) {
            ++missing;
            continue;
        }
        const AtlasEntry* e = atlas.entry(std::size_t(sheet) * std::size_t(kTilesPerSheet)
                                        + std::size_t(tileIndex));
        if (!e || e->w == 0 || e->h == 0) ++missing;
    }
    return missing;
}

// ── Shared frame compositor (single source of truth) ───────────────────────
namespace {

std::uint8_t chan(std::uint32_t rgb, int shift) {
    return std::uint8_t((rgb >> shift) & 0xFFu);
}

bool close_rgb(std::uint32_t a, std::uint32_t b) {
    const int ar = int(chan(a, 16)), ag = int(chan(a, 8)), ab = int(chan(a, 0));
    const int br = int(chan(b, 16)), bg = int(chan(b, 8)), bb = int(chan(b, 0));
    const int dr = ar - br, dg = ag - bg, db = ab - bb;
    return dr * dr + dg * dg + db * db <= 162;
}

std::uint32_t apply_palette(std::uint8_t r, std::uint8_t g, std::uint8_t b,
                            const PaletteConfig& palette) {
    const int rg = int(r) - int(g);
    const int gb = int(g) - int(b);
    const std::uint32_t src = (std::uint32_t(r) << 16)
                            | (std::uint32_t(g) << 8) | std::uint32_t(b);
    if (rg < -2 || rg > 2 || gb < -2 || gb > 2) return src;
    for (int i = 0; i < int(palette.colorCount); ++i) {
        if (close_rgb(src, palette.grayscale[std::size_t(i)])) {
            return palette.colors[std::size_t(i)];
        }
    }
    return src;
}

void blend_pixel(std::uint8_t* dst, std::uint8_t sr, std::uint8_t sg,
                 std::uint8_t sb, std::uint8_t sa) {
    if (sa == 0) return;
    if (sa == 255 || dst[3] == 0) {
        dst[0] = sr; dst[1] = sg; dst[2] = sb; dst[3] = sa;
        return;
    }
    const int da = int(dst[3]);
    const int inv = 255 - int(sa);
    const int outA = int(sa) + da * inv / 255;
    if (outA <= 0) return;
    dst[0] = std::uint8_t((int(sr) * int(sa) + int(dst[0]) * da * inv / 255) / outA);
    dst[1] = std::uint8_t((int(sg) * int(sa) + int(dst[1]) * da * inv / 255) / outA);
    dst[2] = std::uint8_t((int(sb) * int(sa) + int(dst[2]) * da * inv / 255) / outA);
    dst[3] = std::uint8_t(outA);
}

} // namespace

std::uint64_t paperdoll_frame_key(const CharacterDescriptor& descriptor,
                                  const AnimationState& animation) {
    std::uint64_t h = descriptor_hash(descriptor);
    h ^= (std::uint64_t(animation.frame) + 0x9e3779b97f4a7c15ull
        + (h << 6) + (h >> 2));
    h ^= (std::uint64_t(animation.animation) << 48);
    h ^= (std::uint64_t(animation.direction) << 56);
    return h;
}

bool compose_paperdoll_rgba8(const AtlasData& atlas,
                             const std::uint8_t* atlasPixels,
                             int atlasW,
                             int atlasH,
                             const CharacterDescriptor& descriptor,
                             const AnimationState& animation,
                             std::uint8_t* outPixels) {
    if (!atlasPixels || atlasW <= 0 || atlasH <= 0 || !outPixels) return false;
    std::array<RenderLayer, kCategoryCount> layers{};
    const std::size_t layerCount = build_render_plan(atlas, descriptor, animation,
                                                     layers.data(), layers.size());
    if (layerCount == 0) return false;

    std::memset(outPixels, 0,
                std::size_t(kLogicalTileSize) * kLogicalTileSize * 4u);
    for (std::size_t li = 0; li < layerCount; ++li) {
        const RenderLayer& layer = layers[li];
        const AtlasEntry& e = layer.entry;
        for (int y = 0; y < int(e.h); ++y) {
            const int dy = int(e.oy) + y;
            if (dy < 0 || dy >= kLogicalTileSize) continue;
            const int sy = int(e.v0) + y;
            if (sy < 0 || sy >= atlasH) continue;
            for (int x = 0; x < int(e.w); ++x) {
                const int dx = int(e.ox) + x;
                if (dx < 0 || dx >= kLogicalTileSize) continue;
                const int sx = int(e.u0) + x;
                if (sx < 0 || sx >= atlasW) continue;

                const std::size_t src = (std::size_t(sy) * std::size_t(atlasW)
                                      + std::size_t(sx)) * 4u;
                const std::uint8_t sa = atlasPixels[src + 3u];
                if (sa < 3) continue;
                const std::uint32_t col = apply_palette(atlasPixels[src + 0u],
                                                        atlasPixels[src + 1u],
                                                        atlasPixels[src + 2u],
                                                        layer.palette);
                const std::size_t dst = (std::size_t(dy) * std::size_t(kLogicalTileSize)
                                      + std::size_t(dx)) * 4u;
                blend_pixel(outPixels + dst, chan(col, 16), chan(col, 8),
                            chan(col, 0), sa);
            }
        }
    }
    return true;
}

} // namespace sm::character
