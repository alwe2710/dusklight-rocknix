#include "ImGuiLowerScreenButtonsWindow.hpp"

#include "imgui.h"

#include "ImGuiMenuTools.hpp"
#include "dusk/lower_screen_touch.hpp"

namespace dusk {
namespace {

void DrawColorEdit(const char* label, LowerScreenButtonColor& color) {
    float colorValue[4] = {
        color.r / 255.0f,
        color.g / 255.0f,
        color.b / 255.0f,
        color.a / 255.0f,
    };
    if (ImGui::ColorEdit4(label, colorValue, ImGuiColorEditFlags_Uint8)) {
        color.r = static_cast<unsigned char>(colorValue[0] * 255.0f + 0.5f);
        color.g = static_cast<unsigned char>(colorValue[1] * 255.0f + 0.5f);
        color.b = static_cast<unsigned char>(colorValue[2] * 255.0f + 0.5f);
        color.a = static_cast<unsigned char>(colorValue[3] * 255.0f + 0.5f);
    }
}

} // namespace

// Lets the Items/Map button bar on the lower screen (dusk::draw_lower_screen_buttons, see
// src/dusk/lower_screen_touch.cpp) be tuned live on a running build instead of a
// rebuild-redeploy-relaunch round trip per tweak, since that bar's exact position/size/colors were
// all guesswork the first few passes -- see docs/rocknix-porting.md Section E.
void DrawLowerScreenButtonsWindow(bool& open) {
    if (!open) {
        return;
    }
    if (!ImGui::Begin("Lower Screen Buttons", &open)) {
        ImGui::End();
        return;
    }

    LowerScreenButtonTuning& tuning = get_lower_screen_button_tuning();

    ImGui::SeparatorText("Layout (640x480 logical panel space)");
    ImGui::SliderFloat("Bar Height", &tuning.barHeight, 20.0f, 300.0f);
    ImGui::SliderFloat("Gap", &tuning.gap, 0.0f, 20.0f);
    ImGui::SliderFloat("Bottom Overshoot", &tuning.bottomOvershoot, 0.0f, 1000.0f);

    ImGui::SeparatorText("Label");
    ImGui::SliderFloat("Font Size", &tuning.fontSize, 8.0f, 64.0f);
    ImGui::SliderFloat("Label Y Offset", &tuning.labelYOffset, -100.0f, 100.0f);
    ImGui::SliderFloat("Shadow Offset", &tuning.shadowOffset, 0.0f, 5.0f);

    ImGui::SeparatorText("Colors");
    DrawColorEdit("Items Color", tuning.itemsColor);
    DrawColorEdit("Map Color", tuning.mapColor);

    if (ImGui::Button("Reset to defaults")) {
        tuning = LowerScreenButtonTuning{};
    }

    ImGui::End();
}

void ImGuiMenuTools::ShowLowerScreenButtonsWindow() {
    DrawLowerScreenButtonsWindow(m_showLowerScreenButtonsWindow);
}

} // namespace dusk
