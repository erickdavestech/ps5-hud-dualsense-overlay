# Building

The build system comes from Common FPS for PS5 by porhe911 and is used unchanged except for the plugin
identity, the release verifier and the telemetry source added to the controller.

## Requirements

- A POSIX host (Linux or WSL) with `git`, `wget`, `unzip`, `python3`, `objdump`, CMake 3.20 or newer
  and a host C/C++ compiler for the tests.
- The [PS5 payload SDK](https://github.com/ps5-payload-dev/sdk). `PS5_PAYLOAD_SDK` must point to it
  (default: `/opt/ps5-payload-sdk`).

## 1. Fetch the pinned dependencies

```bash
bash ./scripts/prepare_ps5_deps.sh
```

The script installs PS5 payload SDK v0.43 — the version used to build the releases — into
`/opt/ps5-payload-sdk` (it uses `sudo`) and clones the exact etaHEN and shsrv commits listed in
`DEPENDENCIES.lock.json` into `.deps/`. If you already have SDK v0.43 installed, export
`PS5_PAYLOAD_SDK` and skip the SDK step.

## 2. Build the payloads

```bash
bash ./scripts/ps5_source_build.sh
```

Outputs in `dist/`:

| File | Description | Published as |
|---|---|---|
| `Common_FPS_PS5_v1.2.1.elf` | Controller payload with the renderer embedded | `ps5-hud-dualsense-overlay-<version>.elf` |
| `Common_FPS_PS5_etaHEN_v1.2.1.plugin` | The same payload wrapped as an etaHEN plugin (`PHUD00001`) | not published |
| `Common_FPS_ShellUI_v1.2.1.elf` | The renderer alone (already embedded in the controller) | not published |
| `SHA256SUMS.txt` | Checksums of the three files | — |

The internal file names come from upstream and are kept so the build system stays unchanged.

The script runs `tools/verify_release.py`, which checks that the renderer is embedded byte for byte in
the controller, that the plugin wraps exactly the controller ELF with this project's identity, that the
injection gates are present and that the renderer never references `/data`. It also checks that
`main` does not call `fork`.

## Host tests

```bash
cmake -S . -B build-host -DCMAKE_BUILD_TYPE=Release
cmake --build build-host
ctest --test-dir build-host --output-on-failure
python3 tests/test_plugin_wrapper.py
```

If your host only has a versioned compiler, select it explicitly, for example
`CC=clang-18 CXX=clang++-18 cmake -S . -B build-host ...`.

## Regenerating the artwork

The generated PNGs and their embedded C tables are committed, so a normal build does not need these
tools.

Performance panel:

```bash
python3 tools/build_perf_assets.py
```

Requires Pillow. It regenerates `assets/perf/png/` and `src/ps5/shellui_payload/perf_assets.{c,h}`:
the fan ring frames, the fan animation, the icons, the bar and track textures and the accent line.

DualSense controller:

```bash
python3 tools/build_assets.py
```

Requires Pillow and Google Chrome or Chromium (headless). It regenerates `assets/png/`,
`assets/layout.json` and the embedded sprite tables in `src/ps5/shellui_payload/` from the source SVGs
in `assets/source/`. `assets/preview.html` shows the result on a PC.

## Continuous integration

Every push to `main` and every pull request runs [`.github/workflows/ci.yml`](.github/workflows/ci.yml):
host tests, the upstream source gate, the plugin wrapper test and the full PS5 build with
`tools/verify_release.py`. Each run publishes the build checksums in its summary and attaches the build
as an artifact.

## Reproducible builds

Builds use ps5-payload-sdk v0.43 and the etaHEN and shsrv commits pinned in `DEPENDENCIES.lock.json`.
The build embeds no git or time data, so building the same commit from a clean checkout produces the
same controller ELF:

```bash
sha256sum dist/Common_FPS_PS5_v1.2.1.elf
```

Compare the result with the `SHA256SUMS.txt` published with each release.
