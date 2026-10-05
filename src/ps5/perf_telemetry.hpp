/*
 * Copyright (C) 2026 erickdavestech
 * Telemetry decoding follows ps5-hwinfo (drakmor), GPL-3.0.
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#pragma once

#include "common_fps/telemetry_wire.hpp"

#include <cstdint>
#include <sys/types.h>

namespace common_fps::ps5 {

class PerfTelemetry {
public:
    PerfTelemetry();
    ~PerfTelemetry();

    PerfTelemetry(const PerfTelemetry&) = delete;
    PerfTelemetry& operator=(const PerfTelemetry&) = delete;

    void set_game(pid_t pid);
    bool tick(std::uint64_t now_us);
    const TelemetryPacket& packet() const { return packet_; }

private:
    bool dce_sample(std::uint64_t now_us, std::uint64_t* count);
    void dce_close();
    void dce_fail(std::uint64_t now_us, const char* why, int err);
    void resync_after_gap();
    void sample_fps(std::uint64_t now_us);
    void sample_sensors();
    void sample_cpu_loads();
    void sample_power(std::uint64_t now_us);
    void sample_ram(std::uint64_t now_us);

    pid_t game_pid_ = -1;
    int dce_fd_ = -1;
    unsigned long dce_request_ = 0;
    std::uint64_t dce_retry_us_ = 0;
    std::uint64_t dce_backoff_us_ = 0;
    std::uint64_t dce_prev_count_ = 0;
    std::uint64_t dce_prev_us_ = 0;
    std::uint64_t next_sample_us_ = 0;
    std::uint64_t next_power_us_ = 0;
    std::uint64_t next_ram_us_ = 0;
    std::uint64_t next_budget_scan_us_ = 0;
    std::int64_t last_wall_s_ = 0;
    int idle_lanes_ = 0;
    int thread_bank_ = 0;
    unsigned ticks_ = 0;
    bool have_thread_sample_ = false;
    std::uint32_t idle_ids_[kTelemetryLanes]{};
    std::int32_t idle_hint_[2][kTelemetryLanes]{};
    std::uint32_t live_budgets_ = 0;
    TelemetryPacket packet_{};
    char soc_name_[24]{};
};

} // namespace common_fps::ps5
