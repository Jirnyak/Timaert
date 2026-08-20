// The drawn-sprite bank. Decoding lives here; the law it serves is in
// sprite_bank.h and sprites.md.

#define STBI_NO_HDR
#define STBI_NO_LINEAR
#define STBI_NO_PSD
#define STBI_NO_TGA
#define STBI_NO_GIF
#define STBI_NO_PIC
#define STBI_NO_PNM
#include <stb_image.h>

#include "assets/sprite_bank.h"
#include "gpu/vk_device.h"

#include <algorithm>
#include <cstdio>

namespace sm {
namespace {

// The asset search order, the same one sprite_atlas walks: the game runs from
// build/ as often as from the repo root.
unsigned char* decode_sprite(const char* file, int& w, int& h) {
    static const char* kPrefixes[] = {
        "assets/sprites/",
        "../assets/sprites/",
        "../public/assets/sprites/",
        "../../public/assets/sprites/",
        "public/assets/sprites/",
    };
    char buf[512];
    int comp = 0;
    for (const char* pre : kPrefixes) {
        std::snprintf(buf, sizeof buf, "%s%s", pre, file);
        unsigned char* px = stbi_load(buf, &w, &h, &comp, 4);
        if (px) return px;
    }
    return nullptr;
}

} // namespace

bool SpriteBank::init(const gpu::VulkanDevice& dev) {
    for (std::uint32_t& s : slotOf_) s = kNoSlot;

    constexpr std::uint32_t kSlots = sprite_bank_slot_count();
    static_assert(kSlots > 0, "no drawn body rows — the bank would be empty");

    // subTiles = 1: the paper-doll pool packed 2×2 per layer only because its
    // working set (thousands of composited frames) could not fit in MoltenVK's
    // 2048-layer cap. A picture-per-kind set is two orders of magnitude
    // smaller, so a slot is simply a layer and the shader's decode is trivial.
    // No staging ring either — every slot is uploaded once, at load.
    if (!pool_.init(dev, kSpriteBankTile, kSpriteBankTile, kSlots,
                    /*linearFilter=*/false, /*stagingRing=*/1,
                    /*framesInFlight=*/1, /*subTiles=*/1)) {
        std::fprintf(stderr, "[sprite_bank] pool init failed\n");
        return false;
    }

    std::uint32_t next = 0;
    for (std::size_t i = 0; i < std::size_t(SpriteId::Count_); ++i) {
        const SpriteDef& row = kSpriteRows[i];
        if (row.asset == nullptr || row.archetype == kNoBody) continue;

        int w = 0, h = 0;
        unsigned char* px = decode_sprite(row.asset, w, h);
        if (!px) {
            // Loud, not silent: the row promised art and the file is not there,
            // so this kind will fall back to its procedural body and the reason
            // must be on the log rather than inferred from a strange-looking
            // crowd.
            std::fprintf(stderr, "[sprite_bank] missing art for '%s': %s\n",
                         row.name, row.asset);
            continue;
        }
        // The bank stores art at ONE authored size; a mismatched sheet is an
        // authoring error, not something to silently rescale into blur.
        if (std::uint32_t(w) != kSpriteBankTile
            || std::uint32_t(h) != kSpriteBankTile) {
            std::fprintf(stderr,
                         "[sprite_bank] '%s' is %dx%d, the bank tile is %ux%u\n",
                         row.name, w, h, kSpriteBankTile, kSpriteBankTile);
            stbi_image_free(px);
            continue;
        }
        // A PNG arrives in ART order (row 0 = the head); the bank stores the
        // WORLD convention (v = 0 at the FEET — the one billboard.vert speaks
        // for every pass), so the rows flip once HERE and no shader ever flips
        // again. Skipping this is not subtle: the whole town stands on its
        // head, which is exactly what the first frame of this cutover showed.
        const std::size_t rowBytes = std::size_t(kSpriteBankTile) * 4u;
        for (std::uint32_t top = 0, bot = kSpriteBankTile - 1; top < bot;
             ++top, --bot) {
            std::uint8_t* a = px + std::size_t(top) * rowBytes;
            std::uint8_t* b = px + std::size_t(bot) * rowBytes;
            for (std::size_t i = 0; i < rowBytes; ++i) std::swap(a[i], b[i]);
        }
        if (!pool_.upload_slot_now(dev, next, px)) {
            std::fprintf(stderr, "[sprite_bank] upload failed for '%s'\n",
                         row.name);
            stbi_image_free(px);
            continue;
        }
        stbi_image_free(px);
        slotOf_[i] = next++;
    }

    std::fprintf(stderr, "[sprite_bank] %u/%u drawn bodies resident (%ux%u)\n",
                 next, kSlots, kSpriteBankTile, kSpriteBankTile);
    ready_ = true;
    return true;
}

void SpriteBank::destroy(const gpu::VulkanDevice& dev) {
    pool_.destroy(dev);
    for (std::uint32_t& s : slotOf_) s = kNoSlot;
    ready_ = false;
}

std::uint32_t SpriteBank::slot_for(SpriteId id) const {
    const std::size_t i = std::size_t(id);
    if (i >= std::size_t(SpriteId::Count_)) return kNoSlot;
    return slotOf_[i];
}

} // namespace sm
