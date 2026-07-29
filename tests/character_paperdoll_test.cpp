#include "assets/character_paperdoll.h"

#include <array>
#include <cstdio>
#include <cstring>
#include <string>
#include <string_view>

namespace {

bool expect(bool ok, const char* msg) {
    if (!ok) {
        std::fprintf(stderr, "FAIL: %s\n", msg);
        return false;
    }
    return true;
}

bool file_exists(const char* path) {
    std::FILE* f = std::fopen(path, "rb");
    if (!f) return false;
    std::fclose(f);
    return true;
}

const char* atlas_path() {
    static constexpr const char* kCandidates[] = {
        "../public/assets/character/atlas.bin",
        "assets/character/atlas.bin",
        "public/assets/character/atlas.bin",
        "C:/Timaert/public/assets/character/atlas.bin",
    };
    for (const char* p : kCandidates) {
        if (file_exists(p)) return p;
    }
    return kCandidates[0];
}

bool same_descriptor(const sm::character::CharacterDescriptor& a,
                     const sm::character::CharacterDescriptor& b) {
    return a.seed == b.seed
        && a.hiddenMask == b.hiddenMask
        && a.sprites == b.sprites
        && a.paletteRows == b.paletteRows;
}

std::uint8_t sprite_index(const sm::character::CharacterDescriptor& descriptor,
                          sm::character::Category category) {
    return descriptor.sprites[std::size_t(category)];
}

int body_tile_index(const sm::character::AtlasData& atlas,
                    const sm::character::CharacterDescriptor& descriptor,
                    const sm::character::AnimationState& state) {
    sm::character::RenderLayer plan[sm::character::kCategoryCount]{};
    const std::size_t layerCount =
        sm::character::build_render_plan(atlas, descriptor, state,
                                         plan, sm::character::kCategoryCount);
    for (std::size_t i = 0; i < layerCount; ++i) {
        if (plan[i].category == sm::character::Category::Body) {
            return int(plan[i].entryIndex % sm::character::kTilesPerSheet);
        }
    }
    return -1;
}

const sm::character::RenderLayer* find_layer(const sm::character::RenderLayer* plan,
                                             std::size_t layerCount,
                                             sm::character::Category category) {
    for (std::size_t i = 0; i < layerCount; ++i) {
        if (plan[i].category == category) return &plan[i];
    }
    return nullptr;
}

} // namespace

int main() {
    using namespace sm::character;
    bool ok = true;

    AtlasData atlas;
    ok &= expect(atlas.load_bin(atlas_path()), "character atlas.bin should load");
    ok &= expect(atlas.sheetCount == 286, "atlas sheet count should match TS manifest");
    ok &= expect(atlas.entryCount == 45760, "atlas entry count should match TS manifest");
    ok &= expect(atlas.atlasWidth == 964 && atlas.atlasHeight == 964,
                 "atlas dimensions should match TS manifest");
    ok &= expect(atlas.sheet_ordinal("Body/body_001.png") >= 0,
                 "Body/body_001.png must exist");
    ok &= expect(atlas.sheet_ordinal("Head/head_001.png") >= 0,
                 "Head/head_001.png must exist");
    ok &= expect(atlas.sheet_ordinal("HairD/haird_020.png") >= 0,
                 "mirrored HairD sheet must exist");
    const std::string paddedPath = "xxBody/body_001.pngyy";
    const std::string_view slicedPath(paddedPath.data() + 2,
                                      std::strlen("Body/body_001.png"));
    ok &= expect(atlas.sheet_ordinal(slicedPath)
                 == atlas.sheet_ordinal("Body/body_001.png"),
                 "sheet lookup must honor string_view length without a temporary path");
    ok &= expect(atlas.sheet_ordinal(Category::Body, 0)
                 == atlas.sheet_ordinal("Body/body_001.png"),
                 "cached Body sheet ordinal must match path lookup");
    ok &= expect(atlas.sheet_ordinal(Category::HairB, 20)
                 == atlas.sheet_ordinal("HairB/hairb_020.png"),
                 "cached zero-indexed HairB sheet ordinal must match path lookup");
    ok &= expect(atlas.sheet_ordinal(Category::TopB, 30)
                 == atlas.sheet_ordinal("TopB/topb_030.png"),
                 "cached high TopB sheet ordinal must match path lookup");

    char rel[96]{};
    ok &= expect(build_sprite_relative_path(Category::Body, 0, rel, sizeof rel)
                 && std::strcmp(rel, "Body/body_001.png") == 0,
                 "Body index 0 must resolve to one-indexed body_001");
    ok &= expect(build_sprite_relative_path(Category::AccessoryA, 0, rel, sizeof rel)
                 && std::strcmp(rel, "AccessoryA/accessorya_000.png") == 0,
                 "AccessoryA index 0 must resolve to zero-indexed accessorya_000");
    ok &= expect(build_sprite_relative_path(Category::HairA, 0, rel, sizeof rel)
                 && std::strcmp(rel, "HairA/haira_001.png") == 0,
                 "HairA index 0 must resolve to one-indexed haira_001");
    ok &= expect(build_sprite_relative_path(Category::HairB, 0, rel, sizeof rel)
                 && std::strcmp(rel, "HairB/hairb_000.png") == 0,
                 "HairB index 0 must resolve to zero-indexed hairb_000");

    CharacterDescriptor a = generate_character(0x12345678u);
    CharacterDescriptor b = generate_character(0x12345678u);
    CharacterDescriptor c = generate_character(0x12345679u);
    constexpr std::uint64_t kExpectedDescriptorHash = 6629795152062431341ull;
    const std::uint64_t actualDescriptorHash = descriptor_hash(a);
    ok &= expect(same_descriptor(a, b), "same seed must generate identical character");
    ok &= expect(!same_descriptor(a, c), "different seed should change generated character");
    if (actualDescriptorHash != kExpectedDescriptorHash) {
        std::fprintf(stderr, "observed fixed seed descriptor hash=%llu\n",
                     static_cast<unsigned long long>(actualDescriptorHash));
    }
    ok &= expect(actualDescriptorHash == kExpectedDescriptorHash,
                 "fixed seed descriptor hash must stay stable");
    ok &= expect(count_missing_required_assets(atlas, a) == 0,
                 "generated descriptor must not miss required body/head/arms/eyes assets");
    ok &= expect(generate_character(0u).seed == 1u,
                 "zero seed must canonicalize to deterministic seed 1");

    static constexpr std::array<Category, 5> kMirrorPrimaries = {{
        Category::HairB,
        Category::BackA,
        Category::ShoulderA,
        Category::ChopA,
        Category::StrikeA,
    }};
    static constexpr std::array<Category, 5> kMirrorSecondaries = {{
        Category::HairD,
        Category::BackB,
        Category::ShoulderB,
        Category::ChopB,
        Category::StrikeB,
    }};
    for (std::uint32_t i = 0; i < 128u; ++i) {
        const CharacterDescriptor d = generate_character(0xC0FFEE00u + i);
        ok &= expect(count_missing_required_assets(atlas, d) == 0,
                     "all sampled generated descriptors must keep required assets");
        for (std::size_t m = 0; m < kMirrorPrimaries.size(); ++m) {
            const Category primary = kMirrorPrimaries[m];
            const Category secondary = kMirrorSecondaries[m];
            const int expected = clamp_sprite_index(secondary, sprite_index(d, primary));
            ok &= expect(sprite_index(d, secondary) == expected,
                         "secondary mirror layer must follow primary sprite index");
        }
        if (sprite_index(d, Category::JacketA) == 0) {
            ok &= expect(sprite_index(d, Category::JacketB) == 0,
                         "JacketB must be cleared when JacketA is absent");
        }
        ok &= expect(is_hidden(d, Category::TopB)
                     == (sprite_index(d, Category::JacketB) != 0),
                     "TopB visibility must follow JacketB sleeve rule");
    }

    AnimationState idle = make_animation_state(AnimationType::Idle, Direction::Front, 0.0f);
    update_animation(idle, 499.0f);
    ok &= expect(idle.frame == 0, "idle frame must hold before first 500 ms delay");
    update_animation(idle, 1.0f);
    ok &= expect(idle.frame == 1 && idle.frameTimerMs == 0.0f,
                 "idle frame must advance at 500 ms");
    set_direction(idle, Direction::Back);
    ok &= expect(idle.frame == 1, "direction change must not reset current frame");
    set_animation(idle, AnimationType::Walk);
    ok &= expect(idle.frame == 0 && idle.frameTimerMs == 0.0f,
                 "animation change must reset frame and timer");
    AnimationState invalidAnim =
        make_animation_state(AnimationType(250), Direction::Front, 1000.0f);
    update_animation(invalidAnim, 1000.0f);
    ok &= expect(invalidAnim.frame == 0 && invalidAnim.frameTimerMs == 0.0f
                 && is_animation_complete(invalidAnim),
                 "invalid animation enum must fail closed without table overrun");

    AnimationState front = make_animation_state(AnimationType::Idle, Direction::Front, 0.0f);
    AnimationState back = make_animation_state(AnimationType::Idle, Direction::Back, 0.0f);
    AnimationState left = make_animation_state(AnimationType::Idle, Direction::Left, 0.0f);
    AnimationState right = make_animation_state(AnimationType::Idle, Direction::Right, 0.0f);
    AnimationState walkRight = make_animation_state(AnimationType::Walk, Direction::Right, 250.0f);
    ok &= expect(body_tile_index(atlas, a, front) == 0,
                 "front idle frame 0 should use tile 0");
    ok &= expect(body_tile_index(atlas, a, back) == 4,
                 "back idle frame 0 should use tile 4");
    ok &= expect(body_tile_index(atlas, a, left) == 8,
                 "left idle frame 0 should use tile 8");
    ok &= expect(body_tile_index(atlas, a, right) == 12,
                 "right idle frame 0 should use tile 12");
    ok &= expect(body_tile_index(atlas, a, walkRight) == 36,
                 "right walk at 250 ms should use TS tile 36");
    AnimationState waterLate = make_animation_state(AnimationType::Water,
                                                    Direction::Front,
                                                    1249.0f);
    ok &= expect(waterLate.frame == 3,
                 "water animation must honor long fourth-frame delay");
    update_animation(waterLate, 1.0f);
    ok &= expect(waterLate.frame == 4,
                 "water animation state must retain TS six-delay cycle");
    ok &= expect(body_tile_index(atlas, a, waterLate) == 124,
                 "water frame 4 must wrap through the four-frame sheet layout");

    RenderLayer plan[kCategoryCount]{};
    const std::size_t layerCount = build_render_plan(atlas, a, front, plan, kCategoryCount);
    if (layerCount != 14) {
        std::fprintf(stderr, "observed fixed seed layer count=%llu\n",
                     static_cast<unsigned long long>(layerCount));
    }
    ok &= expect(layerCount == 14, "idle front render plan should keep deterministic layer count");
    bool hasBody = false;
    bool hasHead = false;
    for (std::size_t i = 0; i < layerCount; ++i) {
        hasBody = hasBody || plan[i].category == Category::Body;
        hasHead = hasHead || plan[i].category == Category::Head;
    }
    ok &= expect(hasBody && hasHead, "render plan must include body and head layers");
    RenderLayer backPlanA[kCategoryCount]{};
    RenderLayer backPlanB[kCategoryCount]{};
    const AnimationState backWalk =
        make_animation_state(AnimationType::Walk, Direction::Back, 125.0f);
    const std::size_t backCountA =
        build_render_plan(atlas, a, backWalk, backPlanA, kCategoryCount);
    const std::size_t backCountB =
        build_render_plan(atlas, a, backWalk, backPlanB, kCategoryCount);
    ok &= expect(backCountA == backCountB,
                 "cached render order must keep stable layer counts");
    for (std::size_t i = 0; i < backCountA && i < backCountB; ++i) {
        ok &= expect(backPlanA[i].category == backPlanB[i].category
                     && backPlanA[i].entryIndex == backPlanB[i].entryIndex,
                     "cached render order must keep stable layer contents");
    }

    CharacterDescriptor def = make_default_character();
    CharacterDescriptor backpack = def;
    apply_appearance_preset(backpack, AppearancePreset::Backpack);
    ok &= expect(sprite_index(backpack, Category::BackA) == 1
                 && sprite_index(backpack, Category::BackB) == 1,
                 "backpack appearance preset must force mirrored BackA/BackB");
    CharacterDescriptor shoulderArmor = def;
    apply_appearance_preset(shoulderArmor, AppearancePreset::ShoulderArmor);
    ok &= expect(sprite_index(shoulderArmor, Category::ShoulderA) == 1
                 && sprite_index(shoulderArmor, Category::ShoulderB) == 1,
                 "guard appearance preset must force mirrored ShoulderA/ShoulderB");
    CharacterDescriptor horned = def;
    apply_appearance_preset(horned, AppearancePreset::Horns);
    ok &= expect(sprite_index(horned, Category::Horns) == 1,
                 "horns appearance preset must force Horns when absent");
    ok &= expect(descriptor_hash(generate_character(0x12345678u, AppearancePreset::Horns))
                 != actualDescriptorHash,
                 "appearance presets must produce distinct descriptor identity");

    const std::size_t defaultLayerCount = build_render_plan(atlas, def, front,
                                                            plan, kCategoryCount);
    const RenderLayer* body = find_layer(plan, defaultLayerCount, Category::Body);
    const RenderLayer* shoes = find_layer(plan, defaultLayerCount, Category::Shoes);
    ok &= expect(body && body->palette.colorCount == 4
                 && body->palette.grayscale[1] == 0x545454u,
                 "body palette must use the Skintone grayscale row");
    ok &= expect(shoes && shoes->palette.colorCount == 3
                 && shoes->palette.grayscale[0] == 0x000000u
                 && shoes->palette.grayscale[2] == 0x808080u,
                 "shoe palette must use the three-color Shoe grayscale row");

    if (!ok) return 1;
    std::printf("character_paperdoll_test: ok hash=%llu layers=%llu\n",
                static_cast<unsigned long long>(descriptor_hash(a)),
                static_cast<unsigned long long>(layerCount));
    return 0;
}
