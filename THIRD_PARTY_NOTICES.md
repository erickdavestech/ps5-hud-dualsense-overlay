# Third-Party Notices

Common FPS for PS5 interoperates with and derives implementation knowledge from
open-source PS5 homebrew projects.

## PS5 Payload SDK

Project:

```text
https://github.com/ps5-payload-dev/sdk
```

Used as the PS5 payload toolchain/system SDK. The upstream project retains its
copyright notices and per-file licensing terms.

## etaHEN

Project:

```text
https://github.com/etaHEN/etaHEN
```

etaHEN publishes its source under GPLv3. Common FPS uses etaHEN as an
open-source reference/source dependency for PS5 process and ShellUI/PUI
integration.

## ps5-payload-dev/shsrv

Project:

```text
https://github.com/ps5-payload-dev/shsrv
```

GPLv3+ source used as the reference for the ptrace ELF loader and the
payload-args execution contract.

## Historical PHU-derived work

The historical Common FPS `v1.0.0` binary was developed through binary analysis
and patching of third-party PS5 homebrew renderer/loader mechanisms, including
PHU-derived material.

Common FPS does not claim ownership of that third-party code.

The source-built `v1.1.x` path intentionally does **not** embed the historical
PHU ELF/SO renderer blob.

## Common FPS-owned files

Files carrying:

```text
SPDX-License-Identifier: GPL-3.0-or-later
```

are licensed by the Common FPS project under GPL-3.0-or-later.

Third-party files always retain their upstream notices and licenses.

---

# Additional notices for this derivative (PS5 HUD and DualSense Controller Overlay)

This repository is a modified version of Common FPS for PS5, by way of ps5-dualsense-overlay (see
`CREDITS.md`). The notices above are retained verbatim from the upstream project. The following apply
to material added in the derivatives.

## Common FPS for PS5 (upstream base)

Project:

```text
https://github.com/porhe911/Common-FPS-for-PS5
```

GPL-3.0-or-later. The renderer, injection, controller architecture and build system derive from
this project (v1.2.1); its copyright headers are preserved in the source files. Files changed by
the derivatives carry a `Modifications Copyright (C) 2026 erickdavestech` line, and every change is
listed in `CHANGELOG.md`.

## SimpleFPS

Project:

```text
https://github.com/khalifa007/SimpleFPS
```

Author: khalifa007. GPL-3.0-or-later. The patch that makes Common FPS run on PS5 system software
11.xx–13.xx, which this project targets. Its changes are included in this repository, and the files
it modified carry a `Modifications Copyright (C) 2026 khalifa007 (SimpleFPS)` line.

## ps5-dualsense-overlay

Project:

```text
https://github.com/erickdavestech/ps5-dualsense-overlay
```

Author: erickdavestech. GPL-3.0-or-later. The DualSense overlay, its artwork pipeline and the
stability changes on top of Common FPS and SimpleFPS. This project starts from its `v1.0.1` tag.

## Telemetry references

```text
https://github.com/drakmor/ps5-hwinfo
https://github.com/RPCSX/rpcsx
```

ps5-hwinfo (drakmor, GPL-3.0) and RPCSX were used as references for the sensor, power-rail, clock and
memory-pool interfaces read by `src/ps5/perf_telemetry.cpp`. The source files that follow them name
them in their headers.

## elfldr (ptrace helpers used by the research probes)

Project:

```text
https://github.com/ps5-payload-dev/elfldr
```

`probe/pt/pt.c`, `probe/pt/pt.h` and `probe/pt/log.h` are Copyright (C) 2024 John Törnblom,
licensed GPLv3 or later. They are included unmodified with their original notices.

## PS5 Button Icons and Controls (artwork)

Author: Zacksly — https://zacksly.itch.io

```text
License: Creative Commons Attribution 3.0 Unported (CC BY 3.0)
```

The on-screen controller sprites are derived from this asset pack and were **modified** (cropped
and recomposed, sticks split into movable sprites, pressed states recolored, L2/R2 recolored).
The original license text is kept at `assets/source/LICENSE.txt`. This artwork is **not** under
GPL-3.0; it remains under CC BY 3.0 and must be attributed as shown in `CREDITS.md`.
