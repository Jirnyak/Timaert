#include "assets/paperdoll_atlas.h"
#include "gpu/vk_device.h"
#include "stb_image.h"

#include <cstdio>
#include <cstdlib>
#include <cstring>

namespace sm::character {
namespace {
bool path_exists(const char* path) {
    std::FILE* f = std::fopen(path, "rb");
    if (!f) return false;
    std::fclose(f);
    return true;
}

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
        if (n > 0 && std::size_t(n) < outSize && path_exists(out)) return true;
    }
    return false;
}
} // namespace

bool PaperdollAtlas::init(const gpu::VulkanDevice& dev) {
    if (!load_atlas()) return false;

    if (!pool_.init(dev, std::uint32_t(kLogicalTileSize),
                    std::uint32_t(kLogicalTileSize), kPoolLayers,
                    /*linearFilter=*/false, kStagingRing,
                    kPoolFramesInFlight)) {
        std::fprintf(stderr, "[paperdoll] sprite pool init failed\n");
        return false;
    }

    layerKey_.assign(kPoolLayers, 0u);
    layerStamp_.assign(kPoolLayers, 0u);
    stamp_ = 0;
    nextFreshLayer_ = 0;
    composeScratch_.assign(
        std::size_t(kLogicalTileSize) * kLogicalTileSize * 4u, 0u);

    return true;
}

void PaperdollAtlas::destroy(const gpu::VulkanDevice& dev) {
    pool_.destroy(dev);
    frameToLayer_.clear();
    layerKey_.clear();
    layerStamp_.clear();
    descriptorCache_.clear();
    composeScratch_.clear();
    atlasPixels_.clear();
    atlas_ = AtlasData{};
    atlasW_ = atlasH_ = 0;
    loaded_ = false;
    loadAttempted_ = false;
}

bool PaperdollAtlas::load_atlas() {
    if (loaded_) return true;
    if (loadAttempted_) return false;
    loadAttempted_ = true;

    char binPath[512]; char pngPath[512];
    if (!find_character_asset("atlas.bin", binPath, sizeof binPath) || !find_character_asset("atlas.png", pngPath, sizeof pngPath)) {
        std::fprintf(stderr, "[paperdoll] missing atlas.bin/atlas.png\n"); return false;
    }
    if (!atlas_.load_bin(binPath)) {
        std::fprintf(stderr, "[paperdoll] invalid atlas bin: %s\n", binPath); return false;
    }
    int comp = 0;
    unsigned char* px = stbi_load(pngPath, &atlasW_, &atlasH_, &comp, 4);
    if (!px || atlasW_ <= 0 || atlasH_ <= 0) {
        std::fprintf(stderr, "[paperdoll] invalid atlas png: %s\n", pngPath);
        if (px) stbi_image_free(px); return false;
    }

    // Resident on purpose: every cache miss composes from these pixels.
    atlasPixels_.assign(px, px + std::size_t(atlasW_) * std::size_t(atlasH_) * 4u);
    stbi_image_free(px);

    loaded_ = true;
    return true;
}

const CharacterDescriptor& PaperdollAtlas::descriptor_for_seed(std::uint32_t seed, AppearancePreset preset) {
    const std::uint32_t stableSeed = seed ? seed : 1u;
    const std::uint64_t key = std::uint64_t(stableSeed) | (std::uint64_t(std::uint8_t(preset)) << 32);
    auto it = descriptorCache_.find(key);
    if (it != descriptorCache_.end()) return it->second;
    auto res = descriptorCache_.emplace(key, generate_character(stableSeed, preset));
    return res.first->second;
}

void PaperdollAtlas::begin_frame() {
    // TIMAERT_DOLL_STATS=1: one stderr line every 120 frames — cache health of
    // the pool (hits / composes / refusals). The refusal count is the number a
    // flickering crowd shows up in.
    static const bool statsOn = std::getenv("TIMAERT_DOLL_STATS") != nullptr;
    if (statsOn && stamp_ > 0 && stamp_ % 120u == 0u) {
        std::fprintf(stderr,
                     "[dolls] frames=%u hits=%u miss=%u noLayer=%u resident=%zu\n",
                     stamp_, statHits_, statMisses_, statNoLayer_,
                     frameToLayer_.size());
        statHits_ = statMisses_ = statNoLayer_ = 0;
    }
    ++stamp_;
    pool_.begin_frame();
}

std::uint32_t PaperdollAtlas::claim_and_compose(
    std::uint64_t key, const CharacterDescriptor& descriptor,
    const AnimationState& animation) {
    // Claim a layer — a never-used one first, else the least recently
    // stamped. A layer stamped THIS frame is being drawn and is untouchable;
    // one stamped last frame is safe to overwrite, because the upload records
    // into this frame's command buffer, which the queue executes strictly
    // after the previous frame's draws.
    std::uint32_t layer;
    if (nextFreshLayer_ < kPoolLayers) {
        layer = nextFreshLayer_++;
    } else {
        std::uint32_t best = kNoLayer;
        std::uint32_t bestStamp = stamp_;
        for (std::uint32_t i = 0; i < kPoolLayers; ++i) {
            if (layerStamp_[i] < bestStamp) {
                bestStamp = layerStamp_[i];
                best = i;
            }
        }
        if (best == kNoLayer) return kNoLayer; // whole pool visible this frame
        layer = best;
        frameToLayer_.erase(layerKey_[layer]);
    }

    // Compose the frame. A frame with no drawable layers stays fully
    // transparent — still uploaded, so the evicted tenant's pixels never show.
    std::memset(composeScratch_.data(), 0, composeScratch_.size());
    (void)compose_paperdoll_rgba8(atlas_, atlasPixels_.data(), atlasW_, atlasH_,
                                  descriptor, animation, composeScratch_.data());
    // The compositor emits art order (row 0 = the head); the pool stores the
    // WORLD convention (v = 0 at the feet — the one billboard.vert speaks for
    // every pass), so the rows flip once HERE and no shader ever flips again.
    constexpr std::size_t rowBytes = std::size_t(kLogicalTileSize) * 4u;
    std::uint8_t* px = composeScratch_.data();
    std::array<std::uint8_t, rowBytes> rowTmp;
    for (int top = 0, bot = kLogicalTileSize - 1; top < bot; ++top, --bot) {
        std::memcpy(rowTmp.data(), px + std::size_t(top) * rowBytes, rowBytes);
        std::memcpy(px + std::size_t(top) * rowBytes,
                    px + std::size_t(bot) * rowBytes, rowBytes);
        std::memcpy(px + std::size_t(bot) * rowBytes, rowTmp.data(), rowBytes);
    }
    return layer;
}

void PaperdollAtlas::commit_layer(std::uint64_t key, std::uint32_t layer) {
    frameToLayer_.emplace(key, layer);
    layerKey_[layer] = key;
    layerStamp_[layer] = stamp_;
}

void PaperdollAtlas::release_layer(std::uint32_t layer) {
    // Give a claimed-but-not-uploaded layer back: a fresh one returns to the
    // fresh pool; an evicted one is left unmapped with the oldest possible
    // stamp, so it is the first victim next time.
    if (layer + 1 == nextFreshLayer_) {
        --nextFreshLayer_;
    } else {
        layerKey_[layer] = 0u;
        layerStamp_[layer] = 0u;
    }
}

std::uint32_t PaperdollAtlas::layer_for(VkCommandBuffer cmd,
                                        const CharacterDescriptor& descriptor,
                                        const AnimationState& animation) {
    if (!loaded_) return kNoLayer;

    const std::uint64_t key = paperdoll_frame_key(descriptor, animation);
    auto it = frameToLayer_.find(key);
    if (it != frameToLayer_.end()) {
        layerStamp_[it->second] = stamp_;
        ++statHits_;
        return it->second;
    }

    const std::uint32_t layer = claim_and_compose(key, descriptor, animation);
    if (layer == kNoLayer) {
        ++statNoLayer_;
        return kNoLayer;
    }

    if (!pool_.upload_layer(cmd, layer, composeScratch_.data())) {
        release_layer(layer); // staging ring exhausted this frame
        ++statNoLayer_;
        return kNoLayer;
    }

    ++statMisses_;
    commit_layer(key, layer);
    return layer;
}

std::uint32_t PaperdollAtlas::layer_for_now(const gpu::VulkanDevice& dev,
                                            const CharacterDescriptor& descriptor,
                                            const AnimationState& animation) {
    if (!loaded_) return kNoLayer;

    const std::uint64_t key = paperdoll_frame_key(descriptor, animation);
    auto it = frameToLayer_.find(key);
    if (it != frameToLayer_.end()) {
        layerStamp_[it->second] = stamp_;
        return it->second;
    }

    const std::uint32_t layer = claim_and_compose(key, descriptor, animation);
    if (layer == kNoLayer) return kNoLayer;

    if (!pool_.upload_layer_now(dev, layer, composeScratch_.data())) {
        release_layer(layer);
        return kNoLayer;
    }

    commit_layer(key, layer);
    return layer;
}

} // namespace sm::character
