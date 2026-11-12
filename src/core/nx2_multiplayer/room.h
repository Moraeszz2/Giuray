// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <array>
#include <atomic>
#include <mutex>
#include <optional>
#include <span>
#include <string>
#include <thread>
#include <vector>

#include "common/common_types.h"

/// Room codes: the players find their public addresses with STUN, trade them through a public
/// message board (ntfy.sh, the room code is the topic) and then send to each other directly, which
/// opens most home routers both ways. The host learns every joining player's address; a joining
/// player learns the host's.
namespace Core::Nx2Multiplayer::Rooms {

struct Room {
    bool host = false;
    std::string code;
    std::mutex mutex;
    std::string public_address; ///< This player's "ip:port" as STUN sees it (from the game socket).
    std::vector<std::string> peer_addresses; ///< The others' (for a joining player, the host's).
    std::atomic<bool> stop{false};
    std::atomic<bool> connected{false}; ///< Packets come in: the board is looked at less.
    std::optional<std::thread> board;
};

/// A new room code: 6 letters and digits, no look-alikes.
std::string NewCode();

/// Starts trading addresses through the board on a thread.
void Start(Room& room);

/// Stops the board thread.
void Stop(Room& room);

/// A STUN binding request (20 bytes) and the address in a binding response, if `data` is one.
std::array<u8, 20> StunRequest();
std::optional<std::string> StunAddress(std::span<const u8> data);

} // namespace Core::Nx2Multiplayer::Rooms
