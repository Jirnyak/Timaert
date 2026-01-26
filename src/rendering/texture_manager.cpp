#include "rendering/texture_manager.h"
#include "core/game_context.h"

#define STB_IMAGE_IMPLEMENTATION
#define STBI_NO_STDIO
#include "stb_image.h"

#include <fstream>
#include <print>

Texture load_texture(const std::string& path) {
    Texture tex{};

    // Read file into memory
    std::ifstream file(path, std::ios::binary | std::ios::ate);
    if (!file) {
        std::println(stderr, "Failed to open texture: {}", path);
        return tex;
    }

    const auto size = file.tellg();
    file.seekg(0, std::ios::beg);

    std::vector<std::uint8_t> buffer(static_cast<std::size_t>(size));
    if (!file.read(reinterpret_cast<char*>(buffer.data()), size)) {
        std::println(stderr, "Failed to read texture: {}", path);
        return tex;
    }

    int width = 0;
    int height = 0;
    int channels = 0;
    unsigned char* data =
        stbi_load_from_memory(buffer.data(),
                              static_cast<int>(buffer.size()),
                              &width,
                              &height,
                              &channels,
                              STBI_rgb_alpha);

    if (!data) {
        std::println(stderr, "Failed to decode texture: {}", path);
        return tex;
    }

    tex.width = width;
    tex.height = height;
    tex.pixels.assign(data, data + width * height * 4);

    // Create Sokol image
    sg_image_desc img_desc{};
    img_desc.width = width;
    img_desc.height = height;
    img_desc.pixel_format = SG_PIXELFORMAT_RGBA8;
    img_desc.data.mip_levels[0] = {.ptr = data, .size = static_cast<std::size_t>(width * height * 4)};
    tex.image = sg_make_image(&img_desc);

    // Create texture view for sgl_texture
    sg_view_desc view_desc{};
    view_desc.texture.image = tex.image;
    tex.view = sg_make_view(&view_desc);

    // Create sampler with nearest filtering for pixel art
    sg_sampler_desc smp_desc{};
    smp_desc.min_filter = SG_FILTER_NEAREST;
    smp_desc.mag_filter = SG_FILTER_NEAREST;
    smp_desc.mipmap_filter = SG_FILTER_NEAREST;
    smp_desc.wrap_u = SG_WRAP_CLAMP_TO_EDGE;
    smp_desc.wrap_v = SG_WRAP_CLAMP_TO_EDGE;
    tex.sampler = sg_make_sampler(&smp_desc);

    stbi_image_free(data);
    return tex;
}

Texture create_texture_from_pixels(const std::uint8_t* pixels, int width, int height) {
    Texture tex{};
    tex.width = width;
    tex.height = height;
    tex.pixels.assign(pixels, pixels + width * height * 4);

    sg_image_desc img_desc{};
    img_desc.width = width;
    img_desc.height = height;
    img_desc.pixel_format = SG_PIXELFORMAT_RGBA8;
    img_desc.data.mip_levels[0] = {.ptr = pixels, .size = static_cast<std::size_t>(width * height * 4)};
    tex.image = sg_make_image(&img_desc);

    // Create texture view
    sg_view_desc view_desc{};
    view_desc.texture.image = tex.image;
    tex.view = sg_make_view(&view_desc);

    sg_sampler_desc smp_desc{};
    smp_desc.min_filter = SG_FILTER_NEAREST;
    smp_desc.mag_filter = SG_FILTER_NEAREST;
    smp_desc.wrap_u = SG_WRAP_CLAMP_TO_EDGE;
    smp_desc.wrap_v = SG_WRAP_CLAMP_TO_EDGE;
    tex.sampler = sg_make_sampler(&smp_desc);

    return tex;
}

Texture create_dynamic_texture(int width, int height) {
    Texture tex{};
    tex.width = width;
    tex.height = height;
    tex.pixels.resize(static_cast<std::size_t>(width * height * 4), 0);

    sg_image_desc img_desc{};
    img_desc.width = width;
    img_desc.height = height;
    img_desc.pixel_format = SG_PIXELFORMAT_RGBA8;
    img_desc.usage.stream_update = true;
    tex.image = sg_make_image(&img_desc);

    // Create texture view
    sg_view_desc view_desc{};
    view_desc.texture.image = tex.image;
    tex.view = sg_make_view(&view_desc);

    sg_sampler_desc smp_desc{};
    smp_desc.min_filter = SG_FILTER_NEAREST;
    smp_desc.mag_filter = SG_FILTER_NEAREST;
    smp_desc.wrap_u = SG_WRAP_CLAMP_TO_EDGE;
    smp_desc.wrap_v = SG_WRAP_CLAMP_TO_EDGE;
    tex.sampler = sg_make_sampler(&smp_desc);

    return tex;
}

void update_texture(Texture& texture, const std::uint8_t* pixels, int width, int height) {
    if (!texture.valid() || width != texture.width || height != texture.height)
        return;

    sg_image_data img_data{};
    img_data.mip_levels[0] = {.ptr = pixels, .size = static_cast<std::size_t>(width * height * 4)};
    sg_update_image(texture.image, &img_data);
}

void TextureManager::init(int window_width, int window_height, const GameContext& ctx) {
    tile_bg_w_ = window_width;
    tile_bg_h_ = window_height;
    tile_bg_x_ = 0;
    tile_bg_y_ = 0;

    // Create shared nearest-neighbor sampler
    sg_sampler_desc smp_desc{};
    smp_desc.min_filter = SG_FILTER_NEAREST;
    smp_desc.mag_filter = SG_FILTER_NEAREST;
    smp_desc.mipmap_filter = SG_FILTER_NEAREST;
    smp_desc.wrap_u = SG_WRAP_CLAMP_TO_EDGE;
    smp_desc.wrap_v = SG_WRAP_CLAMP_TO_EDGE;
    nearest_sampler_ = sg_make_sampler(&smp_desc);

    auto load_tex = [&ctx](const char* path) { return load_texture(resolve_path(ctx, path)); };

    const std::array<const char*, TILE_TEXTURE_COUNT> tile_paths = {
        "assets/sprites/dirt.png",    // TerrainType::Nothing
        "assets/sprites/sand.png",    // TerrainType::Sand
        "assets/sprites/grass.png",   // TerrainType::Grass
        "assets/sprites/dirt.png",    // TerrainType::Dirt
        "assets/sprites/mount.png",   // TerrainType::Mount
        "assets/sprites/water.png",   // TerrainType::Water
        "assets/sprites/snow.png",    // TerrainType::Snow
        "assets/sprites/jungle.png",  // TerrainType::Jungle
        "assets/sprites/swamp.png",   // TerrainType::Swamp
        "assets/sprites/tundra.png"   // TerrainType::Tundra
    };

    for (std::size_t i = 0; i < tile_paths.size(); ++i) {
        tile_textures_[i] = load_tex(tile_paths[i]);
    }

    std::array<const char*, SPRITE_TEXTURE_COUNT> sprite_paths{};
    sprite_paths[static_cast<std::size_t>(ObjectType::Tree)] = "assets/sprites/tree.png";
    sprite_paths[static_cast<std::size_t>(ObjectType::City)] = "assets/sprites/city.png";
    sprite_paths[static_cast<std::size_t>(ObjectType::Village)] = "assets/sprites/city.png";
    sprite_paths[static_cast<std::size_t>(ObjectType::Town)] = "assets/sprites/city.png";
    sprite_paths[static_cast<std::size_t>(ObjectType::Player)] = "assets/sprites/player.png";
    sprite_paths[static_cast<std::size_t>(ObjectType::Peasant)] = "assets/sprites/peasant.png";
    sprite_paths[static_cast<std::size_t>(ObjectType::Woodcutter)] = "assets/sprites/peasant.png";
    sprite_paths[static_cast<std::size_t>(ObjectType::Merchant)] = "assets/sprites/peasant.png";
    sprite_paths[static_cast<std::size_t>(ObjectType::Caravan)] = "assets/sprites/corovan.png";
    sprite_paths[static_cast<std::size_t>(ObjectType::Bandit)] = "assets/sprites/ngirl1.png";
    sprite_paths[static_cast<std::size_t>(ObjectType::Guard)] = "assets/sprites/peasant.png";
    sprite_paths[static_cast<std::size_t>(ObjectType::Door)] = "assets/sprites/door.png";
    sprite_paths[static_cast<std::size_t>(ObjectType::Witch)] = "assets/sprites/ngirl1.png";

    for (std::size_t i = 0; i < sprite_paths.size(); ++i) {
        if (sprite_paths[i]) {
            sprite_textures_[i] = load_tex(sprite_paths[i]);
        }
    }

    std::array<const char*, ITEM_TEXTURE_COUNT> item_paths{};
    item_paths[static_cast<std::size_t>(ItemType::Coins)] = "assets/sprites/coins.png";

    for (std::size_t i = 0; i < item_paths.size(); ++i) {
        if (item_paths[i]) {
            item_textures_[i] = load_tex(item_paths[i]);
        }
    }

    const std::array<const char*, BACKGROUND_TEXTURE_COUNT> background_paths = {
        "assets/backgrounds/0.png"};

    for (std::size_t i = 0; i < background_paths.size(); ++i) {
        background_textures_[i] = load_tex(background_paths[i]);
    }

    // Create heatmap as dynamic texture
    heatmap_texture_ = create_dynamic_texture(WORLD_WIDTH, WORLD_WIDTH);
}

void TextureManager::cleanup() noexcept {
    for (auto& tex : tile_textures_) {
        tex.destroy();
    }
    for (auto& tex : sprite_textures_) {
        tex.destroy();
    }
    for (auto& tex : item_textures_) {
        tex.destroy();
    }
    for (auto& tex : background_textures_) {
        tex.destroy();
    }
    heatmap_texture_.destroy();

    if (nearest_sampler_.id != SG_INVALID_ID) {
        sg_destroy_sampler(nearest_sampler_);
        nearest_sampler_ = {0};
    }
}
