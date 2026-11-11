// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <array>
#include <string>

#include "common/common_types.h"

namespace Core {
class System;
}

/// Ocarina of Time multiplayer prototype (EDEN_NX2_MP=1). Each Tick reads the local Link and the
/// camera through pointer chains from the main module, sends Link's state by UDP to
/// EDEN_NX2_MP_PEER (ip:port) and receives the peer's on EDEN_NX2_MP_PORT (default 7777).
/// EDEN_NX2_MP_NAME names the local player for the peer (default "P1"). EDEN_NX2_MP_GHOST=1
/// replays the local Link three seconds late as the remote player instead.
namespace Core::Nx2Multiplayer {

struct Player {
    bool valid{};
    std::array<float, 3> position{};
    /// Facing direction on the ground plane (X, Z), from the actor's rotation.
    std::array<float, 2> facing{};
    /// The AS (animation sequence) Link plays, e.g. "Move" or "Rolling"; empty if unknown.
    std::string action;
    /// The game's player controller state (EDEN_NX2_MP_INPUT=1): bytes + 0x8 to + 0x140 of the
    /// ControllerMgr's player controller (held, triggered and released buttons, repeat counters,
    /// sticks). Empty when not read.
    std::array<u8, 0x138> pad{};
    bool has_pad{};
    /// The player's camera forward direction on the ground plane (X, Z): sticks are relative to it.
    std::array<float, 2> camera{0.0f, 1.0f};
    std::string name;
};

struct Frame {
    bool valid{};
    std::array<float, 3> eye{};
    std::array<float, 3> at{};
    Player local;
    Player remote;
    std::string status;
};

bool Enabled();

/// Call from one thread only, about 30 times a second while a game runs.
Frame Tick(Core::System& system);

/// Stops the background actor scan; call before the game's memory goes away.
void Shutdown();

} // namespace Core::Nx2Multiplayer
