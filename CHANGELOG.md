# Changelog

All notable changes to this project are documented in this file. The format follows
[Keep a Changelog](https://keepachangelog.com/en/1.1.0/) and the project uses
[Semantic Versioning](https://semver.org/).

## [Unreleased]

## [1.0.0] - 2026-10-04

First release of PS5 HUD and DualSense Controller Overlay (`ps5-hud-dualsense-overlay`). Based on ps5-dualsense-overlay v1.0.1, which is based on Common FPS
for PS5 v1.2.1 by porhe911 with the SimpleFPS patch by khalifa007.

### Added

- **Performance panel** in the top-left corner, drawn together with the DualSense overlay by a single
  renderer:
  - current FPS;
  - GPU power and clock;
  - CPU load, temperature, clock and per-thread load bars;
  - fan duty with an animated fan;
  - SoC part number, temperature and total power;
  - used and total system RAM with a bar and a percentage.
- **Console telemetry** in the controller (`src/ps5/perf_telemetry.cpp`), sampled twice per second on
  its main thread:
  - FPS from the display controller's flip counter, with a guarded output buffer and retry with backoff;
  - CPU and SoC temperatures, fan duty, CPU core clocks and the GPU clock domain;
  - SoC power rails and per-thread CPU load from idle-thread time;
  - total system RAM from the system memory pools, pageable system memory and the game's resident
    pages.
- **Optional sensor functions** resolved at run time: a missing function only disables its own metric.
- **Rest Mode resynchronization:** a jump in wall-clock time resets the FPS reader, the CPU sample and
  the power and RAM timers.
- **Telemetry packet:** 96 bytes, magic `PFTL`, version 3, with a validity mask. It is sent to the
  renderer over the existing UDP loopback channel. Values the renderer has not received for about 5
  seconds are shown as `--`.
- **Panel artwork pipeline** (`tools/build_perf_assets.py`) and its embedded sprite tables.
- **Documentation:** `docs/TELEMETRY.md` explains every metric and the hardware checks.

### Changed

- **Project identity:**
  - etaHEN plugin ID `PHUD00001`, version 1.00;
  - renderer marker and log files `/system_tmp/hudoverlay_shellui.{pid,log}`.
- **Renderer log:** capped at 12 KB, because `/system_tmp` is only a few KB and also holds system
  sockets. It no longer prints one line per sprite or asset.
- **Controller loop:**
  - one read-only process scan per second serves both the system UI gate (10 scans) and the game gate
    (3 scans);
  - injection is retried every 5 seconds;
  - telemetry is sent only while the renderer is online;
  - the event log is capped at 512 KB.
- **DualSense overlay:** its size and position now scale with the scene size.
- **Renderer setup:**
  - the `Game` scene name and the `RootWidget` getter are cached;
  - the panel reuses the scene root of the controller pass;
  - text and widths are set through cached property setters.

### Removed

- The 20 Hz FPS packet from the controller, which the renderer did not use.

### Known limitations

- GPU load and fan RPM are not shown: no system function reports them.
- Power values follow the system's refresh of the rails, about every 5 seconds.
- The PS button cannot be read, and the Mute button is disabled (inherited from ps5-dualsense-overlay).

[Unreleased]: https://github.com/erickdavestech/ps5-hud-dualsense-overlay/compare/v1.0.0...HEAD
[1.0.0]: https://github.com/erickdavestech/ps5-hud-dualsense-overlay/releases/tag/v1.0.0
