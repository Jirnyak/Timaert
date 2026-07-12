#include "assets/paperdoll_atlas.h"
#include "gpu/vk_device.h"

#include <stb_image.h>

#include <algorithm>
#include <cstddef>
#include <cstdio>

namespace sm::character {
namespace {

bool path_exists(const char* path) {
    std::FILE* f = std::fopen(path, "rb");
    if (!f) return false;
    std::fclose(f);
    return true;
}

// Same search order as the GL cache (kept local; the GL cache is removed in
// step 5, at which point this is the sole copy).
bool find_character_asset(const char* file, char* out, std::size_t outSize) {
    static constexpr const char* kPrefixes[] = {
        "assets/character/",
        "../assets/character/",
        "../public/assets/character/",
        "../../public/assets/character/",
        "public/assets/character/",
        "C:/Timaert/public/assets/character/",
    };
    for (const char* prefix : kPrefixes) {
        const int n = std::snprintf(out, outSize, "%s%s", prefix, file);
        if (n <= 0 || std::size_t(n) >= outSize) continue;
        if (path_exists(out)) return true;
    }
    out[0] = 0;
    return false;
}

} // namespace

bool PaperdollAtlas::init(const gpu::VulkanDevice& dev,
                          std::uint32_t layerCount,
                          std::uint32_t stagingRing) {
    if (!sprite_.init(dev, std::uint32_t(kLogicalTileSize),
                      std::uint32_t(kLogicalTileSize), layerCount,
                      /*linearFilter=*/false, stagingRing)) {
        std::fprintf(stderr, "[paperdoll] sprite array init failed\n");
        return false;
    }
    slots_.assign(layerCount, Slot{});
    keyToSlot_.reserve(layerCount);
    return true;
}

void PaperdollAtlas::destroy(const gpu::VulkanDevice& dev) {
    sprite_.destroy(dev);
    slots_.clear();
    keyToSlot_.clear();
    pending_.clear();
    descriptorCache_.clear();
    atlasPixels_.clear();
    atlas_ = AtlasData{};
    atlasW_ = atlasH_ = 0;
    frameCounter_ = 0;
    loaded_ = false;
    loadAttempted_ = false;
}

bool PaperdollAtlas::load_atlas() {
    if (loaded_) return true;
    if (loadAttempted_) return false;
    loadAttempted_ = true;

    char binPath[512];
    char pngPath[512];
    if (!find_character_asset("atlas.bin", binPath, sizeof binPath)
        || !find_character_asset("atlas.png", pngPath, sizeof pngPath)) {
        std::fprintf(stderr, "[paperdoll] missing atlas.bin/atlas.png\n");
        return false;
    }
    if (!atlas_.load_bin(binPath)) {
        std::fprintf(stderr, "[paperdoll] invalid atlas bin: %s\n", binPath);
        return false;
    }
    int comp = 0;
    unsigned char* px = stbi_load(pngPath, &atlasW_, &atlasH_, &comp, 4);
    if (!px || atlasW_ <= 0 || atlasH_ <= 0) {
        std::fprintf(stderr, "[paperdoll] invalid atlas png: %s\n", pngPath);
        if (px) stbi_image_free(px);
        return false;
    }
    atlasPixels_.assign(px, px + std::size_t(atlasW_) * std::size_t(atlasH_) * 4u);
    stbi_image_free(px);
    loaded_ = true;
    std::fprintf(stderr,
                 "[paperdoll] loaded atlas (%dx%d sheets=%u entries=%u) pool=%u\n",
                 atlasW_, atlasH_, unsigned(atlas_.sheetCount),
                 unsigned(atlas_.entryCount), unsigned(sprite_.layerCount));
    return true;
}

const CharacterDescriptor& PaperdollAtlas::descriptor_for_seed(
    std::uint32_t seed, AppearancePreset preset) {
    const std::uint32_t stableSeed = seed ? seed : 1u;
    const std::uint64_t key = std::uint64_t(stableSeed)
        | (std::uint64_t(std::uint8_t(preset)) << 32);
    auto it = descriptorCache_.find(key);
    if (it != descriptorCache_.end()) return it->second;
    auto res = descriptorCache_.emplace(
        key, generate_character(stableSeed, preset));
    return res.first->second;
}

int PaperdollAtlas::layer_for(const CharacterDescriptor& descriptor,
                              const AnimationState& animation) {
    if (!load_atlas()) return -1;
    const std::uint64_t key = paperdoll_frame_key(descriptor, animation);

    // Hit: refresh recency, return resident layer.
    auto it = keyToSlot_.find(key);
    if (it != keyToSlot_.end()) {
        slots_[it->second].lastUsed = frameCounter_;
        return int(it->second);
    }

    // Miss: compose first (may fail without mutating cache state).
    pending_.emplace_back();
    Pending& p = pending_.back();
    if (!compose_paperdoll_rgba8(atlas_, atlasPixels_.data(), atlasW_, atlasH_,
                                 descriptor, animation, p.px.data())) {
        pending_.pop_back();
        return -1;
    }

    // Pick a slot: first free, else the least-recently-used one NOT touched
    // this frame (protects everything already resolved for the current frame).
    std::uint32_t chosen = std::uint32_t(slots_.size());
    std::uint32_t lruAge = frameCounter_; // slots with lastUsed==frameCounter_ are pinned
    for (std::uint32_t i = 0; i < slots_.size(); ++i) {
        if (!slots_[i].occupied) { chosen = i; break; }
        if (slots_[i].lastUsed < lruAge) { lruAge = slots_[i].lastUsed; chosen = i; }
    }
    if (chosen >= slots_.size()) {
        // Every layer is needed this very frame — extraordinarily unlikely at
        // 1024. Drop the compose; caller falls back for this frame.
        pending_.pop_back();
        return -1;
    }

    Slot& slot = slots_[chosen];
    if (slot.occupied) keyToSlot_.erase(slot.key);
    slot.key = key;
    slot.lastUsed = frameCounter_;
    slot.occupied = true;
    keyToSlot_[key] = chosen;
    p.layer = chosen;
    return int(chosen);
}

void PaperdollAtlas::begin_frame() {
    ++frameCounter_;
    sprite_.begin_frame();
    // Pin still-unflushed layers so they survive eviction until uploaded.
    for (const Pending& p : pending_) slots_[p.layer].lastUsed = frameCounter_;
}

void PaperdollAtlas::flush_uploads(VkCommandBuffer cmd) {
    if (pending_.empty()) return;
    std::size_t i = 0;
    for (; i < pending_.size(); ++i) {
        if (!sprite_.upload_layer(cmd, pending_[i].layer, pending_[i].px.data())) {
            break; // staging ring exhausted; carry the rest to next frame
        }
    }
    if (i == pending_.size()) {
        pending_.clear();
    } else {
        pending_.erase(pending_.begin(), pending_.begin() + std::ptrdiff_t(i));
    }
}

} // namespace sm::character
