#define SDL_MAIN_HANDLED
#include <SDL.h>

#include "assets/character_paperdoll_gl.h"
#include "gl/gl.h"

#include <cstdint>
#include <cstdio>

namespace {

bool expect(bool ok, const char* msg) {
    if (!ok) {
        std::fprintf(stderr, "FAIL: %s\n", msg);
        return false;
    }
    return true;
}

} // namespace

int main() {
    bool ok = true;

    if (SDL_Init(SDL_INIT_VIDEO) != 0) {
        std::fprintf(stderr, "FAIL: SDL_Init: %s\n", SDL_GetError());
        return 1;
    }
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION, 3);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MINOR_VERSION, 2);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_PROFILE_MASK, SDL_GL_CONTEXT_PROFILE_CORE);

    SDL_Window* window = SDL_CreateWindow("paperdoll-gl-smoke",
                                          SDL_WINDOWPOS_UNDEFINED,
                                          SDL_WINDOWPOS_UNDEFINED,
                                          64, 64,
                                          SDL_WINDOW_OPENGL | SDL_WINDOW_HIDDEN);
    if (!window) {
        std::fprintf(stderr, "FAIL: SDL_CreateWindow: %s\n", SDL_GetError());
        SDL_Quit();
        return 1;
    }
    SDL_GLContext ctx = SDL_GL_CreateContext(window);
    if (!ctx) {
        std::fprintf(stderr, "FAIL: SDL_GL_CreateContext: %s\n", SDL_GetError());
        SDL_DestroyWindow(window);
        SDL_Quit();
        return 1;
    }
    SDL_GL_MakeCurrent(window, ctx);

    ok &= expect(sm::gl_load_functions(), "OpenGL loader must initialize");

    sm::character::CharacterTextureCache cache;
    const sm::character::CharacterDescriptor& probeA =
        cache.descriptor_for_seed(0x2468ACE0u);
    const sm::character::CharacterDescriptor& probeB =
        cache.descriptor_for_seed(0x2468ACE0u);
    ok &= expect(&probeA == &probeB,
                 "same seed should hit the descriptor cache before eviction");
    const sm::character::CharacterDescriptor& probeBackpack =
        cache.descriptor_for_seed(0x2468ACE0u,
                                  sm::character::AppearancePreset::Backpack);
    ok &= expect(&probeBackpack != &probeA,
                 "same seed with appearance preset should occupy a separate descriptor cache entry");
    ok &= expect(probeBackpack.sprites[std::size_t(sm::character::Category::BackA)] == 1
                 && probeBackpack.sprites[std::size_t(sm::character::Category::BackB)] == 1,
                 "backpack descriptor cache entry must force mirrored backpack sprites");
    const std::uint64_t probeHash = sm::character::descriptor_hash(probeA);
    for (std::uint32_t i = 0; i < 1100u; ++i) {
        (void)cache.descriptor_for_seed(0x90000000u + i);
    }
    ok &= expect(sm::character::descriptor_hash(
                     cache.descriptor_for_seed(0x2468ACE0u)) == probeHash,
                 "descriptor cache eviction must regenerate deterministic descriptors");

    const sm::character::CharacterDescriptor& descriptor =
        cache.descriptor_for_seed(0x12345678u);
    const sm::character::AnimationState animation =
        sm::character::make_animation_state(sm::character::AnimationType::Idle,
                                            sm::character::Direction::Front,
                                            0.0f);
    const sm::character::CharacterTexture* texture =
        cache.texture_for(descriptor, animation);
    ok &= expect(cache.atlas_loaded(), "paper-doll atlas should load");
    ok &= expect(texture && texture->tex != 0, "paper-doll texture should be created");
    ok &= expect(texture && texture->w == sm::character::kLogicalTileSize,
                 "paper-doll texture width should be 48");
    ok &= expect(texture && texture->h == sm::character::kLogicalTileSize,
                 "paper-doll texture height should be 48");
    const sm::character::CharacterTexture* cachedTexture =
        cache.texture_for(descriptor, animation);
    ok &= expect(cachedTexture == texture,
                 "same descriptor/frame should return cached texture entry");
    ok &= expect(cachedTexture && texture && cachedTexture->tex == texture->tex,
                 "same descriptor/frame should reuse GL texture id");
    const sm::character::CharacterDescriptor& otherDescriptor =
        cache.descriptor_for_seed(0x12345679u);
    const sm::character::CharacterTexture* otherTexture =
        cache.texture_for(otherDescriptor, animation);
    ok &= expect(otherTexture && otherTexture->tex != 0,
                 "different descriptor should create a valid texture entry");
    ok &= expect(otherTexture != texture,
                 "different descriptor should not reuse the same texture entry");
    ok &= expect(cache.texture_for(descriptor, animation) == texture,
                 "original descriptor/frame should survive neighboring cache entries");

    if (texture && texture->tex) {
        glBindTexture(GL_TEXTURE_2D, texture->tex);
        GLint glW = 0;
        GLint glH = 0;
        glGetTexLevelParameteriv(GL_TEXTURE_2D, 0, GL_TEXTURE_WIDTH, &glW);
        glGetTexLevelParameteriv(GL_TEXTURE_2D, 0, GL_TEXTURE_HEIGHT, &glH);
        ok &= expect(glW == sm::character::kLogicalTileSize,
                     "uploaded GL texture width should be 48");
        ok &= expect(glH == sm::character::kLogicalTileSize,
                     "uploaded GL texture height should be 48");
        glBindTexture(GL_TEXTURE_2D, 0);
    }

    static constexpr sm::character::Direction kDirs[] = {
        sm::character::Direction::Front,
        sm::character::Direction::Back,
        sm::character::Direction::Left,
        sm::character::Direction::Right,
    };
    for (sm::character::Direction dir : kDirs) {
        const sm::character::AnimationState walk =
            sm::character::make_animation_state(sm::character::AnimationType::Walk,
                                                dir,
                                                250.0f);
        const sm::character::CharacterTexture* dirTexture =
            cache.texture_for(descriptor, walk);
        ok &= expect(dirTexture && dirTexture->tex != 0,
                     "directional walk texture should upload");
        ok &= expect(dirTexture && dirTexture->w == sm::character::kLogicalTileSize,
                     "directional walk texture width should be 48");
        ok &= expect(dirTexture && dirTexture->h == sm::character::kLogicalTileSize,
                     "directional walk texture height should be 48");
    }

    const std::uint64_t hash = sm::character::descriptor_hash(descriptor);
    cache.destroy();
    SDL_GL_DeleteContext(ctx);
    SDL_DestroyWindow(window);
    SDL_Quit();

    if (!ok) return 1;
    std::printf("character_paperdoll_gl_smoke_test: ok hash=%llu\n",
                static_cast<unsigned long long>(hash));
    return 0;
}
