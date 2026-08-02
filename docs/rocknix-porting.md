# Porting Dusklight to ROCKNIX (Anbernic RG DS)

Status: Step 1 only — a native, standalone port running on the RG DS. Dual-screen support
(second display, streaming, alternative layouts) is out of scope; do not start on it here.

This documents the cross-toolchain setup, cross-build, graphics-backend verification, and
packaging needed to run Dusklight on ROCKNIX's RK3566 target (covers the RG DS; RK3568 dtb
`rk3568-anbernic-rg-ds.dtb` lives under the same RK3566 device tree in ROCKNIX, no per-model
override). No game logic (`src/d`, `src/f_*`, `libs/`) is touched — build/platform/packaging only.

## What's already true, and what this doc corrects

The game code (`src/`, `include/`, `libs/`) is arch-portable and already runs on ARM64 (Android
target) — not a blocker. Aurora's Dawn (WebGPU) has a prebuilt `linux-aarch64` package
(`AURORA_DAWN_PROVIDER=package`, `extern/aurora/cmake/AuroraDawnProvider.cmake`), built with
`ENABLE_VULKAN`/`ENABLE_DESKTOP_GL`/`ENABLE_OPENGLES` all on — both Vulkan and GLES backends are
compiled in without a custom Dawn vendor build. Aurora's auto-fallback order is
WebGPU → Vulkan → OpenGLES → Null (`extern/aurora/lib/aurora.cpp`, `PreferredBackendOrder`);
Desktop GL is compiled in but deliberately commented out of that list.

Two assumptions from the original brief needed verifying against the actual ROCKNIX source
(`https://github.com/ROCKNIX/distribution`), and turned out to be off:

- **Display server**: the brief assumed a bare KMSDRM boot ("no X11/Wayland desktop, boot
  straight into the game"). `projects/ROCKNIX/devices/RK3566/options` in the ROCKNIX repo sets
  `DISPLAYSERVER="wl"` and `WINDOWMANAGER="swaywm-env"` — RK3566 (including the RG DS; there's no
  device-specific override) actually runs a sway/Wayland compositor session.
  `projects/ROCKNIX/packages/wayland/compositor/sway/profile.d/050-sway.conf` exports
  `SDL_VIDEODRIVER=wayland` system-wide for every session (comment in that file: "this lets ppsspp
  work with wayland and not segfault"). So SDL3 should target **wayland**, not kmsdrm, here. The
  good news: `CMakeLists.txt:68` already sets `DAWN_USE_WAYLAND ON` for any `CMAKE_SYSTEM_NAME
  STREQUAL Linux` build, so this needs no code change — just don't override
  `SDL_VIDEODRIVER`/`WAYLAND_DISPLAY` in the port launcher and let ROCKNIX's own profile.d win
  (see `platforms/rocknix/dusklight.sh`).
- **Graphics driver**: confirmed directly rather than inferred from release notes.
  `projects/ROCKNIX/devices/RK3566/options` has `GRAPHIC_DRIVERS="mali panfrost"`,
  `ADDITIONAL_PACKAGES="libmali"` (and the 32-bit variant), `VULKAN_SUPPORT="yes"`,
  `VULKAN="vulkan-loader"`, `PREFER_GLES="yes"`. Both the libmali blob and Panfrost are built in,
  libmali is installed by default and is almost certainly what backs the Vulkan loader (PanVK is
  reported non-conformant on this Bifrost GPU elsewhere). Still, which backend is actually stable
  for Dusklight/Dawn on this device is a test result — see "Graphics backend" below.

## A. Environment & toolchain

```sh
git clone --recursive https://github.com/TwilitRealm/dusklight.git
# or, if you're working from this fork:
git submodule update --init --recursive
```

The aarch64 cross toolchain must come from ROCKNIX's own build, matched to the RK3566 image's
glibc/kernel headers — not a generic distro aarch64-linux-gnu toolchain. Build it on a machine
with full network access (this could not be done from the sandboxed session that wrote this doc —
see "Sandbox constraints" below):

```sh
git clone https://github.com/ROCKNIX/distribution.git
cd distribution
make docker-shell
# inside the container:
PROJECT=ROCKNIX DEVICE=RK3566 ARCH=aarch64 ./scripts/build_mt toolchain
```

This is the same target ROCKNIX's own CI uses to produce just the toolchain
(`.github/workflows/build-aarch64-toolchain.yml`: `./scripts/build_mt toolchain alsa-lib
llvm:host` — `alsa-lib`/`llvm:host` are extra deps for other CI jobs, not required just to get a
working cross compiler). It builds binutils/gcc/glibc/kernel-headers for the target from source;
expect it to take a while, and expect it to need real disk space — it is not the same as a full
system image build (`make RK3566` / `make docker-RK3566`), which also builds the kernel and every
target package and takes much longer.

Once done, from the `distribution` checkout:

```
build.ROCKNIX-RK3566.aarch64/toolchain/bin/aarch64-rocknix-linux-gnu-{gcc,g++,ar,ranlib,strip,...}
build.ROCKNIX-RK3566.aarch64/toolchain/aarch64-rocknix-linux-gnu/sysroot/
```

(target triple derived from `config/path`: `TARGET_NAME=$TARGET_GCC_ARCH-rocknix-linux-gnu$TARGET_ABI`,
`TARGET_GCC_ARCH=aarch64` for this device, empty ABI suffix.)

`cmake/toolchain-rocknix-rk3566.cmake` in this repo consumes that directory directly:

```sh
cmake --preset linux-default-relwithdebinfo \
  -DCMAKE_TOOLCHAIN_FILE=cmake/toolchain-rocknix-rk3566.cmake \
  -DROCKNIX_TOOLCHAIN_ROOT=/path/to/distribution/build.ROCKNIX-RK3566.aarch64/toolchain \
  -DAURORA_DAWN_PROVIDER=package
```

`ROCKNIX_TOOLCHAIN_ROOT` can also be set as an environment variable instead of `-D`. The toolchain
file sets `CMAKE_SYSROOT`/`CMAKE_FIND_ROOT_PATH` to the sysroot under that directory, matching how
ROCKNIX's own build generates its internal `cmake-$TARGET_NAME.conf` (`config/functions`,
`setup_toolchain`), and applies the RK3566 Cortex-A55 `-mcpu` flags from
`projects/ROCKNIX/devices/RK3566/options`.

## B. Cross-build

```sh
cmake --preset linux-default-relwithdebinfo \
  -DCMAKE_TOOLCHAIN_FILE=cmake/toolchain-rocknix-rk3566.cmake \
  -DROCKNIX_TOOLCHAIN_ROOT=/path/to/.../toolchain \
  -DAURORA_DAWN_PROVIDER=package
cmake --build --preset linux-default-relwithdebinfo
```

`BUILD_SHARED_LIBS=OFF` is already the `linux-default` preset default — keep it, fewer runtime
deps to bundle on-device. `AURORA_DAWN_PROVIDER=package` pulls the prebuilt `linux-aarch64` Dawn
package instead of vendoring a Dawn build (see `extern/aurora/cmake/AuroraDawnProvider.cmake`);
this was confirmed reachable (`github.com/encounter/dawn/releases/download/...`) even from the
network-restricted sandbox that wrote this doc.

Expect cross-compile-specific failures the first time through: headers/libs in
`docs/building.md`'s Ubuntu/Arch package lists are host packages, and won't exist for aarch64 on a
plain x86_64 host. `CMAKE_FIND_ROOT_PATH_MODE_{LIBRARY,INCLUDE,PACKAGE}=ONLY` in the toolchain file
means CMake will only look in the ROCKNIX sysroot for these, not fall back to /usr on the host —
so any missing dependency needs to actually exist in the sysroot ROCKNIX's toolchain build
produced (ROCKNIX's package build system installs a matching sysroot copy for every target package
it builds; anything Dusklight needs beyond what the base toolchain step already provides may need
building on the ROCKNIX side too, e.g. `PROJECT=ROCKNIX DEVICE=RK3566 ARCH=aarch64 ./scripts/build_mt
<pkg>`). Fix these as they come up rather than assuming the package list in `docs/building.md`
maps 1:1.

### Sandbox constraints (why this wasn't run end-to-end here)

The session that wrote this doc could not pull `ghcr.io/rocknix/rocknix-build` — its blob storage
host (`pkg-containers.githubusercontent.com`) is blocked by that sandbox's egress policy (403,
confirmed via the environment's own proxy diagnostics, which explicitly say to report rather than
route around such blocks). `CMakeLists.txt` itself has no ROCKNIX/Linux-variant-specific logic
that would obviously break (checked: no unconditional X11 requirement, no non-generic-Linux
assumptions beyond the Wayland-is-already-on point above).

A generic (non-ROCKNIX) `aarch64-linux-gnu` toolchain was used for a one-off smoke test of the
*CMake plumbing only* — never as a stand-in for the real target, per the brief's explicit
requirement for a glibc/kernel-header-matched toolchain. It's useful for what it found, not as
build verification:

- The toolchain-file mechanics themselves are sound: cross-compiler detection, `-mcpu` flags, and
  `AURORA_DAWN_PROVIDER=package` all worked and correctly triggered the prebuilt `linux-aarch64`
  Dawn package fetch.
- Ubuntu's `gcc-*-aarch64-linux-gnu` cross packages and its `:arm64` multiarch dev packages
  (`libasound2-dev:arm64` etc., needed to even approximate `docs/building.md`'s dependency list)
  use two different, non-overlapping sysroot layouts (`/usr/aarch64-linux-gnu/` for the cross
  packages vs. plain multiarch paths under `/usr/lib/aarch64-linux-gnu/` for the `:arm64`
  packages) — `CMAKE_FIND_ROOT_PATH` can only point at one. This is exactly the class of problem a
  single self-contained ROCKNIX-built sysroot avoids, and is itself a reason not to substitute a
  generic toolchain even for local iteration.
- Also hit a second blocked GitHub host distinct from the `ghcr.io` one: `codeload.github.com`
  (`https://github.com/.../archive/refs/tags/...tar.gz`, used by several `FetchContent` deps
  including abseil-cpp — note release-asset downloads via `objects.githubusercontent.com`, e.g.
  the Dawn package itself, were reachable, just not archive-tarball downloads). Confirmed via
  direct `curl`, not routed around, same as the `ghcr.io` block. This is a sandbox-only
  restriction; a normal developer machine won't hit it.

Net: the cross-build is expected to mainly hit the "missing sysroot package" class of error
described above — but that's an expectation, not a verified fact for the *real* toolchain. Run
section B yourself, with the real ROCKNIX-built toolchain, and fix forward from there.

## C. Graphics backend — empirical, not assumed

1. Copy the `auto`-configured binary to the RG DS (default `backend.graphicsBackend` in
   `config.json`) and run it with logging. Aurora logs the resolved backend/adapter at
   `extern/aurora/lib/webgpu/gpu.cpp:844-846` (`g_backendType`, `magic_enum::enum_name`,
   adapter name, driver) via `Log.info("Graphics adapter information...")`.
2. If `auto` (typically resolves to Vulkan, given `PreferredBackendOrder`) runs clean and stable:
   done, nothing further needed.
3. If Vulkan is unstable (crash, visual corruption, etc.) or the fallback doesn't kick in cleanly,
   force OpenGL ES for one run without touching `config.json`:

   ```sh
   dusklight --dvd <path> --backend opengles
   ```

   or, more generally, override any config var for a single run:

   ```sh
   dusklight --dvd <path> --cvar backend.graphicsBackend=opengles
   ```

   (both flags come from `src/m_Do/m_Do_main.cpp` — `--backend` is validated against
   `try_parse_backend` in `src/dusk/ui/settings.cpp:112` and falls back to `auto` with a warning if
   unavailable/unrecognized; `--cvar name=value` goes through
   `dusk::config::load_arg_override`/`ConfigVarLayer::Override`, i.e. it wins over the saved
   config for that run only and is never written back).

   To persist the choice, edit `backend.graphicsBackend` in `config.json` to `"opengles"` (valid
   values include `"auto"`, `"vulkan"`, `"opengles"` — full list in `try_parse_backend`).
   `config.json` lives at `dusk::ConfigPath / "config.json"`; `ConfigPath` is set from
   `AuroraInfo::userPath`, which defaults to SDL3's `SDL_GetPrefPath(nullptr, "Dusklight")`
   (`src/dusk/app_info.hpp:9` for the app name, `src/m_Do/m_Do_main.cpp:576`) — on Linux this is
   under `$XDG_DATA_HOME` (falls back to `~/.local/share`) unless ROCKNIX's environment overrides
   `HOME`/`XDG_DATA_HOME` for the storage user; confirm the real path by checking the log line
   Aurora prints for `userPath` on first run on the device, don't assume it.
4. Once step 2 or 3 has actually been run on hardware, record which backend it was here — a
   result, not a guess:

   > _(unfilled — needs a run on real RG DS hardware, which this porting pass didn't have access
   > to; fill in backend, adapter/driver string from the Aurora log line above, and any caveats.)_

## D. Packaging as a ROCKNIX port

See [`platforms/rocknix/README.md`](../platforms/rocknix/README.md) and
`platforms/rocknix/dusklight.sh` for the actual launcher and deployment layout (`roms/ports`
convention: `SYSTEM_PATH="/storage/roms/ports"`, `SYSTEM_EXTENSION=".sh .appimage"`, per
`config/emulators/ports.conf` and `runemu.sh` in `ROCKNIX/distribution`). No Buildroot system
package, no image rebuild — a self-contained binary + launcher script dropped into `roms/ports`,
same as any other port on the device. The launcher deliberately does not set
`SDL_VIDEODRIVER`/`WAYLAND_DISPLAY` — see the "Display server" correction above; verify on-device
whether ROCKNIX's global profile.d config is sufficient or needs an explicit override, per the
brief's own instruction not to assume interconnects it hasn't verified. The GameCube dump path is
user-provided and configurable (`game/` subfolder or `DUSKLIGHT_DVD_PATH`), never hardcoded or
bundled.
