#pragma once

union SDL_Event;

// Dual screen (ROCKNIX Anbernic RG DS, Step 2 continued): touch handling for the two wide buttons
// planned along the bottom edge of the lower screen (Items / Map). Stage 1: routing + hit-testing
// only, logged rather than drawn -- see docs/rocknix-porting.md Section E for the staged plan and
// the on-device touch-hardware findings this is built on (two independent Goodix digitizers, one
// per physical panel, both pre-calibrated by ROCKNIX to their own half of the compositor's virtual
// desktop; SDL3's SDL_TouchFingerEvent::windowID scopes each touch to the right Aurora window
// without extra plumbing).
namespace dusk {

// Feed every SDL event here (see src/m_Do/m_Do_main.cpp's AURORA_SDL_EVENT case). No-ops unless
// dual screen is active and the event is a finger event for the second window. Returns true if
// the event was consumed (a tap landed on a button) so callers can skip other handling.
bool handle_lower_screen_touch_event(const SDL_Event& event) noexcept;

// Draws the two buttons themselves. Call from within the same offscreen pass/coordinate space
// dMeterMap_c::drawLowerScreen() uses (see mDoGph_drawLowerScreen in src/m_Do/m_Do_graphic.cpp) --
// the button rects below are defined in that same 640x480 logical space, matching what
// handle_lower_screen_touch_event() hit-tests against. No-ops if dual screen isn't active.
void draw_lower_screen_buttons() noexcept;

// Plain (no GX types, so this header stays cheap to include) live-tunable knobs for the button
// bar's layout/art, backing the "Lower Screen Buttons" ImGui debug window (see
// src/dusk/imgui/ImGuiLowerScreenButtonsWindow.cpp) -- lets position/size/color be adjusted on a
// running build instead of a rebuild-redeploy round trip per tweak. handle_lower_screen_touch_event
// and draw_lower_screen_buttons both read the same live instance, so hit-testing can't drift from
// what's drawn.
struct LowerScreenButtonColor {
    unsigned char r, g, b, a;
};
struct LowerScreenButtonTuning {
    float barHeight = 100.0f;
    float gap = 6.0f;
    // No longer needed now that draw_lower_screen_buttons() widens the graf port's scissor bounds
    // to the full 640x480 pass itself (see that function) -- the real fix for the bar not reaching
    // the bottom edge turned out to be the scissor rect, not vertex position, so overshooting the
    // bottom vertex past 480 had no effect ("bottom overshoot bringt gar nichts"). Kept at 0 and
    // still wired up in case some other clipping edge case turns up.
    float bottomOvershoot = 0.0f;
    float fontSize = 30.0f;
    float labelYOffset = -2.0f;
    float shadowOffset = 1.0f;
    LowerScreenButtonColor itemsColor{60, 90, 150, 200};
    LowerScreenButtonColor mapColor{70, 130, 90, 200};
};
LowerScreenButtonTuning& get_lower_screen_button_tuning() noexcept;

} // namespace dusk
