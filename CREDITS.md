# Credits

PS5 HUD and DualSense Controller Overlay is a derivative work. It claims no ownership over the upstream code or the artwork it
builds on. Every source file keeps its original copyright header and SPDX identifier, and each
modified file names who modified it.

## Upstream code

- **Common FPS for PS5** — **porhe911**
  - https://github.com/porhe911/Common-FPS-for-PS5 — GPL-3.0-or-later
  - The base of this project: the controller payload, the `SceShellUI`/PUI (Mono) injection, the hook
    protocol, the build system, the etaHEN plugin packaging and the host tests.
  - Its DCE path is the origin of the display-controller request this project uses to read FPS.
  - This project starts from Common FPS for PS5 v1.2.1.

- **SimpleFPS** — **khalifa007**
  - https://github.com/khalifa007/SimpleFPS — GPL-3.0-or-later
  - The patch that lets Common FPS run on system software 11.xx–13.xx: hook backend selection and the
    MDBG write path for those firmware families.

- **ps5-dualsense-overlay** — **erickdavestech**
  - https://github.com/erickdavestech/ps5-dualsense-overlay — GPL-3.0-or-later
  - The DualSense controller overlay, its artwork pipeline and the stability work on top of Common FPS
    and SimpleFPS. This project starts from its v1.0.1 tag.

## Telemetry references

These projects documented the interfaces this project reads and were used as references while
writing its telemetry code.

- **ps5-hwinfo** — **drakmor**
  - https://github.com/drakmor/ps5-hwinfo — GPL-3.0
  - Reference for decoding the SoC power rails, the SoC clock domains (`GFXCLK`), the fan duty and the
    idle-thread CPU load method.
- **RPCSX** — RPCSX contributors
  - https://github.com/RPCSX/rpcsx
  - Reference for the layout of the `sys_budget_get` records used to read the system memory pools.

## Artwork

- **"PS5 Button Icons and Controls"** — **Zacksly**
  - https://zacksly.itch.io — CC BY 3.0
  - Used to draw the on-screen controller.
  - **The artwork was modified:**
    - cropped and recomposed;
    - sticks split into movable sprites;
    - pressed states recolored with a glow;
    - L2/R2 recolored and placed over the shoulders.
  - The original license is kept in `assets/source/LICENSE.txt`.

  > "PS5 Button Icons and Controls - Zacksly
  > Licensed under CC BY 3.0 - https://zacksly.itch.io"

- The performance panel artwork (rings, fan, icons, bars and accent line in `assets/perf/`) is generated
  by `tools/build_perf_assets.py` and is part of this project (GPL-3.0-or-later).

## PS5 homebrew projects

- **PS5 payload SDK** — John Törnblom — https://github.com/ps5-payload-dev/sdk
  Toolchain and system headers used to build the payloads.
- **shsrv** — John Törnblom — https://github.com/ps5-payload-dev/shsrv — GPLv3+
  Reference for the ptrace ELF loader and the payload-arguments contract (fetched at build time).
- **elfldr** — John Törnblom — https://github.com/ps5-payload-dev/elfldr — GPLv3+
  The ptrace helpers in `probe/pt/` come from this project.
- **etaHEN** — LightningMods and contributors — https://github.com/etaHEN/etaHEN — GPLv3
  Homebrew enabler whose sources the build uses and whose plugin format the release follows.

Research references mentioned in `probe/`: the Ghostpad ptrace-RPC technique and the
ps5-native-gamepad-input-research notes.

## Original work in this project

By **erickdavestech**. Relative to ps5-dualsense-overlay v1.0.1:

| Area | Files |
|---|---|
| Performance panel renderer, outlined text, scaling, merge with the DualSense overlay, bounded renderer log | `src/ps5/shellui_payload/commonfps_shellui.cpp`, `src/ps5/shellui_payload/commonfps_shellui_entry.cpp` |
| Console telemetry (FPS, sensors, clocks, power, CPU load, RAM, Rest Mode resync) | `src/ps5/perf_telemetry.cpp`, `src/ps5/perf_telemetry.hpp` |
| Telemetry packet and its transport | `include/common_fps/telemetry_wire.hpp`, `src/ps5/state_sender.cpp`, `src/ps5/state_sender.hpp` |
| Controller loop: single process scan, stability gates, injection retries, event log | `src/ps5/commonfps_ps5_main.cpp` |
| Renderer marker and log names of this project (`hudoverlay_shellui.*`) | `src/ps5/shellui_injector.cpp`, `src/ps5/shellui_payload/commonfps_shellui_entry.cpp` |
| Panel artwork pipeline and embedded tables | `tools/build_perf_assets.py`, `assets/perf/`, `src/ps5/shellui_payload/perf_assets.*` |
| Project identity (`PHUD00001`) and release verification | `ps5/CMakeLists.txt`, `tools/verify_release.py` |
| Telemetry documentation | `docs/TELEMETRY.md` |

The work inherited from ps5-dualsense-overlay is listed in that project's credits and in
[CHANGELOG.md](CHANGELOG.md).

The source code is GPL-3.0-or-later; the controller artwork remains CC BY 3.0.
