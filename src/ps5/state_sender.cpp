/*
 * Common FPS for PS5
 * Copyright (C) 2026 porhe911
 * Modifications Copyright (C) 2026 erickdavestech
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#include "state_sender.hpp"

#include <arpa/inet.h>
#include <cstddef>
#include <cstring>
#include <netinet/in.h>
#include <sys/socket.h>
#include <unistd.h>

namespace common_fps::ps5 {

StateSender::StateSender() = default;

StateSender::~StateSender() {
    if (socket_ >= 0)
        close(socket_);
}

bool StateSender::open() {
    if (socket_ >= 0)
        return true;

    socket_ = socket(AF_INET, SOCK_DGRAM, 0);
    return socket_ >= 0;
}

namespace {

bool send_loopback(int socket_fd, const void* data, std::size_t size) {
    sockaddr_in addr{};
    addr.sin_family = AF_INET;
    addr.sin_port = htons(kDefaultIpcPort);
    addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);

    const auto sent = sendto(
        socket_fd,
        data,
        size,
        0,
        reinterpret_cast<sockaddr*>(&addr),
        sizeof(addr));

    return sent == static_cast<ssize_t>(size);
}

} // namespace

bool StateSender::send(const TelemetryPacket& packet) {
    if (!open())
        return false;
    return send_loopback(socket_, &packet, sizeof(packet));
}

bool StateSender::send(const WirePacket& packet) {
    if (!open())
        return false;

    sockaddr_in addr{};
    addr.sin_family = AF_INET;
    addr.sin_port = htons(kDefaultIpcPort);
    addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);

    const auto sent = sendto(
        socket_,
        &packet,
        sizeof(packet),
        0,
        reinterpret_cast<sockaddr*>(&addr),
        sizeof(addr));

    return sent == static_cast<ssize_t>(sizeof(packet));
}

} // namespace common_fps::ps5
