#include "assets/paperdoll_atlas.h"
#include "core/rng.h"
#include "gpu/vk_device.h"
#include "stb_image.h"

#include <cstdio>
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

    if (!atlasTex_.create_rgba8(dev, std::uint32_t(atlasW_), std::uint32_t(atlasH_),
                                atlasPixels_.data(), false, false)) {
        std::fprintf(stderr, "[paperdoll] failed to create atlas texture\n");
        return false;
    }
    
    atlasPixels_.clear();
    atlasPixels_.shrink_to_fit();

    if (!entriesSsbo_.create_device_local(dev, gpuEntries_.data(), gpuEntries_.size() * sizeof(GpuAtlasEntry), VK_BUFFER_USAGE_STORAGE_BUFFER_BIT)) {
        return false;
    }
    if (!ordinalsSsbo_.create_device_local(dev, gpuSheetOrdinals_.data(), gpuSheetOrdinals_.size() * sizeof(std::int32_t), VK_BUFFER_USAGE_STORAGE_BUFFER_BIT)) {
        return false;
    }
    
    const VkDeviceSize maxDescriptorsBytes = 16384 * sizeof(GpuCharacterDescriptor);
    if (!descriptorsSsbo_.create_host_mapped(dev, maxDescriptorsBytes, VK_BUFFER_USAGE_STORAGE_BUFFER_BIT)) {
        return false;
    }

    VkDescriptorSetLayoutBinding b[4]{};
    b[0].binding = 0; b[0].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER; b[0].descriptorCount = 1; b[0].stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;
    b[1].binding = 1; b[1].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER; b[1].descriptorCount = 1; b[1].stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;
    b[2].binding = 2; b[2].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER; b[2].descriptorCount = 1; b[2].stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;
    b[3].binding = 3; b[3].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER; b[3].descriptorCount = 1; b[3].stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT | VK_SHADER_STAGE_VERTEX_BIT;

    VkDescriptorSetLayoutCreateInfo dlci{};
    dlci.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
    dlci.bindingCount = 4;
    dlci.pBindings = b;
    vkCreateDescriptorSetLayout(dev.device, &dlci, nullptr, &setLayout_);

    VkDescriptorPoolSize ps[2]{ {VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 1}, {VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 3} };
    VkDescriptorPoolCreateInfo dpci{};
    dpci.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
    dpci.maxSets = 1;
    dpci.poolSizeCount = 2;
    dpci.pPoolSizes = ps;
    vkCreateDescriptorPool(dev.device, &dpci, nullptr, &descPool_);

    VkDescriptorSetAllocateInfo dsai{};
    dsai.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
    dsai.descriptorPool = descPool_;
    dsai.descriptorSetCount = 1;
    dsai.pSetLayouts = &setLayout_;
    vkAllocateDescriptorSets(dev.device, &dsai, &descSet_);

    VkDescriptorImageInfo dii{};
    dii.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
    dii.imageView = atlasTex_.view;
    dii.sampler = atlasTex_.sampler;

    VkDescriptorBufferInfo dbi1{};
    dbi1.buffer = entriesSsbo_.buffer; dbi1.offset = 0; dbi1.range = gpuEntries_.size() * sizeof(GpuAtlasEntry);

    VkDescriptorBufferInfo dbi2{};
    dbi2.buffer = ordinalsSsbo_.buffer; dbi2.offset = 0; dbi2.range = gpuSheetOrdinals_.size() * sizeof(std::int32_t);

    VkDescriptorBufferInfo dbi3{};
    dbi3.buffer = descriptorsSsbo_.buffer; dbi3.offset = 0; dbi3.range = maxDescriptorsBytes;

    VkWriteDescriptorSet writes[4]{};
    for(int i=0; i<4; ++i) {
        writes[i].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
        writes[i].dstSet = descSet_;
        writes[i].dstBinding = i;
        writes[i].descriptorCount = 1;
        if (i == 0) { writes[i].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER; writes[i].pImageInfo = &dii; }
        else if (i == 1) { writes[i].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER; writes[i].pBufferInfo = &dbi1; }
        else if (i == 2) { writes[i].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER; writes[i].pBufferInfo = &dbi2; }
        else if (i == 3) { writes[i].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER; writes[i].pBufferInfo = &dbi3; }
    }
    vkUpdateDescriptorSets(dev.device, 4, writes, 0, nullptr);

    return true;
}

void PaperdollAtlas::destroy(const gpu::VulkanDevice& dev) {
    if (setLayout_) vkDestroyDescriptorSetLayout(dev.device, setLayout_, nullptr);
    if (descPool_) vkDestroyDescriptorPool(dev.device, descPool_, nullptr);
    setLayout_ = VK_NULL_HANDLE; descPool_ = VK_NULL_HANDLE;
    entriesSsbo_.destroy(dev); ordinalsSsbo_.destroy(dev); descriptorsSsbo_.destroy(dev); atlasTex_.destroy(dev);
    gpuDescriptorCache_.clear(); descriptorCache_.clear(); gpuDescriptors_.clear();
    atlasPixels_.clear(); atlas_ = AtlasData{}; atlasW_ = atlasH_ = 0; loaded_ = false; loadAttempted_ = false;
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
    
    atlasPixels_.assign(px, px + std::size_t(atlasW_) * std::size_t(atlasH_) * 4u);
    stbi_image_free(px);

    gpuEntries_.reserve(atlas_.entryCount);
    for (std::size_t i = 0; i < atlas_.entryCount; ++i) {
        const AtlasEntry* e = atlas_.entry(i);
        GpuAtlasEntry ge;
        ge.uv_wh = (std::uint32_t(e->v0) << 16) | std::uint32_t(e->u0);
        ge.ox_oy = (std::uint32_t(e->h) << 16) | std::uint32_t(e->w);
        ge.pad1 = (std::uint32_t(e->oy) << 16) | std::uint32_t(e->ox);
        ge.pad2 = 0;
        gpuEntries_.push_back(ge);
    }
    gpuSheetOrdinals_.reserve(kCategoryCount * kMaxSpritesPerCategory);
    for (std::size_t c = 0; c < kCategoryCount; ++c) {
        for (std::size_t s = 0; s < kMaxSpritesPerCategory; ++s) {
            gpuSheetOrdinals_.push_back(std::int32_t(atlas_.characterSheetOrdinals[c][s]));
        }
    }

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

std::uint32_t PaperdollAtlas::register_descriptor(const CharacterDescriptor& descriptor) {
    const std::uint64_t key = descriptor_hash(descriptor);
    auto it = gpuDescriptorCache_.find(key);
    if (it != gpuDescriptorCache_.end()) {
        return it->second;
    }
    
    std::uint32_t newIndex = std::uint32_t(gpuDescriptors_.size());
    GpuCharacterDescriptor gpuDesc = make_gpu_descriptor(descriptor);
    gpuDescriptors_.push_back(gpuDesc);
    gpuDescriptorCache_.emplace(key, newIndex);
    
    if (descriptorsSsbo_.mapped) {
        GpuCharacterDescriptor* ptr = static_cast<GpuCharacterDescriptor*>(descriptorsSsbo_.mapped);
        ptr[newIndex] = gpuDesc;
    }
    
    return newIndex;
}

} // namespace sm::character
