#include "lower_screen_touch.hpp"

#include "dual_screen.hpp"
#include "logging.h"

#include "d/d_com_inf_game.h"
#include "d/d_drawlist.h"
#include "m_Do/m_Do_ext.h"

#include "JSystem/J2DGraph/J2DTextBox.h"

#include <SDL3/SDL.h>
#include <aurora/lib/window.hpp>

namespace dusk {

LowerScreenButtonTuning& get_lower_screen_button_tuning() noexcept {
    static LowerScreenButtonTuning tuning;
    return tuning;
}

namespace {

// 640x480 fixed panel resolution (see window::create_second_window()) -- unlike everything in
// LowerScreenButtonTuning, this isn't a tunable, it's the physical panel size.
constexpr float kLowerScreenWidth = 640.0f;
constexpr float kLowerScreenHeight = 480.0f;

enum class LowerScreenButton { None, Items, Map };

// Touch-pressed feedback: blend a color 50% toward white so a held button visibly lights up.
GXColor brighten(GXColor color) noexcept {
    color.r = static_cast<u8>(color.r + (255 - color.r) / 2);
    color.g = static_cast<u8>(color.g + (255 - color.g) / 2);
    color.b = static_cast<u8>(color.b + (255 - color.b) / 2);
    return color;
}

LowerScreenButton button_at(float x, float y) noexcept {
    const float barTop = kLowerScreenHeight - get_lower_screen_button_tuning().barHeight;
    if (y < barTop || y >= kLowerScreenHeight) {
        return LowerScreenButton::None;
    }
    if (x < 0.0f || x >= kLowerScreenWidth) {
        return LowerScreenButton::None;
    }
    return x < kLowerScreenWidth * 0.5f ? LowerScreenButton::Items : LowerScreenButton::Map;
}

// Tracks one finger at a time (only one tap is meaningful for these buttons; extra simultaneous
// fingers are ignored rather than tracked, since nothing here needs multi-touch yet).
SDL_FingerID sActiveFinger = 0;
bool sActiveFingerDown = false;
LowerScreenButton sActiveFingerStartButton = LowerScreenButton::None;

// Separate from sActiveFingerStartButton (which is the tap's *start* button, used to decide
// whether a release counts as a tap on that same button) -- this is "whichever button the finger
// is over right now, while held down", read by draw_lower_screen_buttons() to highlight it. Visual
// confirmation that a touch landed at all, since on_button_tapped()'s log line isn't visible
// on-device.
LowerScreenButton sCurrentlyPressedButton = LowerScreenButton::None;

void on_button_tapped(LowerScreenButton button) noexcept {
    // Stage 1: log only -- see docs/rocknix-porting.md Section E for the staged plan. Stage 2
    // wires Map to pausing + opening the big map on this screen; Stage 3 wires Items to the
    // drag-and-drop X/Y assignment screen.
    switch (button) {
    case LowerScreenButton::Items:
        DuskLog.info("Lower screen: Items button tapped");
        break;
    case LowerScreenButton::Map:
        DuskLog.info("Lower screen: Map button tapped");
        break;
    case LowerScreenButton::None:
        break;
    }
}

} // namespace

bool handle_lower_screen_touch_event(const SDL_Event& event) noexcept {
    if (!dusk::is_dual_screen_active()) {
        return false;
    }
    if (event.type != SDL_EVENT_FINGER_DOWN && event.type != SDL_EVENT_FINGER_MOTION &&
        event.type != SDL_EVENT_FINGER_UP && event.type != SDL_EVENT_FINGER_CANCELED)
    {
        return false;
    }

    SDL_Window* secondWindow = aurora::window::get_second_sdl_window();
    if (secondWindow == nullptr || event.tfinger.windowID != SDL_GetWindowID(secondWindow)) {
        return false;
    }

    // event.tfinger.x/y are normalized 0..1 within the window that received the touch (see
    // SDL_TouchFingerEvent::windowID). The second window is always created at a fixed 640x480
    // (window::create_second_window()), which is also this panel's logical size, so normalized
    // coordinates scale directly -- no need to query the actual window size.
    const float localX = event.tfinger.x * kLowerScreenWidth;
    const float localY = event.tfinger.y * kLowerScreenHeight;

    switch (event.type) {
    case SDL_EVENT_FINGER_DOWN: {
        if (sActiveFingerDown) {
            // Already tracking a finger; ignore additional ones for these buttons.
            return true;
        }
        sActiveFinger = event.tfinger.fingerID;
        sActiveFingerDown = true;
        sActiveFingerStartButton = button_at(localX, localY);
        sCurrentlyPressedButton = sActiveFingerStartButton;
        return true;
    }
    case SDL_EVENT_FINGER_MOTION: {
        if (!sActiveFingerDown || event.tfinger.fingerID != sActiveFinger) {
            return false;
        }
        // Only highlight while still over the button the touch started on -- sliding off cancels
        // the highlight even though the tap itself isn't resolved (released/counted) until FINGER_UP.
        const LowerScreenButton overButton = button_at(localX, localY);
        sCurrentlyPressedButton = (overButton == sActiveFingerStartButton) ? overButton : LowerScreenButton::None;
        return true;
    }
    case SDL_EVENT_FINGER_UP: {
        if (!sActiveFingerDown || event.tfinger.fingerID != sActiveFinger) {
            return false;
        }
        sActiveFingerDown = false;
        sCurrentlyPressedButton = LowerScreenButton::None;
        const LowerScreenButton endButton = button_at(localX, localY);
        if (endButton != LowerScreenButton::None && endButton == sActiveFingerStartButton) {
            on_button_tapped(endButton);
        }
        sActiveFingerStartButton = LowerScreenButton::None;
        return true;
    }
    case SDL_EVENT_FINGER_CANCELED: {
        if (event.tfinger.fingerID == sActiveFinger) {
            sActiveFingerDown = false;
            sActiveFingerStartButton = LowerScreenButton::None;
            sCurrentlyPressedButton = LowerScreenButton::None;
        }
        return true;
    }
    default:
        return false;
    }
}

namespace {

// left/boundWidth define the box HBIND_CENTER centers the text *within* -- left is the box's own
// left edge, not an already-centered point (passing the button's center here double-centers it,
// landing the text boundWidth/2 too far right; that was the "needs an X-offset" bug).
void draw_one_label(J2DGrafContext* graf, J2DTextBox* label, const char* text, float left, float labelY,
                    float boundWidth, float shadowOffset) {
    if (label == nullptr) {
        return;
    }
    // Re-establish 2D state before each label draw -- dDlst_2DQuad_c::draw() (used for the button
    // backgrounds) resets it via setup2D() as its own cleanup convention afterward, and this seems
    // to matter for J2DTextBox::draw() too (a second consecutive label silently failed to render
    // at all without this -- not mispositioned, just absent).
    graf->setPort();
    label->setString(text);
    label->setCharColor(0x000000FF);
    label->setGradColor(0x000000FF);
    label->draw(left + shadowOffset, labelY + shadowOffset, boundWidth, HBIND_CENTER);
    label->setCharColor(0xFFFFFFFF);
    label->setGradColor(0xFFFFFFFF);
    label->draw(left, labelY, boundWidth, HBIND_CENTER);
}

} // namespace

void draw_lower_screen_buttons() noexcept {
    if (!dusk::is_dual_screen_active()) {
        return;
    }
    const LowerScreenButtonTuning& tuning = get_lower_screen_button_tuning();

    // setPort() (not just setup2D()) resets the scissor rect too -- without it this can inherit
    // whatever scissor the previous draw call in this pass left behind (same bug as
    // dMeterMap_c::drawLowerScreen() hit first; see docs/rocknix-porting.md Section E).
    //
    // graf's mBounds/mScissorBounds are whatever the *shared* grafport instance was last placed
    // at by other (top-screen-oriented) code -- not necessarily this 640x480 offscreen pass's own
    // size -- and setScissor() clips to that bounds box regardless of vertex position, which is
    // why increasing bottomOvershoot had no visible effect ("bottom overshoot bringt gar nichts"):
    // the vertices were already past the bottom of the *scissor rect*, not just past 480. Widening
    // place() to the full pass before drawing, then restoring afterward so this doesn't leak into
    // whatever draws next (top screen next frame, or the item HUD's own pass right after this).
    J2DGrafContext* graf = dComIfGp_getCurrentGrafPort();
    const JGeometry::TBox2<f32> savedBounds = *graf->getBounds();
    graf->place(JGeometry::TBox2<f32>(0.0f, 0.0f, kLowerScreenWidth, kLowerScreenHeight));
    graf->setPort();

    const float barTop = kLowerScreenHeight - tuning.barHeight;
    const float buttonWidth = (kLowerScreenWidth - tuning.gap * 3.0f) * 0.5f;
    const float buttonTop = barTop + tuning.gap;
    // Deliberately past the panel's real bottom edge -- see LowerScreenButtonTuning::bottomOvershoot
    // in lower_screen_touch.hpp for why this needs to overshoot rather than land exactly on 480.
    const float buttonBottom = kLowerScreenHeight + tuning.bottomOvershoot;
    const float mapLeft = tuning.gap * 2.0f + buttonWidth;

    GXColor itemsColor{tuning.itemsColor.r, tuning.itemsColor.g, tuning.itemsColor.b, tuning.itemsColor.a};
    GXColor mapColor{tuning.mapColor.r, tuning.mapColor.g, tuning.mapColor.b, tuning.mapColor.a};
    // Touch feedback: brighten whichever button the finger is currently over (see
    // sCurrentlyPressedButton, maintained by handle_lower_screen_touch_event above) -- the only
    // visible confirmation on-device that a touch landed, since on_button_tapped()'s log line
    // isn't visible there.
    if (sCurrentlyPressedButton == LowerScreenButton::Items) {
        itemsColor = brighten(itemsColor);
    } else if (sCurrentlyPressedButton == LowerScreenButton::Map) {
        mapColor = brighten(mapColor);
    }

    dDlst_2DQuad_c quad;
    quad.init(static_cast<s16>(tuning.gap), static_cast<s16>(buttonTop), static_cast<s16>(tuning.gap + buttonWidth),
              static_cast<s16>(buttonBottom), itemsColor);
    quad.draw();

    quad.init(static_cast<s16>(mapLeft), static_cast<s16>(buttonTop), static_cast<s16>(mapLeft + buttonWidth),
              static_cast<s16>(buttonBottom), mapColor);
    quad.draw();

    // Labels. One lazily-created, reused J2DTextBox (never destroyed -- dual screen state, like
    // these buttons, lives for the whole process) rather than one instance per label: this draws
    // every frame during normal gameplay, so there's no create/delete lifecycle to hook into, and
    // two *concurrent* instances were the difference between the second label rendering or not.
    static J2DTextBox* sLabel = nullptr;
    if (sLabel == nullptr) {
        sLabel = JKR_NEW J2DTextBox();
    }
    if (sLabel != nullptr) {
        sLabel->setFontSize(tuning.fontSize, tuning.fontSize);
        sLabel->setFont(mDoExt_getMesgFont());

        // Centered against the intended visual bottom (kLowerScreenHeight), not buttonBottom's
        // deliberate overshoot -- otherwise the label would sit far below the visible button.
        const float labelY = buttonTop + (kLowerScreenHeight - buttonTop) * 0.5f + tuning.labelYOffset;
        draw_one_label(graf, sLabel, "Items", tuning.gap, labelY, buttonWidth, tuning.shadowOffset);
        draw_one_label(graf, sLabel, "Karte", mapLeft, labelY, buttonWidth, tuning.shadowOffset);
    }

    graf->place(savedBounds);
}

} // namespace dusk
