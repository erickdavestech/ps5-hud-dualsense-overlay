# Telemetry

How the controller payload reads each value shown on the performance panel, how often it does it and
what was checked on hardware. Every source is read-only. None of them attaches to the game process or
reads or writes its memory.

## Data flow

1. **Sampling:** `src/ps5/perf_telemetry.cpp` samples the sensors on the controller's main thread.
   Sony's sensor functions are not reliable from freshly spawned threads.
2. **Transport:** the latest values travel in a `TelemetryPacket` (`include/common_fps/telemetry_wire.hpp`,
   96 bytes, magic `PFTL`, version 3). The packet goes to the renderer over UDP loopback on port 39028,
   and only while the renderer is online.
3. **Validation:** a bit mask (`valid`) marks which fields hold a real reading. The renderer shows `--`
   for every field whose bit is clear.
4. **Staleness:** if no packet arrives for 300 UI frames (about 5 seconds), the renderer clears every
   value.

## Cadence

| Task | Interval |
|---|---|
| Sensors, clocks, FPS and CPU load | 0.5 s |
| RAM | 2 s |
| Rescan of the live memory pools | 30 s |
| Power rails | 5 s (the system refreshes them at about that rate) |
| Process scan and stability gates (controller) | 1 s |
| Renderer injection retry | 5 s |
| `Perf sample` line in the controller log | 60 s |

## Metrics

### FPS

- **Source:** the display controller's flip counter, read with an `ioctl` on `/dev/dce`:
  - request `0x80308217`, with a sign-extended fallback;
  - selector `0x10000000A` and mask `0x8000000000`;
  - the counter is at offset 8 of the returned block.
- **Calculation:** FPS = Δcount / Δtime between samples.
- **Buffer:** on 13.60 the kernel writes 128 bytes, more than the 0x60 bytes other tools reserve. The
  output buffer is therefore a dedicated 16 KB mapping with a guard page and a canary.
- **Errors:** after an error the device is closed and retried with a backoff that grows from 5 to 60
  seconds.
- **Origin:** the request and selector come from the DCE path of Common FPS for PS5.

### CPU

| Value | Source |
|---|---|
| Temperature | `sceKernelGetCpuTemperature` |
| Clock | `sceKernelGetCpuCoreClock` for each of the 8 cores; the panel shows the highest one |
| Load per thread (16 bars) | Idle-thread time from `sceKernelGetCpuUsage` |
| Total load | Mean of the 16 threads |

- **Load per thread:** load = 1 − Δidle / Δtime for each logical CPU.
- **Idle threads:** the threads named `SceIdleCpuN`, one per logical CPU, located once with
  `sceKernelGetThreadName`.
- **Units:** `sceKernelGetCpuUsage` reports per-thread times in microseconds.
- **Thread lookup:** the thread list is re-read each sample, and each idle thread is looked up starting
  from its previous index.

### GPU

| Value | Source |
|---|---|
| Clock | `sceKernelGetSocClock`, clock domain 20 (`GFXCLK`) |
| Power | GPU core rail of `sceKernelGetSocPowerConsumption` |

- The GPU **load** is not shown because there is no system function that reports it.

### SoC

| Value | Source |
|---|---|
| Part number | `hw.model` (for example `100-000001195`), shown without the `AMD ` prefix |
| Temperature | `sceKernelGetSocSensorTemperature(0)` |
| Total power | Sum of the 8 rails returned by `sceKernelGetSocPowerConsumption` |

- **Rail format:** `sceKernelGetSocPowerConsumption` takes a single argument and fills a 0x70-byte
  block. Each rail is a power / voltage / current triple in mW, mV and mA.
- **Label:** sensor 0 does not track the GPU, so it is labelled *SoC*.

### Fan

- **Source:** `sceKernelGetCurrentFanDuty` returns a duty from 0 to 1024; the panel shows it as a
  percentage.
- **Animation:** the fan icon turns faster as the duty rises.
- **RPM:** not shown, because there is no system function that reports it.

### RAM

The panel shows used / total **system** memory.

**Used** is the sum of three parts:

1. **System memory pools:** the direct memory used by each capped pool, read with `sys_budget_get`
   (syscall 570, resource 1 = direct memory).
2. **Pageable system memory:** `vm.stats.vm0` pages − free − inactive, at 16 KB per page.
3. **The game:** its resident pages, from `kinfo_proc.ki_rssize` (offset 264) of `kern.proc.pid`.

**Total** is the console's physical memory: 16 GB (16384 MB) of GDDR6.

- **Game pool:** a running game can also appear among the pools as an uncapped pool (total 0) whose
  "used" value is its whole reservation. That pool is skipped, because the game is already counted by
  its resident pages; adding both would exceed 16 GB.
- **MIB cache:** the `sysctl` MIBs are resolved once by name and then queried by number.
- **Pool list:** the list of live pools is rescanned every 30 seconds.

### Names

The panel uses the console's official specifications:

| Label | Value |
|---|---|
| GPU | Radeon RDNA 2 |
| CPU | Zen 2, 8 cores / 16 threads |
| Memory | 16 GB GDDR6, 448 GB/s, unified |

## Compatibility and resilience

- **Optional functions:** `sceKernelGetCpuCoreClock`, `sceKernelGetSocClock` and
  `sceKernelGetSocPowerConsumption` are resolved with `dlsym`. If one is missing, only its metric is
  marked invalid.
- **Rest Mode:** when the wall clock jumps by more than 3 seconds between samples, the console was
  suspended. The controller then resynchronizes:
  - closes the display controller and reopens it without the backoff delay;
  - discards the previous CPU sample, so load is computed from fresh samples;
  - re-reads the power rails, RAM and memory pools on the next sample;
  - marks FPS and CPU load invalid until new values are measured.
- **Game detection:** the game process is identified by a non-system title (titles starting with `NPXS`
  are ignored) and must be seen for 3 consecutive scans. The system UI must be stable for 10 scans
  before the renderer is injected.

## Hardware checks (PS5 Slim, 13.60)

- **CPU load:** the idle-thread method and the sum of all non-idle thread time gave the same result
  (18.5 % in the same sample).
- **Power:** the rails satisfy P = V × I; for example, a GPU core rail of 63 W at 1.108 V and 57.1 A.
- **RAM:** during gameplay the panel read about 9 GB of 16 GB in use. Before the game pool was
  excluded, the same scene was clamped at 16 GB.
- **UI frame rate:** the system UI kept 59.9 updates per second with the panel on screen. The renderer
  logs a warning if it drops below 55.
