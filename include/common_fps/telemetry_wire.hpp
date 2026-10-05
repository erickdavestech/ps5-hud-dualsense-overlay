/*
 * Copyright (C) 2026 erickdavestech
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#pragma once

#include <cstdint>

namespace common_fps {

inline constexpr std::uint32_t kTelemetryMagic = 0x4C544650;
inline constexpr std::uint16_t kTelemetryVersion = 3;
inline constexpr int kTelemetryLanes = 16;
inline constexpr std::uint8_t kLaneUnknown = 0xff;

enum TelemetryValid : std::uint32_t {
    kValidFps = 1u << 0,
    kValidCpuTemp = 1u << 1,
    kValidSocTemp = 1u << 2,
    kValidFan = 1u << 3,
    kValidCpuClock = 1u << 4,
    kValidGpuClock = 1u << 5,
    kValidCpuLoad = 1u << 6,
    kValidGpuPower = 1u << 7,
    kValidRam = 1u << 8,
    kValidApuPower = 1u << 9,
};

#pragma pack(push, 1)
struct TelemetryPacket {
    std::uint32_t magic = kTelemetryMagic;
    std::uint16_t version = kTelemetryVersion;
    std::uint16_t size = sizeof(TelemetryPacket);
    std::uint64_t sequence = 0;
    std::uint32_t valid = 0;

    float fps = 0.0f;
    std::int16_t cpu_temp_c = 0;
    std::int16_t soc_temp_c = 0;
    std::uint16_t fan_duty_raw = 0;
    std::uint16_t cpu_clock_max_mhz = 0;
    std::uint16_t cpu_clock_avg_mhz = 0;
    std::uint16_t gpu_clock_mhz = 0;
    float cpu_load = 0.0f;
    std::uint8_t lane_load[kTelemetryLanes]{};
    float gpu_power_w = 0.0f;
    float apu_power_w = 0.0f;
    std::uint32_t ram_used_mb = 0;
    std::uint32_t ram_total_mb = 0;
    char soc_name[24]{};
};
#pragma pack(pop)

static_assert(sizeof(TelemetryPacket) == 96, "TelemetryPacket layout changed");

} // namespace common_fps
