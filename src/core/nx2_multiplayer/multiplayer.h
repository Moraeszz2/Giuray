// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <array>
#include <optional>
#include <string>
#include <vector>

#include "common/common_types.h"

namespace Core {
class System;
}

/// Ocarina of Time multiplayer, up to four players: each other player is a Link in this player's
/// world, driven by their controller, with progress (chests, doors, events) shared between all.
///
/// Turned on from the Multiplayer window (StartRoom), or by the environment:
/// - EDEN_NX2_MP=1 turns it on;
/// - EDEN_NX2_MP_ROOM=HOST hosts a room, EDEN_NX2_MP_ROOM=<code> joins one;
/// - without a room, EDEN_NX2_MP_PEER=ip:port joins that host directly (else this player hosts
///   and answers whoever sends to it), on UDP EDEN_NX2_MP_PORT (default 7777);
/// - EDEN_NX2_MP_NAME names this player (default "P1");
/// - EDEN_NX2_MP_SYNC=0 turns progress sync off, EDEN_NX2_MP_NAVI=0 the other Links' Navis.
namespace Core::Nx2Multiplayer {

struct Player {
    bool valid{};
    std::array<float, 3> position{};
    /// Facing direction on the ground plane (X, Z), from the actor's rotation.
    std::array<float, 2> facing{};
    /// The AS (animation sequence) Link plays, e.g. "Move" or "Rolling"; empty if unknown.
    std::string action;
    /// The player controller's pad state (Layout::PadStateStart onwards); see has_pad.
    std::array<u8, 0x138> pad{};
    bool has_pad{};
    /// The player's camera forward direction on the ground plane (X, Z): sticks are relative to it.
    std::array<float, 2> camera{0.0f, 1.0f};
    std::string name;
};

/// Another player as the overlay shows them.
struct RemoteView {
    std::string name;
    /// Where their Link stands here (its body), when it has been created.
    std::optional<std::array<float, 3>> body;
};

struct Frame {
    bool valid{}; ///< The camera (eye, at) was found.
    std::array<float, 3> eye{};
    std::array<float, 3> at{};
    Player local;
    std::vector<RemoteView> remotes; ///< The players connected now.
    std::string status;
};

bool Enabled();

/// Hosts a room (`code` empty) or joins one: turns multiplayer on for this run. Call from the
/// thread that ticks.
void StartRoom(const std::string& code, const std::string& name);

/// Leaves the room (or stops hosting); the other players' Links stay where they stand.
void LeaveRoom();

/// Whether this player is in a room (or hosting), or has asked to be.
bool InRoom();

/// The room's code (empty without a room) and the network status line.
std::string RoomCode();
std::string NetworkStatus();

/// Whether the network status means connected to someone.
bool Connected();

/// The names of the other players connected now.
std::vector<std::string> PlayerNames();

/// Call from one thread only, about 30 times a second while a game runs.
Frame Tick(Core::System& system);

/// Stops the background actor scan; call before the game's memory goes away.
void Shutdown();

} // namespace Core::Nx2Multiplayer
