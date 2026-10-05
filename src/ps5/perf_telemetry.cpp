/*
 * Copyright (C) 2026 erickdavestech
 * Telemetry decoding follows ps5-hwinfo (drakmor), GPL-3.0.
 * Budget layout (sys_budget_get) follows RPCSX.
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#include "perf_telemetry.hpp"

#include <cerrno>
#include <cstdarg>
#include <cstdio>
#include <cstring>
#include <ctime>
#include <dlfcn.h>
#include <fcntl.h>
#include <sys/ioctl.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <sys/sysctl.h>
#include <unistd.h>

extern "C" {
int sceKernelGetCpuTemperature(int* out_celsius);
int sceKernelGetSocSensorTemperature(int sensor_id, int* out_celsius);
int sceKernelGetCurrentFanDuty(std::uint16_t* out_duty, std::uint64_t* out_chassis);
int sceKernelGetCpuUsage(void* out, std::int32_t* size);
int sceKernelGetThreadName(std::uint32_t id, char* out);
}

namespace common_fps::ps5 {
namespace {

constexpr const char* kLogPath = "/data/CommonFPS_v1_2_1.log";
constexpr off_t kLogMaxBytes = 512 * 1024;

constexpr unsigned long kDceIoctlZero = 0x0000000080308217UL;
constexpr unsigned long kDceIoctlSign = 0xFFFFFFFF80308217UL;
constexpr std::uint64_t kDceArg0 = 0x10000000AULL;
constexpr std::uint64_t kDceArg1 = 0x8000000000ULL;
constexpr std::size_t kDceCountOffset = 8;
constexpr std::size_t kDceBufferBytes = 0x4000;
constexpr std::uint8_t kDceCanary = 0xA5;
constexpr std::uint64_t kDceBackoffMinUs = 5'000'000ULL;
constexpr std::uint64_t kDceBackoffMaxUs = 60'000'000ULL;

constexpr std::uint64_t kSampleIntervalUs = 500'000ULL;
constexpr std::uint64_t kPowerIntervalUs = 5'000'000ULL;
constexpr std::uint64_t kRamIntervalUs = 2'000'000ULL;
constexpr std::uint64_t kBudgetRescanUs = 30'000'000ULL;
constexpr std::int64_t kResumeGapS = 3;
constexpr unsigned kLogEveryTicks = 120;

constexpr int kSocClockGfx = 20;
constexpr int kCpuCores = 8;
constexpr int kMaxThreads = 3072;
constexpr long kSysBudgetGet = 570;
constexpr int kMaxBudgetId = 32;
constexpr int kBudgetItems = 16;
constexpr std::uint32_t kBudgetDmem = 1;
constexpr double kPageMb = 16384.0 / 1048576.0;
constexpr std::uint32_t kPhysicalRamMb = 16384;
constexpr std::size_t kKinfoPidOffset = 72;
constexpr std::size_t kKinfoRssOffset = 264;

using CoreClockFn = int (*)(int*);
using SocClockFn = int (*)(std::uint32_t*);
using SocPowerFn = int (*)(void*);

struct DceArg {
    std::uint64_t selector;
    std::uint64_t mask;
    std::uint64_t output;
    std::uint64_t reserved[3];
};

struct OrbisTimeval {
    std::int64_t tv_sec;
    std::int64_t tv_usec;
};

struct ProcStats {
    std::uint32_t process_id;
    std::uint32_t td_tid;
    OrbisTimeval user_time;
    OrbisTimeval system_time;
};

struct BudgetInfo {
    std::uint32_t resource;
    std::uint32_t flags;
    std::uint64_t total;
    std::uint64_t used;
};

struct ThreadSample {
    std::uint64_t t_us;
    std::int32_t count;
    ProcStats threads[kMaxThreads];
};

struct CachedSysctl {
    const char* name;
    int mib[8];
    std::size_t len;
    bool resolved;
    bool failed;
};

std::uint8_t* g_dce_buffer = nullptr;
DceArg g_dce_arg __attribute__((aligned(64)));
ThreadSample g_thread_samples[2];
int g_core_clocks[256];
std::uint32_t g_soc_clocks[256];
std::uint8_t g_power_raw[0x400] __attribute__((aligned(64)));
BudgetInfo g_budget[kBudgetItems];

CoreClockFn g_core_clock_fn = nullptr;
SocClockFn g_soc_clock_fn = nullptr;
SocPowerFn g_soc_power_fn = nullptr;

CachedSysctl g_vm0_pages{"vm.stats.vm0.v_page_count", {}, 0, false, false};
CachedSysctl g_vm0_free{"vm.stats.vm0.v_free_count", {}, 0, false, false};
CachedSysctl g_vm0_inactive{"vm.stats.vm0.v_inactive_count", {}, 0, false, false};

void log_line(const char* fmt, ...) {
    const int fd = open(kLogPath, O_WRONLY | O_CREAT | O_APPEND, 0644);
    if (fd < 0)
        return;
    struct stat st {};
    if (fstat(fd, &st) == 0 && st.st_size > kLogMaxBytes)
        (void)ftruncate(fd, 0);
    char line[384];
    va_list ap;
    va_start(ap, fmt);
    int n = std::vsnprintf(line, sizeof(line) - 1, fmt, ap);
    va_end(ap);
    if (n < 0)
        n = 0;
    if (static_cast<std::size_t>(n) > sizeof(line) - 2)
        n = static_cast<int>(sizeof(line) - 2);
    line[n++] = '\n';
    (void)write(fd, line, static_cast<std::size_t>(n));
    (void)close(fd);
}

std::uint64_t monotonic_us() {
    timespec ts{};
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return static_cast<std::uint64_t>(ts.tv_sec) * 1'000'000ULL +
           static_cast<std::uint64_t>(ts.tv_nsec) / 1000ULL;
}

std::int64_t wall_seconds() {
    timespec ts{};
    clock_gettime(CLOCK_REALTIME, &ts);
    return static_cast<std::int64_t>(ts.tv_sec);
}

bool read_u32(CachedSysctl& c, std::uint64_t* out) {
    if (c.failed)
        return false;
    if (!c.resolved) {
        c.len = sizeof(c.mib) / sizeof(c.mib[0]);
        if (sysctlnametomib(c.name, c.mib, &c.len) != 0) {
            c.failed = true;
            return false;
        }
        c.resolved = true;
    }
    std::uint32_t v = 0;
    std::size_t len = sizeof(v);
    if (sysctl(c.mib, static_cast<unsigned>(c.len), &v, &len, nullptr, 0) != 0)
        return false;
    *out = v;
    return true;
}

bool dce_buffer_ready() {
    if (g_dce_buffer)
        return true;
    void* p = mmap(nullptr, kDceBufferBytes * 2, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANON,
                   -1, 0);
    if (p == MAP_FAILED)
        return false;
    auto* bytes = static_cast<std::uint8_t*>(p);
    if (mprotect(bytes + kDceBufferBytes, kDceBufferBytes, PROT_NONE) != 0) {
        munmap(p, kDceBufferBytes * 2);
        return false;
    }
    g_dce_buffer = bytes;
    return true;
}

std::size_t dce_bytes_written() {
    std::size_t n = kDceBufferBytes;
    while (n > 0 && g_dce_buffer[n - 1] == kDceCanary)
        --n;
    return n;
}

double proc_seconds(const ProcStats& p) {
    return static_cast<double>(p.user_time.tv_sec) +
           static_cast<double>(p.user_time.tv_usec) / 1e6 +
           static_cast<double>(p.system_time.tv_sec) +
           static_cast<double>(p.system_time.tv_usec) / 1e6;
}

const ProcStats* find_thread(const ThreadSample& s, std::uint32_t tid, std::int32_t* hint) {
    if (*hint >= 0 && *hint < s.count && s.threads[*hint].td_tid == tid)
        return &s.threads[*hint];
    for (std::int32_t i = 0; i < s.count; ++i) {
        if (s.threads[i].td_tid == tid) {
            *hint = i;
            return &s.threads[i];
        }
    }
    *hint = -1;
    return nullptr;
}

bool take_thread_sample(ThreadSample& s) {
    s.count = kMaxThreads;
    if (sceKernelGetCpuUsage(s.threads, &s.count) != 0 || s.count <= 0 || s.count > kMaxThreads)
        return false;
    s.t_us = monotonic_us();
    return true;
}

int resolve_idle_threads(const ThreadSample& s, std::uint32_t ids[kTelemetryLanes]) {
    char name[0x40];
    int found = 0;
    std::memset(ids, 0, sizeof(std::uint32_t) * kTelemetryLanes);
    for (std::int32_t i = 0; i < s.count && found < kTelemetryLanes; ++i) {
        int lane = -1;
        std::memset(name, 0, sizeof(name));
        if (sceKernelGetThreadName(s.threads[i].td_tid, name) != 0)
            continue;
        if (std::strcmp(name, "SceIdleCpuRv") == 0) {
            if (!ids[13]) {
                ids[13] = s.threads[i].td_tid;
                ++found;
            }
            continue;
        }
        if (std::sscanf(name, "SceIdleCpu%d", &lane) == 1 && lane >= 0 &&
            lane < kTelemetryLanes && lane != 13 && !ids[lane]) {
            ids[lane] = s.threads[i].td_tid;
            ++found;
        }
    }
    return found;
}

bool game_resident_mb(pid_t pid, double* out_mb) {
    alignas(8) std::uint8_t buf[2048];
    std::size_t len = sizeof(buf);
    int mib[4] = {1, 14, 1, static_cast<int>(pid)};
    if (sysctl(mib, 4, buf, &len, nullptr, 0) != 0 || len < kKinfoRssOffset + 8)
        return false;
    int structsize = 0;
    int kpid = 0;
    std::int64_t rss_pages = 0;
    std::memcpy(&structsize, buf, sizeof(structsize));
    std::memcpy(&kpid, buf + kKinfoPidOffset, sizeof(kpid));
    std::memcpy(&rss_pages, buf + kKinfoRssOffset, sizeof(rss_pages));
    if (structsize <= 0 || static_cast<std::size_t>(structsize) > len || kpid != pid ||
        rss_pages < 0)
        return false;
    *out_mb = static_cast<double>(rss_pages) * kPageMb;
    return true;
}

bool budget_dmem_used(int id, std::uint64_t* used) {
    int count = kBudgetItems;
    std::memset(g_budget, 0, sizeof(g_budget));
    if (syscall(kSysBudgetGet, id, g_budget, &count) != 0)
        return false;
    for (int i = 0; i < count && i < kBudgetItems; ++i) {
        if (g_budget[i].resource == kBudgetDmem) {
            if (g_budget[i].total == 0)
                return false;
            *used = g_budget[i].used;
            return true;
        }
    }
    return false;
}

} // namespace

PerfTelemetry::PerfTelemetry() {
    g_core_clock_fn = reinterpret_cast<CoreClockFn>(dlsym(RTLD_DEFAULT, "sceKernelGetCpuCoreClock"));
    g_soc_clock_fn = reinterpret_cast<SocClockFn>(dlsym(RTLD_DEFAULT, "sceKernelGetSocClock"));
    g_soc_power_fn =
        reinterpret_cast<SocPowerFn>(dlsym(RTLD_DEFAULT, "sceKernelGetSocPowerConsumption"));

    char model[64]{};
    std::size_t len = sizeof(model) - 1;
    if (sysctlbyname("hw.model", model, &len, nullptr, 0) == 0) {
        std::size_t n = std::strlen(model);
        while (n > 0 && model[n - 1] == ' ')
            model[--n] = 0;
        std::snprintf(soc_name_, sizeof(soc_name_), "AMD %s", model);
    }
    last_wall_s_ = wall_seconds();
    log_line("Perf telemetry init optional=core_clock:%d soc_clock:%d soc_power:%d soc=%s",
             g_core_clock_fn ? 1 : 0, g_soc_clock_fn ? 1 : 0, g_soc_power_fn ? 1 : 0, soc_name_);
}

PerfTelemetry::~PerfTelemetry() {
    dce_close();
}

void PerfTelemetry::dce_close() {
    if (dce_fd_ >= 0) {
        close(dce_fd_);
        dce_fd_ = -1;
    }
}

void PerfTelemetry::dce_fail(std::uint64_t now_us, const char* why, int err) {
    dce_close();
    dce_prev_us_ = 0;
    dce_backoff_us_ = dce_backoff_us_ == 0 ? kDceBackoffMinUs
                                           : (dce_backoff_us_ * 2 > kDceBackoffMaxUs
                                                  ? kDceBackoffMaxUs
                                                  : dce_backoff_us_ * 2);
    dce_retry_us_ = now_us + dce_backoff_us_;
    log_line("Perf FPS paused (%s errno=%d), retry in %llus", why, err,
             static_cast<unsigned long long>(dce_backoff_us_ / 1'000'000ULL));
}

void PerfTelemetry::resync_after_gap() {
    dce_close();
    dce_prev_us_ = 0;
    dce_retry_us_ = 0;
    dce_backoff_us_ = 0;
    have_thread_sample_ = false;
    next_power_us_ = 0;
    next_ram_us_ = 0;
    next_budget_scan_us_ = 0;
    packet_.valid &= ~(kValidFps | kValidCpuLoad);
    log_line("Perf telemetry resync after suspend/resume");
}

void PerfTelemetry::set_game(pid_t pid) {
    if (pid == game_pid_)
        return;

    dce_close();
    game_pid_ = pid;
    dce_request_ = kDceIoctlZero;
    dce_retry_us_ = 0;
    dce_backoff_us_ = 0;
    dce_prev_count_ = 0;
    dce_prev_us_ = 0;
    next_sample_us_ = 0;
    next_power_us_ = 0;
    next_ram_us_ = 0;
    next_budget_scan_us_ = 0;
    idle_lanes_ = 0;
    have_thread_sample_ = false;
    std::memset(idle_ids_, 0, sizeof(idle_ids_));
    std::memset(idle_hint_, 0xff, sizeof(idle_hint_));

    const std::uint64_t sequence = packet_.sequence;
    packet_ = TelemetryPacket{};
    packet_.sequence = sequence;
    std::memcpy(packet_.soc_name, soc_name_, sizeof(packet_.soc_name));

    if (pid > 0)
        log_line("Perf telemetry armed pid=%d", static_cast<int>(pid));
    else
        log_line("Perf telemetry idle (no game)");
}

bool PerfTelemetry::dce_sample(std::uint64_t now_us, std::uint64_t* count) {
    if (now_us < dce_retry_us_)
        return false;

    if (dce_fd_ < 0) {
        if (!dce_buffer_ready()) {
            dce_fail(now_us, "guard buffer", errno);
            return false;
        }
        dce_fd_ = open("/dev/dce", O_RDWR);
        if (dce_fd_ < 0) {
            dce_fail(now_us, "open", errno);
            return false;
        }
    }

    std::memset(g_dce_buffer, kDceCanary, kDceBufferBytes);
    std::memset(&g_dce_arg, 0, sizeof(g_dce_arg));
    g_dce_arg.selector = kDceArg0;
    g_dce_arg.mask = kDceArg1;
    g_dce_arg.output = reinterpret_cast<std::uint64_t>(g_dce_buffer);

    int rc = ioctl(dce_fd_, dce_request_, &g_dce_arg);
    if (rc < 0 && errno == EINVAL && dce_request_ == kDceIoctlZero) {
        dce_request_ = kDceIoctlSign;
        std::memset(g_dce_buffer, kDceCanary, kDceBufferBytes);
        rc = ioctl(dce_fd_, dce_request_, &g_dce_arg);
    }
    if (rc < 0) {
        dce_fail(now_us, "ioctl", errno);
        return false;
    }

    const std::size_t written = dce_bytes_written();
    if (written <= kDceCountOffset + sizeof(*count) || written >= kDceBufferBytes) {
        dce_fail(now_us, "unexpected size", static_cast<int>(written));
        return false;
    }

    dce_backoff_us_ = 0;
    std::memcpy(count, g_dce_buffer + kDceCountOffset, sizeof(*count));
    return true;
}

void PerfTelemetry::sample_fps(std::uint64_t now_us) {
    std::uint64_t count = 0;
    if (!dce_sample(now_us, &count)) {
        packet_.valid &= ~kValidFps;
        return;
    }
    if (dce_prev_us_ != 0 && now_us > dce_prev_us_ && count >= dce_prev_count_) {
        const double dt = static_cast<double>(now_us - dce_prev_us_) / 1e6;
        const double fps = static_cast<double>(count - dce_prev_count_) / dt;
        if (fps >= 0.0 && fps <= 500.0) {
            packet_.fps = static_cast<float>(fps);
            packet_.valid |= kValidFps;
        }
    }
    dce_prev_count_ = count;
    dce_prev_us_ = now_us;
}

void PerfTelemetry::sample_sensors() {
    int cpu_t = 0;
    if (sceKernelGetCpuTemperature(&cpu_t) == 0 && cpu_t > 0 && cpu_t < 150) {
        packet_.cpu_temp_c = static_cast<std::int16_t>(cpu_t);
        packet_.valid |= kValidCpuTemp;
    }

    int soc_t = 0;
    if (sceKernelGetSocSensorTemperature(0, &soc_t) == 0 && soc_t > 0 && soc_t < 150) {
        packet_.soc_temp_c = static_cast<std::int16_t>(soc_t);
        packet_.valid |= kValidSocTemp;
    }

    std::uint16_t duty = 0;
    std::uint64_t chassis = 0;
    if (sceKernelGetCurrentFanDuty(&duty, &chassis) == 0 && duty <= 1024) {
        packet_.fan_duty_raw = duty;
        packet_.valid |= kValidFan;
    }

    if (g_core_clock_fn) {
        std::memset(g_core_clocks, 0, sizeof(g_core_clocks));
        if (g_core_clock_fn(g_core_clocks) == 0) {
            int max_mhz = 0;
            int sum = 0;
            int n = 0;
            for (int i = 0; i < kCpuCores; ++i) {
                const int mhz = g_core_clocks[i];
                if (mhz > 0 && mhz < 10000) {
                    if (mhz > max_mhz)
                        max_mhz = mhz;
                    sum += mhz;
                    ++n;
                }
            }
            if (n > 0) {
                packet_.cpu_clock_max_mhz = static_cast<std::uint16_t>(max_mhz);
                packet_.cpu_clock_avg_mhz = static_cast<std::uint16_t>(sum / n);
                packet_.valid |= kValidCpuClock;
            }
        }
    }

    if (g_soc_clock_fn) {
        std::memset(g_soc_clocks, 0, sizeof(g_soc_clocks));
        if (g_soc_clock_fn(g_soc_clocks) == 0 && g_soc_clocks[kSocClockGfx] > 0 &&
            g_soc_clocks[kSocClockGfx] < 10000) {
            packet_.gpu_clock_mhz = static_cast<std::uint16_t>(g_soc_clocks[kSocClockGfx]);
            packet_.valid |= kValidGpuClock;
        }
    }
}

void PerfTelemetry::sample_cpu_loads() {
    ThreadSample& cur = g_thread_samples[thread_bank_];
    if (!take_thread_sample(cur)) {
        packet_.valid &= ~kValidCpuLoad;
        return;
    }

    if (idle_lanes_ == 0) {
        idle_lanes_ = resolve_idle_threads(cur, idle_ids_);
        if (idle_lanes_ > 0)
            log_line("Perf CPU idle threads resolved=%d", idle_lanes_);
    }

    if (have_thread_sample_ && idle_lanes_ > 0) {
        const int prev_bank = 1 - thread_bank_;
        const ThreadSample& prev = g_thread_samples[prev_bank];
        const double dt = static_cast<double>(cur.t_us - prev.t_us) / 1e6;
        double sum = 0.0;
        int n = 0;
        for (int lane = 0; lane < kTelemetryLanes; ++lane) {
            packet_.lane_load[lane] = kLaneUnknown;
            if (!idle_ids_[lane] || dt <= 0.001)
                continue;
            const ProcStats* a = find_thread(prev, idle_ids_[lane], &idle_hint_[prev_bank][lane]);
            const ProcStats* b = find_thread(cur, idle_ids_[lane], &idle_hint_[thread_bank_][lane]);
            if (!a || !b)
                continue;
            double load = 100.0 * (1.0 - (proc_seconds(*b) - proc_seconds(*a)) / dt);
            if (load < 0.0)
                load = 0.0;
            if (load > 100.0)
                load = 100.0;
            packet_.lane_load[lane] = static_cast<std::uint8_t>(load + 0.5);
            sum += load;
            ++n;
        }
        if (n > 0) {
            packet_.cpu_load = static_cast<float>(sum / n);
            packet_.valid |= kValidCpuLoad;
        } else {
            idle_lanes_ = 0;
        }
    }

    have_thread_sample_ = true;
    thread_bank_ = 1 - thread_bank_;
}

void PerfTelemetry::sample_power(std::uint64_t now_us) {
    if (!g_soc_power_fn || now_us < next_power_us_)
        return;
    next_power_us_ = now_us + kPowerIntervalUs;

    std::memset(g_power_raw, 0, sizeof(g_power_raw));
    if (g_soc_power_fn(g_power_raw) != 0) {
        packet_.valid &= ~(kValidGpuPower | kValidApuPower);
        return;
    }
    std::uint32_t rails[24];
    std::memcpy(rails, g_power_raw, sizeof(rails));
    if (rails[0] > 0 && rails[0] < 400000) {
        packet_.gpu_power_w = static_cast<float>(rails[0]) / 1000.0f;
        packet_.valid |= kValidGpuPower;
    } else {
        packet_.valid &= ~kValidGpuPower;
    }
    std::uint64_t total_mw = 0;
    for (int rail = 0; rail < 8; ++rail)
        total_mw += rails[rail * 3];
    if (total_mw > 0 && total_mw < 500000) {
        packet_.apu_power_w = static_cast<float>(total_mw) / 1000.0f;
        packet_.valid |= kValidApuPower;
    } else {
        packet_.valid &= ~kValidApuPower;
    }
}

void PerfTelemetry::sample_ram(std::uint64_t now_us) {
    if (now_us < next_ram_us_)
        return;
    next_ram_us_ = now_us + kRamIntervalUs;

    if (now_us >= next_budget_scan_us_ || live_budgets_ == 0) {
        next_budget_scan_us_ = now_us + kBudgetRescanUs;
        live_budgets_ = 0;
        for (int id = 0; id < kMaxBudgetId; ++id) {
            std::uint64_t used = 0;
            if (budget_dmem_used(id, &used))
                live_budgets_ |= 1u << id;
        }
    }

    std::uint64_t dmem_used = 0;
    for (int id = 0; id < kMaxBudgetId; ++id) {
        std::uint64_t used = 0;
        if ((live_budgets_ & (1u << id)) && budget_dmem_used(id, &used))
            dmem_used += used;
    }

    std::uint64_t vm0_total = 0, vm0_free = 0, vm0_inactive = 0;
    double game_mb = 0.0;
    const bool ok = live_budgets_ != 0 && read_u32(g_vm0_pages, &vm0_total) &&
                    read_u32(g_vm0_free, &vm0_free) && read_u32(g_vm0_inactive, &vm0_inactive) &&
                    vm0_free + vm0_inactive <= vm0_total && game_resident_mb(game_pid_, &game_mb);
    if (!ok) {
        packet_.valid &= ~kValidRam;
        return;
    }

    double used_mb = static_cast<double>(dmem_used) / 1048576.0 +
                     static_cast<double>(vm0_total - vm0_free - vm0_inactive) * kPageMb + game_mb;
    if (used_mb > kPhysicalRamMb)
        used_mb = kPhysicalRamMb;
    packet_.ram_used_mb = static_cast<std::uint32_t>(used_mb + 0.5);
    packet_.ram_total_mb = kPhysicalRamMb;
    packet_.valid |= kValidRam;
}

bool PerfTelemetry::tick(std::uint64_t now_us) {
    if (game_pid_ <= 0 || now_us < next_sample_us_)
        return false;
    next_sample_us_ = now_us + kSampleIntervalUs;

    const std::int64_t wall = wall_seconds();
    if (wall - last_wall_s_ > kResumeGapS && last_wall_s_ != 0)
        resync_after_gap();
    last_wall_s_ = wall;

    sample_fps(now_us);
    sample_sensors();
    sample_cpu_loads();
    sample_power(now_us);
    sample_ram(now_us);

    ++packet_.sequence;
    if (++ticks_ % kLogEveryTicks == 0) {
        log_line("Perf sample fps=%.1f cpuT=%d socT=%d fan=%u/1024 cpu=%.1f%% clk=%u gfx=%u "
                 "gpuW=%.1f apuW=%.1f ram=%u/%u valid=0x%x",
                 static_cast<double>(packet_.fps), packet_.cpu_temp_c, packet_.soc_temp_c,
                 packet_.fan_duty_raw, static_cast<double>(packet_.cpu_load),
                 packet_.cpu_clock_max_mhz, packet_.gpu_clock_mhz,
                 static_cast<double>(packet_.gpu_power_w),
                 static_cast<double>(packet_.apu_power_w), packet_.ram_used_mb,
                 packet_.ram_total_mb, packet_.valid);
    }
    return true;
}

} // namespace common_fps::ps5
