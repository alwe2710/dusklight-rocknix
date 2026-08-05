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

cd "${PORT_DIR}"
exec "${BIN}" "${ARGS[@]}" &>>"${LOG_FILE}"
