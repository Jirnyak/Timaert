#include "ui/ui_theme.h"
#include "ui/ui_image.h"

namespace sm::ui {

void draw_title_backdrop() {
    const ImVec2 vp = viewport_size();
    auto* dl = ImGui::GetBackgroundDrawList();
    const UiImage* bg = ui_image_for("assets/backgrounds/0.png");
    if (!bg || vp.x <= 0.0f || vp.y <= 0.0f) {
        dl->AddRectFilled(ImVec2(0, 0), vp, IM_COL32(0, 0, 0, 217));
        return;
    }
    const float imgAspect = float(bg->w) / float(bg->h);
    const float vpAspect  = vp.x / vp.y;
    ImVec2 uv0(0.0f, 0.0f), uv1(1.0f, 1.0f);
    if (imgAspect > vpAspect) {       // image wider than screen: crop sides
        const float w = vpAspect / imgAspect;
        uv0.x = (1.0f - w) * 0.5f;
        uv1.x = uv0.x + w;
    } else {                          // taller: crop top/bottom
        const float h = imgAspect / vpAspect;
        uv0.y = (1.0f - h) * 0.5f;
        uv1.y = uv0.y + h;
    }
    dl->AddImage(bg->tex, ImVec2(0, 0), vp, uv0, uv1);
    // Warm dusk over the paint so the panels read; a touch heavier toward the
    // bottom, where the menu itself sits.
    dl->AddRectFilled(ImVec2(0, 0), vp, IM_COL32(16, 13, 8, 72));
    dl->AddRectFilledMultiColor(ImVec2(0, vp.y * 0.55f), vp,
                                IM_COL32(16, 13, 8, 0),  IM_COL32(16, 13, 8, 0),
                                IM_COL32(16, 13, 8, 96), IM_COL32(16, 13, 8, 96));
}

} // namespace sm::ui
