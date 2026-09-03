// Дневная физика полей следов (контракт в scent_field.h).
#include "macro/scent_field.h"

namespace sm {

void scent_field_daily(ScentField& sf, int day) {
    if (sf.w <= 0 || sf.h <= 0 || sf.factions <= 0) return;
    const int W = sf.w, H = sf.h;
    const std::size_t plane = std::size_t(W) * std::size_t(H);

    // ДИФФУЗИЯ: 1/8 клетки в день, поровну восьми соседям тора. Дельты
    // копятся в стороне и применяются разом — порядок обхода не влияет на
    // итог (поле без гонок с самим собой). Один буфер на все планы; нулевые
    // клетки перешагиваются, так что пустая фракция почти бесплатна.
    std::vector<std::int32_t> delta(plane);
    auto diffuse_plane = [&](std::uint16_t* cells) {
        std::int32_t* d = delta.data();
        for (std::size_t i = 0; i < plane; ++i) d[i] = 0;
        bool any = false;
        for (int y = 0; y < H; ++y) {
            const int yu = (y == 0 ? H - 1 : y - 1) * W;
            const int yc = y * W;
            const int yd = (y == H - 1 ? 0 : y + 1) * W;
            for (int x = 0; x < W; ++x) {
                const std::uint16_t t = cells[yc + x];
                if (t == 0u) continue;
                const std::uint32_t per =
                    (std::uint32_t(t) >> kScentDiffusionShift) >> 3;
                if (per == 0u) continue;
                any = true;
                const int xl = x == 0 ? W - 1 : x - 1;
                const int xr = x == W - 1 ? 0 : x + 1;
                d[yu + xl] += std::int32_t(per);
                d[yu + x]  += std::int32_t(per);
                d[yu + xr] += std::int32_t(per);
                d[yc + xl] += std::int32_t(per);
                d[yc + xr] += std::int32_t(per);
                d[yd + xl] += std::int32_t(per);
                d[yd + x]  += std::int32_t(per);
                d[yd + xr] += std::int32_t(per);
                d[yc + x]  -= std::int32_t(per) * 8;
            }
        }
        if (!any) return;
        for (std::size_t i = 0; i < plane; ++i) {
            if (d[i] == 0) continue;
            const std::int32_t v = std::int32_t(cells[i]) + d[i];
            cells[i] = v <= 0 ? 0u
                     : v > 0xFFFF ? std::uint16_t(0xFFFFu)
                                  : std::uint16_t(v);
        }
    };
    for (int f = 0; f < sf.factions; ++f) {
        diffuse_plane(sf.strength.data() + std::size_t(f) * plane);
        diffuse_plane(sf.wealth.data() + std::size_t(f) * plane);
    }

    // РАСПАД: по календарю мира, не по фазе загрузки.
    if (kScentDecayDays > 0 && day > 0 && day % kScentDecayDays == 0) {
        for (std::uint16_t& c : sf.strength) c >>= 1;
        for (std::uint16_t& c : sf.wealth) c >>= 1;
    }
}

} // namespace sm
