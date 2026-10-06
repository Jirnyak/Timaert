// Vulkan macro-map renderer — draws shaders/macro.frag (the procedural 2D world
// map fragment synth) from the CPU climate master + feature/zone byte
// grids. Replaces the GL MacroRenderer at the Vulkan cutover; compiles
// alongside it beforehand. Backend = gpu/ (Vulkan); this is an L1 macro concept.
#pragma once

#include <vulkan/vulkan.h>

#include <cstdint>
#include <vector>

#include "gpu/vk_pipeline.h"
#include "gpu/vk_texture.h"

namespace gpu { struct VulkanDevice; }

namespace sm {

struct TerrainData;
struct FeatureLayer;
struct ZoneLayer;
struct TreeLayer;
struct KnowledgeLayer;
struct NavWorld;

// ОТЛАДОЧНЫЙ ВИД ЗАПЕЧЁННОЙ НАВИГАЦИИ (M-239). Схема запекания этого мира
// целиком выведена из прототипа-лабиринта (CANON S7), и ГЛАВНЫЙ совет той
// работы — «рисуйте свои промежуточные данные: один кадр хитмапа заменяет час
// чтения кода» — не был исполнен ни одной строкой, из-за чего 4.3 тыс. клеток
// суши без округи не видел никто ни разу.
//
// Вид ЗАМЕЩАЕТ картинку мира, а не смешивается с ней: полупрозрачный слой
// поверх терраина прячет ровно то, что ищут, — тёмное пятно на тёмной горе.
enum class NavDebugView : std::uint8_t {
    Off,       // мир как обычно
    Regions,   // округа клетки — КАТЕГОРИЯ (соседние значения расходятся цветом)
    HomeCost,  // цена пути до своего места — СКАЛЯР (светлое у места, дальше темнее)
};

// Следующий вид по кругу. `switch` без `default:` намеренно: новый вид делает
// это место красным под компилятором (-Wswitch), а не молча невидимым.
inline NavDebugView next_nav_debug_view(NavDebugView v) {
    switch (v) {
    case NavDebugView::Off:      return NavDebugView::Regions;
    case NavDebugView::Regions:  return NavDebugView::HomeCost;
    case NavDebugView::HomeCost: return NavDebugView::Off;
    }
    return NavDebugView::Off;
}

class MacroRendererVk {
public:
    bool init(const gpu::VulkanDevice& dev, VkRenderPass pass);
    void destroy(const gpu::VulkanDevice& dev);

    // (Re)upload the world data textures (master + feature + zone) plus
    // the optional per-cell RGB night-light field (macro_lighting bake) and the
    // optional per-cell tree-count layer (macro/tree_layer.h — encoded as R8
    // count/16384, binding 5; 1x1 zero when absent). The light field is
    // width*height RGBA8; pass nullptr / 0,0 for no lights (a 1x1 black field
    // is bound so binding 4 is always valid). Load-time / on-world-change
    // only — never per frame.
    void upload(const gpu::VulkanDevice& dev, const TerrainData& td,
                const FeatureLayer& features, const ZoneLayer& zones,
                const std::uint8_t* lightFieldRgba = nullptr,
                std::uint32_t lightFieldW = 0, std::uint32_t lightFieldH = 0,
                const TreeLayer* treeLayer = nullptr,
                const KnowledgeLayer* knowledge = nullptr);

    // Surgically re-upload ONLY the per-cell night-light field (binding 4),
    // leaving the master/feature/zone textures untouched. Refreshes night
    // glow when the world state that drives it changes mid-session (settlements
    // loaded from a save, populations drifting across the daily economy tick)
    // without the cost of a full world re-upload. No-op until upload() has run
    // (the descriptor set must already be bound). Pass nullptr / 0,0 to reset to
    // the 1x1 black field.
    void upload_light_field(const gpu::VulkanDevice& dev,
                            const std::uint8_t* lightFieldRgba,
                            std::uint32_t lightFieldW, std::uint32_t lightFieldH);

    // Surgically re-upload ONLY the tree-count field (binding 4) — same
    // discipline as upload_light_field. Called when TreeLayer.revision moves
    // (felled trees / future woodcutters), never per frame.
    void upload_tree_field(const gpu::VulkanDevice& dev,
                           const TreeLayer* treeLayer);

    // Refresh ONLY the danger-zone field (binding 2), same surgical
    // discipline. Called by the one rebaker (rebake_world) when the zone
    // layer is rebaked — load, season, a landmark's death — never per frame.
    void upload_zone_field(const gpu::VulkanDevice& dev,
                           const ZoneLayer& zones);

    // Refresh ONLY the knowledge field (binding 5). Unlike the doors above
    // this one runs OFTEN — every player cell crossing bumps the revision —
    // so when the grid dims are unchanged it rewrites the R8 image IN PLACE
    // (VulkanTexture::update_region: queue-ordered barriers, its own fence,
    // no vkDeviceWaitIdle drain). The realloc path survives only for a
    // dimension change, which a full upload() covers anyway.
    void upload_knowledge_field(const gpu::VulkanDevice& dev,
                                const KnowledgeLayer* knowledge);

    // Refresh ONLY the nav debug field (binding 7), same surgical discipline.
    // ЧТО ЛЕЖИТ В БАЙТЕ — РЕШАЕТСЯ ЗДЕСЬ, НА CPU, и это не удобство, а
    // ЗАКОН ТУПИКА РЕНДЕРА: поток строго вниз, поэтому шейдер получает только
    // «категория это или скаляр» и не знает слов «округа» и «дистанция». Ноль
    // байта зарезервирован под «НЕТ ОКРУГИ» — ради него вид и существует.
    // `view == Off` (или отсутствие запечённой навигации) биндит 1×1 нуль,
    // поэтому биндинг 7 всегда валиден. Зовётся по НАЖАТИЮ клавиши и по
    // сдвигу запекания, никогда за кадр.
    void upload_nav_field(const gpu::VulkanDevice& dev, const NavWorld* nav,
                          NavDebugView view);

    // Record the fullscreen map draw for the current framebuffer.
    // `mapStyle` selects the CHART composition (the map page's document
    // rendering — flat atlas colours, inked coast/roads, fixed relief light,
    // no clock) instead of the living world; defaulted off so every
    // harness and the live view keep their pictures unchanged.
    void record(VkCommandBuffer cmd, VkExtent2D ext, const TerrainData& td,
                float camX, float camY, float zoom, float seaLevel,
                float timeOfDay, float elapsed, bool mapStyle = false,
                NavDebugView navView = NavDebugView::Off);

    bool ready() const { return uploaded_; }

private:
    void free_textures(const gpu::VulkanDevice& dev);

    gpu::VulkanTexture master_{}, feature_{}, zone_{};
    gpu::VulkanTexture lightField_{};  // per-cell RGB night glow (binding 3)
    gpu::VulkanTexture treeField_{};   // per-cell tree count R8 (binding 4)
    gpu::VulkanTexture knowledgeField_{}; // per-cell knowledge R8 (binding 5)
    // ПАЛИТРА БИОМОВ — ОДНА ТАБЛИЦА НА ИГРУ (binding 6, M-158). Здесь стояла
    // её рукописная копия в шейдере: десять строк совпадали с `kBiomes`, а
    // вода расходилась, и игрок видел ОБА ответа — превью мира рисует из
    // таблицы, карта рисовала из копии. `static_assert` через границу
    // GLSL/C++ не поставить, значит согласие копий не охраняемо по
    // построению — поэтому копии больше нет, а счёт строк шейдер берёт из
    // самих данных (`textureSize`), не из литерала.
    gpu::VulkanTexture biomePalette_{};   // kBiomes RGB, RGBA32F Nx1
    gpu::VulkanTexture navField_{};       // отладочный байт навигации R8 (binding 7)
    VkDescriptorSetLayout setLayout_ = VK_NULL_HANDLE;
    VkDescriptorPool pool_ = VK_NULL_HANDLE;
    VkDescriptorSet set_ = VK_NULL_HANDLE;
    gpu::VulkanPipeline pipeline_{};
    std::vector<std::uint8_t> scratch_;  // FeatureLayer sanitize scratch
    bool uploaded_ = false;
};

} // namespace sm
