// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <array>
#include <atomic>
#include <mutex>
#include <optional>
#include <span>
#include <string>
#include <thread>

#include "common/common_types.h"

/// Room codes for the Ocarina of Time multiplayer prototype: the two players find each other's
/// public address with STUN, trade it through a public message board (ntfy.sh, the room code is
/// the topic) and then send to each other directly, which opens most home routers both ways.
namespace Core::Nx2Room {

struct Room {
    bool host = false;
    std::string code;
    std::mutex mutex;
    std::string public_address; ///< This player's "ip:port" as STUN sees it (from the game socket).
    std::string peer_address;   ///< The other player's, once the board has it.
    std::atomic<bool> stop{false};
    std::atomic<bool> connected{false}; ///< Packets come in: the board is looked at less.
    std::optional<std::thread> board;
};

/// A new room code: 6 letters and digits, no look-alikes.
std::string NewCode();

/// Starts trading addresses through the board on a thread (stops with `room.stop`).
void Start(Room& room);

/// A STUN binding request (20 bytes) and the address in a binding response, if `data` is one.
std::array<u8, 20> StunRequest();
std::optional<std::string> StunAddress(std::span<const u8> data);

} // namespace Core::Nx2Room
