# ROCKNIX port packaging

Packaging templates for running Dusklight as a native ROCKNIX "port" (`roms/ports` convention),
targeting the Anbernic RG DS (RK3566/RK3568, Mali-G52 Bifrost).

See [`docs/rocknix-porting.md`](../../docs/rocknix-porting.md) for the full toolchain setup,
cross-build, and graphics-backend verification procedure. This directory only contains what gets
deployed to the device.

## Contents

- `dusklight.sh` — the port launcher. Deploy as `roms/ports/Dusklight.sh` (top-level, must sit
  directly under `roms/ports` per ROCKNIX's `SYSTEM_EXTENSION=".sh .appimage"` convention for the
  `ports` system).

## Deploying a build

After cross-building per `docs/rocknix-porting.md`, assemble on the device (or an SD card) as:

```
roms/ports/Dusklight.sh              <- platforms/rocknix/dusklight.sh
roms/ports/dusklight/
  dusklight                          <- build/<preset>/dusklight (the cross-built binary)
  <any shared libs not statically linked, alongside the binary — RPATH is $ORIGIN>
  game/                               <- user drops their own GameCube dump here
```

Nothing here hardcodes a game path or bundles copyrighted assets — see `dusklight.sh` for the
`game/` convention and the `DUSKLIGHT_DVD_PATH` override.
