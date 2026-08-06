#include "dual_screen.hpp"

#include <cstdlib>

namespace dusk {

bool is_dual_screen_active() noexcept {
    static const bool active = std::getenv("DUSKLIGHT_DUAL_SCREEN") != nullptr;
    return active;
}

static bool sDrawingLowerScreenContent = false;

bool is_drawing_lower_screen_content() noexcept {
    return sDrawingLowerScreenContent;
}

void set_drawing_lower_screen_content(bool value) noexcept {
    sDrawingLowerScreenContent = value;
}

}  // namespace dusk
