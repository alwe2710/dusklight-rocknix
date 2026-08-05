#!/bin/bash
# Dusklight launcher for ROCKNIX (Anbernic RG DS / RK3566-RK3568).
#
# Deployment layout on-device (ROCKNIX "ports" convention, see
# config/emulators/ports.conf and runemu.sh in ROCKNIX/distribution):
#
#   /storage/roms/ports/Dusklight.sh   <- copy of this file (must sit directly under roms/ports)
#   /storage/roms/ports/dusklight/     <- everything below
#     dusklight                        <- cross-built binary (platform/CMakeLists RPATH=$ORIGIN,
#                                          so bundle any non-statically-linked shared libs here too)
#     res/                             <- REQUIRED, not optional: fonts/icons/RmlUI stylesheets the
#                                          binary loads relative to itself at startup (SDL_GetBasePath()
#                                          / CWD-relative "res/..."). Missing this crashes on the very
#                                          first frame — window flashes black, straight back to the
#                                          ROCKNIX menu, no visible error. Copy build/<preset>/res
#                                          verbatim; it's a CMake POST_BUILD step, easy to miss if you
#                                          hand-assemble the deploy dir instead of copying the whole
#                                          build output next to the binary.
#     game/                            <- put your own GameCube dump here (ISO/RVZ/WIA/WBFS/CISO/GCZ);
#                                          NOT provided by this repo, see docs/building.md "Running"
#
# The game dump path is intentionally not hardcoded: drop exactly one supported image in
# dusklight/game/, or set DUSKLIGHT_DVD_PATH to point elsewhere.
#
# Graphics backend: ROCKNIX's RK3566 target boots into a sway/Wayland compositor
# (projects/ROCKNIX/devices/RK3566/options: DISPLAYSERVER="wl", WINDOWMANAGER="swaywm-env") and
# already exports SDL_VIDEODRIVER=wayland system-wide via
# projects/ROCKNIX/packages/wayland/compositor/sway/profile.d/050-sway.conf. This is NOT a bare
# KMSDRM console session, so we deliberately do not override SDL_VIDEODRIVER/WAYLAND_DISPLAY here
# and just inherit ROCKNIX's own environment, same as other ports/emulators on this device.
#
# Dusklight/Aurora backend selection defaults to "auto" (WebGPU -> Vulkan -> OpenGLES -> Null,
# Desktop GL is compiled in but intentionally excluded from auto-fallback). RK3566 ships both the
# libmali blob and Panfrost/PanVK for the Mali-G52 (Bifrost); which one is stable is a per-device
# empirical result, not an assumption (see docs/rocknix-porting.md section "Graphics backend").
# Override for one run without touching config.json:
#   DUSKLIGHT_BACKEND=opengles roms/ports/Dusklight.sh
# or persist the choice by editing backend.graphicsBackend in config.json (see docs).
#
# Dual screen (Step 2, in progress): the RG DS exposes its two DSI panels as independent Wayland
# outputs -- confirmed on-device via `swaymsg -t get_outputs`: DSI-2 (0,0, focused, powered on by
# default -- the top/primary screen) and DSI-1 (640,0, powered off by default -- the bottom/lower
# screen; sway's reported X/Y here is just its virtual output layout, not the panels' physical
# top/bottom relationship, which this assumes based on which one is on/focused by default).
# Dusklight creates a second window titled "Dusklight Lower Screen" when DUSKLIGHT_DUAL_SCREEN is
# set.
#
# Getting a second *window* to actually show up on DSI-1 needs three things sway has to do, not
# just one -- worked out empirically on-device (see docs/rocknix-porting.md), and the same pattern
# ROCKNIX's own drastic-sa uses (`for_window [app_id="drastic"] output DSI-1 power on, ...` in
# /storage/.config/sway/config, which ROCKNIX regenerates on every emulator/port launch -- our own
# appended rule does NOT survive that and must be re-added on every run, not just once):
#   1. `output DSI-1 power on` -- DSI-1 is powered off by default (confirmed via
#      /sys/class/backlight/*/bl_power and DRM connector state showing crtc=(null) until powered
#      on); without this the window is composited correctly but the panel backlight/CRTC pipeline
#      is simply off, so it stays black regardless of what's rendered.
#   2. `move position 640 0` -- Wayland clients cannot request their own absolute window position
#      (unlike X11/Win32); only the compositor can place a window at DSI-1's virtual coordinates
#      (640,0 in sway's output layout, confirmed via `swaymsg -t get_outputs`). `move to output
#      DSI-1` alone (an earlier attempt) was not sufficient -- it reassigns which output/workspace
#      sway *considers* the window to belong to, but didn't actually get it to render there.
#   3. `border none` -- without this the floating window's title bar/border eats into the 640x480
#      content area, cutting a visible notch out of the adjacent primary screen's edge.
# Set DUSKLIGHT_DUAL_SCREEN=0 to disable and run single-screen only.

set -euo pipefail

PORT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/dusklight" && pwd)"
BIN="${PORT_DIR}/dusklight"
LOG_DIR="${PORT_DIR}/logs"
LOG_FILE="${LOG_DIR}/dusklight-$(date +%Y%m%d-%H%M%S).log"

mkdir -p "${LOG_DIR}"

if [ ! -x "${BIN}" ]; then
    echo "dusklight binary not found or not executable: ${BIN}" >&2
    exit 1
fi

# Resolve the DVD dump: explicit override wins, otherwise the single file under game/.
if [ -n "${DUSKLIGHT_DVD_PATH:-}" ]; then
    DVD_PATH="${DUSKLIGHT_DVD_PATH}"
else
    GAME_DIR="${PORT_DIR}/game"
    mkdir -p "${GAME_DIR}"
    DVD_PATH=""
    shopt -s nullglob nocaseglob
    for candidate in "${GAME_DIR}"/*.iso "${GAME_DIR}"/*.rvz "${GAME_DIR}"/*.wia "${GAME_DIR}"/*.wbfs "${GAME_DIR}"/*.ciso "${GAME_DIR}"/*.gcz; do
        DVD_PATH="${candidate}"
        break
    done
    shopt -u nullglob nocaseglob
fi

if [ -z "${DVD_PATH}" ] || [ ! -f "${DVD_PATH}" ]; then
    echo "No game dump found. Place a GameCube dump (ISO/RVZ/WIA/WBFS/CISO/GCZ) in" >&2
    echo "  ${PORT_DIR}/game/" >&2
    echo "or set DUSKLIGHT_DVD_PATH to its full path." >&2
    exit 1
fi

ARGS=(--dvd "${DVD_PATH}")

if [ -n "${DUSKLIGHT_BACKEND:-}" ]; then
    ARGS+=(--backend "${DUSKLIGHT_BACKEND}")
fi

DUSKLIGHT_DUAL_SCREEN="${DUSKLIGHT_DUAL_SCREEN:-1}"
export DUSKLIGHT_DUAL_SCREEN
if [ "${DUSKLIGHT_DUAL_SCREEN}" != "0" ] && command -v swaymsg >/dev/null 2>&1; then
    SWAY_CONFIG="/storage/.config/sway/config"
    SWAY_RULE='for_window [title="Dusklight Lower Screen"] output DSI-1 power on, border none, move position 640 0, resize set width 640px height 480px'
    # ROCKNIX regenerates this config on emulator/port launches, so our appended rule does not
    # reliably survive between runs -- always ensure it's present rather than checking once.
    if [ -f "${SWAY_CONFIG}" ] && ! grep -qF "${SWAY_RULE}" "${SWAY_CONFIG}" 2>/dev/null; then
        printf '\n# Added by Dusklight (dual-screen HUD/minimap on lower panel)\n%s\n' "${SWAY_RULE}" >> "${SWAY_CONFIG}"
        swaymsg reload >/dev/null 2>&1 || true
    fi
fi

cd "${PORT_DIR}"
exec "${BIN}" "${ARGS[@]}" &>>"${LOG_FILE}"
