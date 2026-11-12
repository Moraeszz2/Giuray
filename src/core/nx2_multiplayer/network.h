// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <span>

#include "common/common_types.h"
#include "core/nx2_multiplayer/state.h"

/// The players' UDP traffic. The host answers whoever joins (up to MaxRemotes players) and relays
/// each player's packets and flags to the others; a joining player sends to the host only.
namespace Core::Nx2Multiplayer::Network {

/// Opens the socket and the room from the environment (see multiplayer.h).
void Open(State& state);

/// Leaves: closes the socket and the room, forgets the other players.
void Close(State& state);

/// Sends the local Link, receives the others' Links and flags.
void Exchange(State& state, const Player& local);

/// Sends Bool flag indices to every other player (but `except`, the one they came from).
void QueueFlags(State& state, std::span<const u16> indices, const Endpoint* except);

} // namespace Core::Nx2Multiplayer::Network
