#include "macro/vk_macro_renderer.h"

#include "gpu/vk_device.h"
#include "macro/features.h"
#include "tables/biomes.h"
#include "macro/knowledge.h"
#include "macro/macro_night.h"
#include "macro/map_generator.h"
#include "macro/tree_layer.h"
#include "macro/zones.h"

#include <SDL.h>

#include <algorithm>
#include <cstddef>
#include <cstdio>
#include <vector>

namespace sm {
namespace {

// std140/std430 push-constant block — must match shaders/macro.frag `Push`.
struct MacroPush {
    // `viewSize` стоял здесь вторым именем ОДНОЙ величины: и он, и
    // `resolution` получали `ext.width/height`, а шейдер делил на первое,
    // чтобы умножить на второе. Параллельные константы одной величины
    // сводятся к одной (ЗАКОН КОНСТАНТ), выжил делитель `gl_FragCoord` —
    // он честно называет кадровый буфер. Снос поля из СЕРЕДИНЫ блока
    // безопасен именно потому, что ниже всё заполняется ПО ИМЕНИ: пропуск
    // не компилируется, а не связывается молча (шрам сессии 31).
    float resolution[2];
    float mapSize[2];
    float cam[2];
    float zoom;
    float seaLevel;
    float seed;
    float timeOfDay;
    float nightDarken;
    float elapsed;  // real seconds — drives haze flow / water shimmer
    float mapStyle; // 0 = the living world; 1 = the CHART (map page document)
};

// БАЙТОВАЯ СЕТКА ГРУЗИТСЯ ОДНОЙ ДВЕРЬЮ — `create_r8`.
// Здесь стояла вторая: `expand_r8` раздувал байт в RGBA8 (байт в R, нули и
// 255 в остальных трёх), и фича, зона и деревья ехали вчетверо толще, хотя
// шейдер читает у них ровно канал `.r`. Поле знания с самого начала шло
// `create_r8` — то есть вторая дверь доказывала ненужность первой. R8_UNORM
// даёт шейдеру ТО ЖЕ число (байт/255), поэтому картинка не двигается, а
// видеопамяти на карте 1024² уходит на 3 МиБ меньше на каждую из трёх сеток.

// Encode the knowledge layer for the shader: one byte per cell, level/2 in
// UNORM (0 / 128 / 255 for Unknown / Explored / Visible). Sampled with LINEAR
// + repeat so the fog border breathes across a cell instead of stepping;
// macro.frag decodes with sample * 2.
bool encode_knowledge_field(const KnowledgeLayer* k,
                            std::vector<std::uint8_t>& out, int& w, int& h) {
    if (!k || !k->has_complete_storage()) return false;
    w = k->width;
    h = k->height;
    const std::size_t n = std::size_t(w) * std::size_t(h);
    out.resize(n);
    for (std::size_t i = 0; i < n; ++i) {
        const std::uint8_t v = k->data[i];
        out[i] = v >= kKnowledgeVisible ? 255u
               : v == kKnowledgeExplored ? 128u
                                         : 0u;
    }
    return true;
}

// Encode the tree-count layer for the shader: one byte per cell,
// round(count / 16384 * 255) — u_treeMap reads it back as density [0,1].
// 64-tree quantisation is far below anything the map sprite can resolve.
bool encode_tree_field(const TreeLayer* layer, std::vector<std::uint8_t>& out,
                       int& w, int& h) {
    if (!layer || !layer->has_complete_storage()) return false;
    w = layer->width;
    h = layer->height;
    const std::size_t n = std::size_t(w) * std::size_t(h);
    out.resize(n);
    for (std::size_t i = 0; i < n; ++i) {
        const std::uint32_t c = std::min<std::uint32_t>(
            layer->data[i], std::uint32_t(kMaxTreesPerCell));
        out[i] = std::uint8_t((c * 255u + kMaxTreesPerCell / 2u)
                              / std::uint32_t(kMaxTreesPerCell));
    }
    return true;
}

} // namespace

bool MacroRendererVk::init(const gpu::VulkanDevice& dev, VkRenderPass pass) {
    // Descriptor set 0 = seven combined image samplers (master/feature/zone
    // + night light field + tree field + knowledge + biome palette).
    VkDescriptorSetLayoutBinding bindings[7]{};
    for (std::uint32_t i = 0; i < 7; ++i) {
        bindings[i].binding = i;
        bindings[i].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
        bindings[i].descriptorCount = 1;
        bindings[i].stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;
    }
    VkDescriptorSetLayoutCreateInfo dlci{};
    dlci.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
    dlci.bindingCount = 7;
    dlci.pBindings = bindings;
    if (vkCreateDescriptorSetLayout(dev.device, &dlci, nullptr, &setLayout_) != VK_SUCCESS)
        return false;

    VkDescriptorPoolSize ps{VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 7};
    VkDescriptorPoolCreateInfo dpci{};
    dpci.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
    dpci.maxSets = 1;
    dpci.poolSizeCount = 1;
    dpci.pPoolSizes = &ps;
    if (vkCreateDescriptorPool(dev.device, &dpci, nullptr, &pool_) != VK_SUCCESS)
        return false;

    VkDescriptorSetAllocateInfo dsai{};
    dsai.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
    dsai.descriptorPool = pool_;
    dsai.descriptorSetCount = 1;
    dsai.pSetLayouts = &setLayout_;
    if (vkAllocateDescriptorSets(dev.device, &dsai, &set_) != VK_SUCCESS)
        return false;

    char* base = SDL_GetBasePath();
    char vpath[1024], fpath[1024];
    std::snprintf(vpath, sizeof vpath, "%sshaders/fullscreen.vert.spv", base ? base : "./");
    std::snprintf(fpath, sizeof fpath, "%sshaders/macro.frag.spv", base ? base : "./");
    if (base) SDL_free(base);
    if (!pipeline_.create(dev, pass, vpath, fpath, sizeof(MacroPush), setLayout_)) {
        std::fprintf(stderr, "[macro-vk] pipeline failed\n");
        return false;
    }
    return true;
}

void MacroRendererVk::free_textures(const gpu::VulkanDevice& dev) {
    biomePalette_.destroy(dev);
    knowledgeField_.destroy(dev);
    treeField_.destroy(dev);
    lightField_.destroy(dev);
    zone_.destroy(dev);
    feature_.destroy(dev);
    master_.destroy(dev);
}

void MacroRendererVk::upload(const gpu::VulkanDevice& dev, const TerrainData& td,
                             const FeatureLayer& features, const ZoneLayer& zones,
                             const std::uint8_t* lightFieldRgba,
                             std::uint32_t lightFieldW, std::uint32_t lightFieldH,
                             const TreeLayer* treeLayer,
                             const KnowledgeLayer* knowledge) {
    if (uploaded_) {
        vkDeviceWaitIdle(dev.device);
        free_textures(dev);
        uploaded_ = false;
    }
    if (td.width <= 0 || td.height <= 0 || td.rgba.empty()) return;

    const std::uint8_t blank = 0;

    // Master: already RGBA8 (R=height, G=moisture, B=temperature, A=mask).
    master_.create_rgba8(dev, std::uint32_t(td.width), std::uint32_t(td.height),
                         td.rgba.data(), true, true);

    // Feature: sanitized byte grid, R8, nearest.
    const std::uint8_t* fd = features.complete_cells_or_sanitized(scratch_);
    if (fd) {
        feature_.create_r8(dev, std::uint32_t(features.width),
                           std::uint32_t(features.height), fd, false, true);
    } else {
        feature_.create_r8(dev, 1, 1, &blank, false, true);
    }

    // Zone: the raw danger byte IS the texel, R8, nearest.
    if (zones.has_complete_storage() && zones.width > 0 && zones.height > 0) {
        const std::size_t n = std::size_t(zones.width) * std::size_t(zones.height);
        std::vector<std::uint8_t> zb(n, 0);
        for (std::size_t i = 0; i < n && i < zones.data.size(); ++i)
            zb[i] = zones.data[i];   // the raw danger byte IS the texel
        zone_.create_r8(dev, std::uint32_t(zones.width),
                        std::uint32_t(zones.height), zb.data(), false, true);
    } else {
        zone_.create_r8(dev, 1, 1, &blank, false, true);
    }

    // Light field: per-cell RGB night glow (macro_lighting bake), linear+repeat
    // for smooth torus-wrapped falloff. 1x1 black when no lights are supplied,
    // so binding 3 is always valid.
    if (lightFieldRgba && lightFieldW > 0 && lightFieldH > 0) {
        lightField_.create_rgba8(dev, lightFieldW, lightFieldH,
                                 lightFieldRgba, true, true);
    } else {
        const std::uint8_t blackRGBA[4] = {0, 0, 0, 255};
        lightField_.create_rgba8(dev, 1, 1, blackRGBA, true, true);
    }

    // Tree-count field: R8 density (count/16384), nearest — cell
    // probes must read exact per-cell values, like the feature map. 1x1 zero
    // when no layer is supplied, so binding 4 is always valid.
    {
        std::vector<std::uint8_t> tb;
        int tw = 0, th = 0;
        if (encode_tree_field(treeLayer, tb, tw, th)) {
            treeField_.create_r8(dev, std::uint32_t(tw), std::uint32_t(th),
                                 tb.data(), false, true);
        } else {
            treeField_.create_r8(dev, 1, 1, &blank, false, true);
        }
    }

    // Knowledge field: R8 level/2 (see encode_knowledge_field), LINEAR +
    // repeat so the fog border breathes across the torus. When no layer is
    // supplied — the dev harnesses (macro_shot, gpu_smoke) and any caller
    // predating the fog — a 1×1 VISIBLE texel is bound: their pictures stay
    // exactly the world with no fog, and binding 5 is always valid. The game
    // itself always passes the real grid (boot_world resets it).
    {
        std::vector<std::uint8_t> kb;
        int kw = 0, kh = 0;
        if (encode_knowledge_field(knowledge, kb, kw, kh)) {
            knowledgeField_.create_r8(dev, std::uint32_t(kw), std::uint32_t(kh),
                                      kb.data(), true, true);
        } else {
            const std::uint8_t visible = 255u;
            knowledgeField_.create_r8(dev, 1, 1, &visible, true, true);
        }
    }

    // ПАЛИТРА БИОМОВ — прямая выгрузка авторской строки `kBiomes`, без
    // квантования: одна таблица на игру, и новый биом становится видимым на
    // карте добавлением СТРОКИ, а не правкой шейдера. Ширина образа И ЕСТЬ
    // счёт биомов — шейдер спрашивает её `textureSize`, поэтому литерала
    // «одиннадцать» не заводится ни там, ни здесь.
    {
        std::vector<float> pal(std::size(kBiomes) * 4u);
        for (std::size_t i = 0; i < std::size(kBiomes); ++i) {
            pal[i * 4 + 0] = kBiomes[i].r;
            pal[i * 4 + 1] = kBiomes[i].g;
            pal[i * 4 + 2] = kBiomes[i].b;
            pal[i * 4 + 3] = 1.0f;
        }
        biomePalette_.create_rgba32f(dev, std::uint32_t(std::size(kBiomes)), 1,
                                     pal.data(), false, false);
    }

    // Bind the seven textures into set 0.
    const gpu::VulkanTexture* tex[7] = {&master_, &feature_, &zone_,
                                        &lightField_, &treeField_,
                                        &knowledgeField_, &biomePalette_};
    VkDescriptorImageInfo dii[7]{};
    VkWriteDescriptorSet writes[7]{};
    for (std::uint32_t i = 0; i < 7; ++i) {
        dii[i].imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
        dii[i].imageView = tex[i]->view;
        dii[i].sampler = tex[i]->sampler;
        writes[i].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
        writes[i].dstSet = set_;
        writes[i].dstBinding = i;
        writes[i].descriptorCount = 1;
        writes[i].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
        writes[i].pImageInfo = &dii[i];
    }
    vkUpdateDescriptorSets(dev.device, 7, writes, 0, nullptr);
    uploaded_ = true;
}

void MacroRendererVk::upload_knowledge_field(const gpu::VulkanDevice& dev,
                                             const KnowledgeLayer* knowledge) {
    if (!uploaded_) return;

    std::vector<std::uint8_t> kb;
    int kw = 0, kh = 0;
    if (!encode_knowledge_field(knowledge, kb, kw, kh)) return;

    // The common case — every player cell crossing: the grid dims are those
    // the texture was created with, so rewrite the texels IN PLACE. The copy
    // is queue-ordered behind the in-flight frame's sampling (update_region's
    // FRAGMENT_SHADER→TRANSFER barrier) and waits only on its own transfer
    // fence — no realloc, no descriptor rewrite, no vkDeviceWaitIdle drain.
    if (knowledgeField_.width == std::uint32_t(kw)
        && knowledgeField_.height == std::uint32_t(kh)) {
        knowledgeField_.update_region(dev, 0, 0, std::uint32_t(kw),
                                      std::uint32_t(kh), kb.data());
        return;
    }

    // Dimension change (a different world) — the rare path, same discipline
    // as upload_tree_field: idle, recreate, rewrite binding 5.
    vkDeviceWaitIdle(dev.device);
    knowledgeField_.destroy(dev);
    knowledgeField_.create_r8(dev, std::uint32_t(kw), std::uint32_t(kh),
                              kb.data(), true, true);
    VkDescriptorImageInfo dii{};
    dii.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
    dii.imageView = knowledgeField_.view;
    dii.sampler = knowledgeField_.sampler;
    VkWriteDescriptorSet write{};
    write.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
    write.dstSet = set_;
    write.dstBinding = 5;
    write.descriptorCount = 1;
    write.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    write.pImageInfo = &dii;
    vkUpdateDescriptorSets(dev.device, 1, &write, 0, nullptr);
}

void MacroRendererVk::upload_tree_field(const gpu::VulkanDevice& dev,
                                        const TreeLayer* treeLayer) {
    // Same discipline as upload_light_field: nothing to patch before the
    // first full upload(); idle the device before replacing the live image.
    // Callers gate this on TreeLayer.revision, never per frame.
    if (!uploaded_) return;
    vkDeviceWaitIdle(dev.device);
    treeField_.destroy(dev);

    std::vector<std::uint8_t> tb;
    int tw = 0, th = 0;
    if (encode_tree_field(treeLayer, tb, tw, th)) {
        treeField_.create_r8(dev, std::uint32_t(tw), std::uint32_t(th),
                             tb.data(), false, true);
    } else {
        const std::uint8_t blank = 0;
        treeField_.create_r8(dev, 1, 1, &blank, false, true);
    }

    // Rewrite only binding 4; the other samplers stay live.
    VkDescriptorImageInfo dii{};
    dii.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
    dii.imageView = treeField_.view;
    dii.sampler = treeField_.sampler;
    VkWriteDescriptorSet write{};
    write.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
    write.dstSet = set_;
    write.dstBinding = 4;
    write.descriptorCount = 1;
    write.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    write.pImageInfo = &dii;
    vkUpdateDescriptorSets(dev.device, 1, &write, 0, nullptr);
}

void MacroRendererVk::upload_zone_field(const gpu::VulkanDevice& dev,
                                        const ZoneLayer& zones) {
    // Same discipline as upload_tree_field: nothing to patch before the first
    // full upload(); idle the device before replacing the live image. Zones
    // were baked once per PROCESS until the one rebaker landed (2026-08-24) —
    // this surgical path is what lets a load, a season or a living landmark
    // move the danger the map shows (binding 2, u_zoneMap).
    if (!uploaded_) return;
    vkDeviceWaitIdle(dev.device);
    zone_.destroy(dev);

    if (zones.has_complete_storage() && zones.width > 0 && zones.height > 0) {
        const std::size_t n =
            std::size_t(zones.width) * std::size_t(zones.height);
        std::vector<std::uint8_t> zb(n, 0);
        for (std::size_t i = 0; i < n && i < zones.data.size(); ++i)
            zb[i] = zones.data[i];   // the raw danger byte IS the texel
        zone_.create_r8(dev, std::uint32_t(zones.width),
                        std::uint32_t(zones.height), zb.data(), false, true);
    } else {
        const std::uint8_t blank = 0;
        zone_.create_r8(dev, 1, 1, &blank, false, true);
    }

    VkDescriptorImageInfo dii{};
    dii.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
    dii.imageView = zone_.view;
    dii.sampler = zone_.sampler;
    VkWriteDescriptorSet write{};
    write.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
    write.dstSet = set_;
    write.dstBinding = 2;
    write.descriptorCount = 1;
    write.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    write.pImageInfo = &dii;
    vkUpdateDescriptorSets(dev.device, 1, &write, 0, nullptr);
}

void MacroRendererVk::upload_light_field(const gpu::VulkanDevice& dev,
                                         const std::uint8_t* lightFieldRgba,
                                         std::uint32_t lightFieldW,
                                         std::uint32_t lightFieldH) {
    // The descriptor set is allocated and its five bindings are written by
    // upload(); until that has run there is nothing to patch. This keeps the
    // refresh path safe to call unconditionally by the world logic.
    if (!uploaded_) return;

    // Same discipline as upload(): idle the device before tearing down the old
    // image (a prior frame's draw may still reference it), then recreate just
    // the light field. Callers gate this on real world-state change, so the
    // wait happens at most once per in-game day — never per frame.
    vkDeviceWaitIdle(dev.device);
    lightField_.destroy(dev);

    if (lightFieldRgba && lightFieldW > 0 && lightFieldH > 0) {
        lightField_.create_rgba8(dev, lightFieldW, lightFieldH,
                                 lightFieldRgba, true, true);
    } else {
        const std::uint8_t blackRGBA[4] = {0, 0, 0, 255};
        lightField_.create_rgba8(dev, 1, 1, blackRGBA, true, true);
    }

    // Rewrite only binding 3; the other samplers still point at the live
    // master/feature/zone textures.
    VkDescriptorImageInfo dii{};
    dii.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
    dii.imageView = lightField_.view;
    dii.sampler = lightField_.sampler;
    VkWriteDescriptorSet write{};
    write.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
    write.dstSet = set_;
    write.dstBinding = 3;
    write.descriptorCount = 1;
    write.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    write.pImageInfo = &dii;
    vkUpdateDescriptorSets(dev.device, 1, &write, 0, nullptr);
}

void MacroRendererVk::record(VkCommandBuffer cmd, VkExtent2D ext, const TerrainData& td,
                             float camX, float camY, float zoom, float seaLevel,
                             float timeOfDay, float elapsed, bool mapStyle) {
    if (!uploaded_) return;

    VkViewport vp{};
    vp.width = float(ext.width);
    vp.height = float(ext.height);
    vp.maxDepth = 1.0f;
    VkRect2D sc{};
    sc.extent = ext;
    vkCmdSetViewport(cmd, 0, 1, &vp);
    vkCmdSetScissor(cmd, 0, 1, &sc);

    vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, pipeline_.pipeline);
    vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, pipeline_.layout,
                            0, 1, &set_, 0, nullptr);

    MacroPush pc{};
    pc.resolution[0] = float(ext.width);
    pc.resolution[1] = float(ext.height);
    pc.mapSize[0] = float(td.width);
    pc.mapSize[1] = float(td.height);
    pc.cam[0] = camX;
    pc.cam[1] = camY;
    pc.zoom = zoom;
    pc.seaLevel = seaLevel;
    // ФАЗА УЗОРА МИРА. Шейдер прибавляет `seed * k` (k = 0.11…0.41) к
    // координате узора, то есть сид работает ФАЗОЙ, а фаза значима лишь по
    // модулю периода. Поэтому сид складывается в пролёт узора самого мира —
    // сторона карты в клетках × делитель клетки, — и предел здесь ВЫВЕДЕН, а
    // не выбран: он ровно того же порядка, что сама координата, поэтому не
    // уводит хеш в область, где у float32 кончается мантисса. Сторона мира
    // есть степень двойки (ЗАКОН АДРЕСА), делитель клетки тоже, значит фолд —
    // МАСКА, и деления на пути кадра не появляется.
    // Здесь стояло `1.0f` с обоснованием «GL macro renderer hardcodes
    // u_seed = 1.0» — при том что GL-путь удалён вместе с тем файлом.
    const std::uint32_t patternSpan = std::uint32_t(td.width) * 16u;
    pc.seed = float(td.seed & (patternSpan - 1u));
    pc.timeOfDay = timeOfDay;
    pc.nightDarken = macro_night_darken(timeOfDay);
    pc.elapsed = elapsed;
    pc.mapStyle = mapStyle ? 1.0f : 0.0f;
    vkCmdPushConstants(cmd, pipeline_.layout, VK_SHADER_STAGE_FRAGMENT_BIT,
                       0, sizeof(pc), &pc);
    vkCmdDraw(cmd, 3, 1, 0, 0);
}

void MacroRendererVk::destroy(const gpu::VulkanDevice& dev) {
    if (uploaded_) {
        free_textures(dev);
        uploaded_ = false;
    }
    pipeline_.destroy(dev);
    if (pool_) {
        vkDestroyDescriptorPool(dev.device, pool_, nullptr);
        pool_ = VK_NULL_HANDLE;
    }
    if (setLayout_) {
        vkDestroyDescriptorSetLayout(dev.device, setLayout_, nullptr);
        setLayout_ = VK_NULL_HANDLE;
    }
}

} // namespace sm
