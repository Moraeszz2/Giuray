// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <array>
#include <string_view>

#include "common/common_types.h"
#include "core/nx2_multiplayer/state.h"

namespace Kernel {
class KProcess;
}

namespace Core::Memory {
class Memory;
}

/// The remote players' Links: created in this player's world, driven by their controllers.
namespace Core::Nx2Multiplayer::RemoteLinks {

/// Reads the local player controller's pad state into `local`.
void ReadLocalPad(Core::Memory::Memory& memory, u64 base, Player& local);

/// Queues ActorMgr's create request (an actor by name at a position) for the dispatch hook. False
/// while an earlier request is still pending.
bool QueueCreate(Core::Memory::Memory& memory, u64 base, std::string_view actor_name,
                 const std::array<float, 3>& position);

/// Creates, drives and corrects each remote player's Link; `link` is the local Link.
void Update(State& state, Kernel::KProcess& process, u64 base, const Player& link,
            const Frame& frame);

/// A remote Link's body position (its physics body), if it has one.
std::optional<std::array<float, 3>> BodyOf(Core::Memory::Memory& memory, u64 actor);

} // namespace Core::Nx2Multiplayer::RemoteLinks
