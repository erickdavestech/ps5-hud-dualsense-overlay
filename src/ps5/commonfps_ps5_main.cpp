/*
 * Common FPS for PS5
 * Copyright (C) 2026 porhe911
 * Modifications Copyright (C) 2026 erickdavestech
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#include "perf_telemetry.hpp"
#include "shellui_injector.hpp"
#include "state_sender.hpp"

#include <cerrno>
#include <cstdarg>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <ctime>
#include <fcntl.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>

namespace {

constexpr const char* kControllerLog = "/data/CommonFPS_v1_2_1.log";
constexpr off_t kLogMaxBytes = 512 * 1024;

constexpr long kSysctlSyscall = 202;
constexpr int kCtlKern = 1;
constexpr int kKernProc = 14;
constexpr int kKernProcProc = 8;
constexpr std::size_t kPidOffset = 72U;
constexpr std::size_t kCommOffset = 447U;
constexpr char kShellUiName[] = "SceShellUI";
constexpr char kGameName[] = "eboot.bin";

constexpr std::uint64_t kScanIntervalUs = 1'000'000ULL;
constexpr std::uint64_t kInjectRetryUs = 5'000'000ULL;
constexpr useconds_t kLoopSleepUs = 100'000U;
constexpr unsigned kShellUiStableScans = 10;
constexpr unsigned kGameStableScans = 3;

struct app_info_t {
    std::uint32_t app_id;
    std::uint64_t unknown1;
    char title_id[14];
    char unknown2[0x3c];
};

extern "C" int sceKernelGetAppInfo(pid_t pid, app_info_t* info);

void write_all(int fd, const char* data, std::size_t size) noexcept {
    while (size != 0) {
        const ssize_t written = write(fd, data, size);
        if (written > 0) {
            data += written;
            size -= static_cast<std::size_t>(written);
            continue;
        }
        if (written < 0 && errno == EINTR)
            continue;
        break;
    }
}

void log_event(const char* fmt, ...) noexcept {
    const int fd = open(kControllerLog, O_WRONLY | O_CREAT | O_APPEND, 0644);
    if (fd < 0)
        return;
    struct stat st {};
    if (fstat(fd, &st) == 0 && st.st_size > kLogMaxBytes)
        (void)ftruncate(fd, 0);

    char line[512];
    va_list ap;
    va_start(ap, fmt);
    int n = std::vsnprintf(line, sizeof(line) - 1, fmt, ap);
    va_end(ap);
    if (n < 0)
        n = 0;
    if (static_cast<std::size_t>(n) > sizeof(line) - 2)
        n = static_cast<int>(sizeof(line) - 2);
    line[n++] = '\n';
    write_all(fd, line, static_cast<std::size_t>(n));
    (void)close(fd);
}

std::uint64_t monotonic_us() noexcept {
    timespec ts{};
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return static_cast<std::uint64_t>(ts.tv_sec) * 1'000'000ULL +
           static_cast<std::uint64_t>(ts.tv_nsec) / 1000ULL;
}

struct ProcessScan {
    bool ok = false;
    pid_t shellui = -1;
    pid_t game = -1;
    char title[16]{};
    unsigned records = 0;
};

class ProcessScanner {
public:
    ProcessScanner() = default;
    ~ProcessScanner() { std::free(buffer_); }
    ProcessScanner(const ProcessScanner&) = delete;
    ProcessScanner& operator=(const ProcessScanner&) = delete;

    ProcessScan scan() noexcept {
        ProcessScan result{};
        int mib[4] = {kCtlKern, kKernProc, kKernProcProc, 0};
        std::size_t need = 0;
        if (syscall(kSysctlSyscall, mib, 4, nullptr, &need, nullptr, 0) != 0 || need == 0)
            return result;
        if (!reserve(need + need / 4U + 4096U))
            return result;

        std::size_t len = capacity_;
        if (syscall(kSysctlSyscall, mib, 4, buffer_, &len, nullptr, 0) != 0 || len > capacity_)
            return result;

        const pid_t self = getpid();
        const std::uint8_t* cursor = buffer_;
        const std::uint8_t* const end = buffer_ + len;
        while (cursor + sizeof(int) <= end) {
            int size_signed = 0;
            std::memcpy(&size_signed, cursor, sizeof(size_signed));
            if (size_signed <= 0 || static_cast<std::size_t>(size_signed) >
                                         static_cast<std::size_t>(end - cursor))
                break;
            const std::size_t size = static_cast<std::size_t>(size_signed);
            ++result.records;
            if (size >= kCommOffset + sizeof(kShellUiName)) {
                pid_t pid = -1;
                std::memcpy(&pid, cursor + kPidOffset, sizeof(pid));
                const char* comm = reinterpret_cast<const char*>(cursor + kCommOffset);
                if (pid > 0 && pid != self) {
                    if (result.shellui < 0 &&
                        std::memcmp(comm, kShellUiName, sizeof(kShellUiName)) == 0) {
                        result.shellui = pid;
                    } else if (result.game < 0 &&
                               std::memcmp(comm, kGameName, sizeof(kGameName)) == 0) {
                        app_info_t info{};
                        if (sceKernelGetAppInfo(pid, &info) == 0 && info.title_id[0] &&
                            std::strncmp(info.title_id, "NPXS", 4) != 0) {
                            result.game = pid;
                            std::snprintf(result.title, sizeof(result.title), "%.13s",
                                          info.title_id);
                        }
                    }
                }
            }
            cursor += size;
        }
        result.ok = true;
        return result;
    }

private:
    bool reserve(std::size_t need) noexcept {
        if (need <= capacity_)
            return true;
        void* grown = std::realloc(buffer_, need);
        if (!grown)
            return false;
        buffer_ = static_cast<std::uint8_t*>(grown);
        capacity_ = need;
        return true;
    }

    std::uint8_t* buffer_ = nullptr;
    std::size_t capacity_ = 0;
};

struct StableGate {
    pid_t pid = -1;
    unsigned seen = 0;

    bool update(pid_t observed, unsigned required) noexcept {
        if (observed > 0 && observed == pid) {
            if (seen < required)
                ++seen;
            return false;
        }
        const bool changed = observed != pid;
        pid = observed;
        seen = observed > 0 ? 1 : 0;
        return changed;
    }

    bool ready(unsigned required) const noexcept { return pid > 0 && seen >= required; }
};

void write_ready_record(const ProcessScan& first) noexcept {
    const int fd = open(kControllerLog, O_WRONLY | O_CREAT | O_TRUNC, 0644);
    if (fd < 0)
        return;
    char record[768];
    const int n = std::snprintf(
        record, sizeof(record),
        "Common FPS for PS5 v1.2.1 (performance HUD build)\n"
        "Mode=loader-tracked internal_fork=absent spawned_pid=resident "
        "shellui_observation=sysctl_comm_1s stability_gate=10 "
        "game_gate=process_present_stable_3s renderer_injection=deferred_until_game "
        "ipc=udp_loopback_telemetry_2hz game_memory_access=none\n"
        "Worker ready pid=%d ppid=%d first_shellui_pid=%d first_game_pid=%d records=%u\n",
        getpid(), getppid(), first.shellui, first.game, first.records);
    if (n > 0)
        write_all(fd, record, static_cast<std::size_t>(n) < sizeof(record)
                                  ? static_cast<std::size_t>(n)
                                  : sizeof(record) - 1);
    (void)close(fd);
}

[[noreturn]] void run_tracked_worker() noexcept {
    ProcessScanner scanner;
    write_ready_record(scanner.scan());

    common_fps::ps5::StateSender sender;
    common_fps::ps5::PerfTelemetry telemetry;
    StableGate shellui;
    StableGate game;
    bool renderer_online = false;
    std::uint64_t next_scan_us = 0;
    std::uint64_t next_inject_us = 0;

    for (;;) {
        const std::uint64_t now = monotonic_us();

        if (now >= next_scan_us) {
            next_scan_us = now + kScanIntervalUs;
            const ProcessScan scan = scanner.scan();
            if (scan.ok) {
                if (shellui.update(scan.shellui, kShellUiStableScans)) {
                    renderer_online = false;
                    next_inject_us = 0;
                    log_event("ShellUI pid=%d", scan.shellui);
                }
                if (game.update(scan.game, kGameStableScans))
                    log_event("Game pid=%d title=%s", scan.game,
                              scan.game > 0 ? scan.title : "-");
            }
        }

        const bool game_ready = game.ready(kGameStableScans);
        if (!renderer_online && game_ready && shellui.ready(kShellUiStableScans) &&
            now >= next_inject_us) {
            renderer_online = common_fps::ps5::ensure_shellui_renderer();
            next_inject_us = now + kInjectRetryUs;
            log_event("Renderer %s shellui=%d game=%d",
                      renderer_online ? "online" : "injection failed, retry in 5s", shellui.pid,
                      game.pid);
        }

        telemetry.set_game(game_ready ? game.pid : -1);
        if (telemetry.tick(now) && renderer_online)
            (void)sender.send(telemetry.packet());

        usleep(kLoopSleepUs);
    }
}

} // namespace

extern "C" int main() {
    run_tracked_worker();
}
