#pragma once

// Dual-screen (ROCKNIX Anbernic RG DS, Step 2) support glue between game code (src/d,
// src/m_Do) and Aurora's second-window/surface (see extern/aurora, patched via
// rocknix-dual-screen-second-window.patch). Kept as a tiny standalone header so game code
// doesn't need to pull in Aurora's WebGPU headers just to check whether dual-screen mode is on.

namespace dusk {

// True for the whole process lifetime if DUSKLIGHT_DUAL_SCREEN was set at startup (see
// platforms/rocknix/dusklight.sh and src/m_Do/m_Do_main.cpp, which sets
// AuroraConfig::enableSecondWindow from the same variable). Cheap to call every frame.
bool is_dual_screen_active() noexcept;

// Set (only) around the lower-screen offscreen pass in src/m_Do/m_Do_graphic.cpp
// (mDoGph_drawLowerScreen()), so display-list draw() overrides that got relocated wholesale --
// rather than given their own dedicated drawLowerScreen() method, the way dMeterMap_c was --
// can tell "am I being drawn for the lower screen right now" apart from "am I being drawn in my
// old top-screen spot, which dual screen suppresses" without needing two near-duplicate copies
// of a large draw() implementation. See dMeter2Draw_c::draw() (src/d/d_meter2_draw.cpp) for the
// first user: the whole item HUD (A/B/X/Y/Z button prompts, item quick-select icons) moves as one
// unit this way, since it was already a single self-contained draw() call.
bool is_drawing_lower_screen_content() noexcept;
void set_drawing_lower_screen_content(bool value) noexcept;

}  // namespace dusk
