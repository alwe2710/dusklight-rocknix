# Porting Dusklight to ROCKNIX (Anbernic RG DS)

Status: Step 1 only — a native, standalone port running on the RG DS. Dual-screen support
(second display, streaming, alternative layouts) is out of scope; do not start on it here.

Toolchain build and cross-build (Sections A/B) are done — a real `dusklight` aarch64 binary has
been produced with the actual ROCKNIX-built toolchain, packaged per Section D below. Graphics
backend (Section C) is still unverified: it needs an actual run on RG DS hardware, which this pass
didn't have access to.

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

**Status: done — built end-to-end with a real ROCKNIX-produced toolchain (not the generic-toolchain
smoke test below) and linked successfully.** The steps and flags below are what that build actually
needed, not a prediction.

```sh
cmake --preset linux-default-relwithdebinfo \
  -DCMAKE_TOOLCHAIN_FILE=cmake/toolchain-rocknix-rk3566.cmake \
  -DROCKNIX_TOOLCHAIN_ROOT=/path/to/.../toolchain \
  -DAURORA_DAWN_PROVIDER=package \
  -DSDL_X11=OFF -DSDL_WAYLAND=ON \
  -DHAVE_GETRESGID=1 -DHAVE_GETRESUID=1 \
  -DRust_CARGO_TARGET=aarch64-unknown-linux-gnu
cmake --build --preset linux-default-relwithdebinfo
```

`ROCKNIX_TOOLCHAIN_ROOT` must be exported as an environment variable (not just passed as `-D`) for
the **build** step, not only configure: `libjpeg-turbo-ext` is pulled in via `ExternalProject_Add`
and spawns its own nested `cmake` configure at *build* time, which re-reads
`cmake/toolchain-rocknix-rk3566.cmake` from scratch and only sees the toolchain root if it's in the
process environment. A `-D` on the outer configure invocation does not reach it.

`BUILD_SHARED_LIBS=OFF` is already the `linux-default` preset default. `AURORA_DAWN_PROVIDER=package`
pulls the prebuilt `linux-aarch64` Dawn package instead of vendoring a Dawn build (see
`extern/aurora/cmake/AuroraDawnProvider.cmake`) — confirmed working end-to-end.

The three extra `-D` flags above are all real fixes for real failures hit during this build, each
confirmed necessary (not cargo-culted):

- **`-DSDL_X11=OFF -DSDL_WAYLAND=ON`**: without target packages for Wayland, SDL3's configure
  aborts with "SDL could not find X11 or Wayland development libraries" (build `wayland`,
  `wayland-protocols`, `libxkbcommon`, and `libglvnd` — see below — to get past this). Once those
  packages exist in the sysroot, SDL3 auto-detects **both** Wayland and X11 (`libglvnd` pulls in
  `libX11` as a transitive dependency, which is enough for SDL's X11 check to pass) and then fails
  again demanding X11-only packages (`libXcursor` etc.) that ROCKNIX/RK3566 doesn't ship and that
  we don't want, since the device boots into sway/Wayland, not X11. Forcing `SDL_X11=OFF` avoids
  needing those X11-extension packages at all, consistent with the "Display server" correction
  above.
- **`-DHAVE_GETRESGID=1 -DHAVE_GETRESUID=1`**: SDL3's `CMakeLists.txt` appends `-D_GNU_SOURCE=1` to
  `CMAKE_REQUIRED_FLAGS` near the top of the file (for exactly this kind of glibc feature-macro
  issue), but that flag isn't present anymore by the time `check_symbol_exists(getresgid ...)` runs
  ~1000 lines later (confirmed via `CMakeConfigureLog.yaml`: the actual failed check command has no
  `_GNU_SOURCE` in it) — an SDL3 CMake bug, not a ROCKNIX/sysroot problem. Both functions are
  genuinely declared in the ROCKNIX glibc's `unistd.h` under `__USE_GNU` and compile fine with
  `-D_GNU_SOURCE=1` (verified with a standalone compile). Without this override, SDL3 defines its
  own `static inline getresgid()`/`getresuid()` fallback in `SDL_gtk.c`, which then conflicts with
  glibc's real (non-static) declaration and fails to compile. Pre-seeding the two `HAVE_*` cache
  variables skips SDL's broken check entirely (`CheckSymbolExists.cmake` only runs the check
  `if(NOT DEFINED "${VARIABLE}")`) and is factually correct, not a workaround-of-convenience.
- **`-DRust_CARGO_TARGET=aarch64-unknown-linux-gnu`**: `extern/aurora`'s `nod` (GameCube/Wii disc
  I/O) dependency has no prebuilt `linux-aarch64` package upstream (checked
  `github.com/encounter/nod/releases` directly — only `linux-x86_64`, `macos-arm64`,
  `windows-x86_64` `libnod-*` archives exist; `nodtool-linux-aarch64` is a separate CLI binary, not
  the library), so `AuroraNodProvider.cmake`'s `auto` resolution falls through to `vendor`
  (build-from-source via Corrosion). Corrosion picked the correct cross-compiler as the linker but
  left Cargo building for the **host** Rust target (`x86_64-unknown-linux-gnu`) by default, so
  rustc's x86_64 objects got fed to the aarch64 linker (`unrecognized command-line option '-m64'`).
  `AuroraNodProvider.cmake` already has an equivalent forced-target special case for 32-bit Windows
  (`if (WIN32 AND CMAKE_SIZEOF_VOID_P EQUAL 4) ... set(Rust_CARGO_TARGET "i686-pc-windows-msvc" ...)`)
  — this is the same fix, just for this target. Also requires the `aarch64-unknown-linux-gnu` Rust
  std to actually be installed (`rustup target add aarch64-unknown-linux-gnu`); a plain distro
  `rustc` package (e.g. Arch's) usually ships only the host std.

Beyond the above, expect ordinary cross-compile "missing sysroot package" failures: headers/libs in
`docs/building.md`'s Ubuntu/Arch package lists are host packages and won't exist for aarch64 on a
plain x86_64 host. `CMAKE_FIND_ROOT_PATH_MODE_{LIBRARY,INCLUDE,PACKAGE}=ONLY` in the toolchain file
means CMake only looks in the ROCKNIX sysroot for these — build the missing package for target via
`PROJECT=ROCKNIX DEVICE=RK3566 ARCH=aarch64 ./scripts/build_mt <pkg>` (it installs straight into
`$ROCKNIX_TOOLCHAIN_ROOT/aarch64-rocknix-linux-gnu/sysroot`, confirmed identical to
`SYSROOT_PREFIX` in ROCKNIX's own `config/path`, so no extra wiring is needed). This build needed,
beyond the base `toolchain` target: `wayland wayland-protocols libxkbcommon` (Wayland/keyboard
support for SDL) and `libglvnd` (provides `egl.pc`, needed by SDL's GLES/EGL detection — lighter
than building all of `mesa`, since actual GLES/Vulkan loading happens via `dlopen` at runtime, not
link time; SDL and WebGPU/Dawn only need the headers/pkg-config files at build time).

One CMake footgun hit along the way: after the *first* failed configure attempt (before the Wayland
packages existed), CMake caches negative `find_package`/`pkg_check_modules` results in
`CMakeCache.txt`. A `--fresh` reconfigure (or `rm -rf build/<preset>`) is needed after fixing a
missing-sysroot-package error — otherwise the stale cached "not found" persists even once the
package is actually there.

### Toolchain build (also done, not just this cross-build)

The ROCKNIX-produced toolchain itself (Section A) was also actually built here, via
`podman run ... ghcr.io/rocknix/rocknix-build:latest bash -c 'PROJECT=ROCKNIX DEVICE=RK3566
ARCH=aarch64 ./scripts/build_mt toolchain'` (Docker was present but not usable — the invoking user
isn't in the `docker` group and `/var/run/docker.sock` isn't group-writable for them; ROCKNIX's own
Makefile already falls back to `podman` automatically when `docker` isn't usable, rootless podman
works fine here). 51 package steps, ending in `gcc:bootstrap` (GCC 15.2.0,
`aarch64-rocknix-linux-gnu`) and `glibc:target`. No sandbox network blocks were hit in this
environment (unlike an earlier session that wrote the rest of this doc — `ghcr.io` and
`codeload.github.com` were both reachable here), so the "sandbox constraints" caveats from that
earlier pass no longer apply; they're kept below only as a record of what an *actually* restricted
environment looks like, in case this doc is reused somewhere more locked down.

<details>
<summary>Earlier sandbox-constrained session's notes (superseded — kept for reference only)</summary>

The session that first wrote this doc could not pull `ghcr.io/rocknix/rocknix-build` — its blob
storage host (`pkg-containers.githubusercontent.com`) was blocked by that sandbox's egress policy
(403). A generic (non-ROCKNIX) `aarch64-linux-gnu` toolchain was used there for a one-off smoke
test of the *CMake plumbing only*: cross-compiler detection, `-mcpu` flags, and
`AURORA_DAWN_PROVIDER=package` all worked mechanically, and Ubuntu's `gcc-*-aarch64-linux-gnu`
cross packages vs. `:arm64` multiarch dev packages were found to use two non-overlapping sysroot
layouts (`/usr/aarch64-linux-gnu/` vs. `/usr/lib/aarch64-linux-gnu/`) — a reason not to substitute a
generic toolchain even for local iteration. That session also hit `codeload.github.com` being
blocked separately from `ghcr.io`. None of this applied in the environment that did the real build
above.

</details>

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

**Status: a real package has been built from the Section B binary**, not just templated. The
build output (`build/linux-default-relwithdebinfo/dusklight`) is a genuine
`ELF 64-bit LSB executable, ARM aarch64 ... for GNU/Linux 6.10.0` — confirmed with `file` and
`readelf -d` against the ROCKNIX cross-toolchain's own `readelf`, not assumed from the build
succeeding. `readelf -d` lists exactly six `NEEDED` entries: `libc.so.6`, `libm.so.6`,
`ld-linux-aarch64.so.1` (core glibc/dynamic-linker — always from the device, never bundled, since
they must match the running kernel/base image exactly) and `libgcc_s.so.1`, `libstdc++.so.6`,
`libz.so.1` (auxiliary runtime libs — bundled alongside the binary, since `RPATH=$ORIGIN` is already
set by `CMakeLists.txt:203-204` and there's no guarantee the on-device versions match ours; sourced
straight from the toolchain's own sysroot/lib, so they're guaranteed ABI-compatible with the
binary). Confirms the doc's "Grafiktreiber"/backend libraries (Vulkan loader, GLES/EGL, Wayland)
are **not** link-time `NEEDED` entries at all — SDL and Dawn `dlopen()` those at runtime, matching
`SDL_DLOPEN_NOTES`/`SDL_HIDAPI` etc. being enabled in the SDL3 configure summary — so nothing
Wayland/Vulkan/GLES-related needs bundling here, only linked into the sysroot at build time.

The RelWithDebInfo binary is 490 MB unstripped (debug info intentionally kept by the preset for
first-hardware-test crash debugging); stripped with the ROCKNIX toolchain's own `strip` for the
actual deployable package it's 47 MB. Packaged layout matches Section D exactly:

```
roms/ports/Dusklight.sh
roms/ports/dusklight/
  dusklight             (stripped, 47 MB)
  libgcc_s.so.1
  libstdc++.so.6
  libz.so.1
  game/                 (empty — user drops their own dump here)
  logs/                 (created by the launcher on first run)
```

Total package ~50 MB uncompressed, ~24 MB as `tar.gz`. To install: extract onto the SD card's
`/storage/roms/ports/` (or scp the same layout directly there over the network/SSH, whichever
ROCKNIX transfer method is in use), drop a GameCube dump into `dusklight/game/`, and launch
"Dusklight" from the ROCKNIX ports menu. Section C (graphics backend) is the next thing to actually
verify once it boots.
