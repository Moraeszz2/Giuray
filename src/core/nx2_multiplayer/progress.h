// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include "common/common_types.h"
#include "core/nx2_multiplayer/state.h"

namespace Core::Memory {
class Memory;
}

/// Progress sync: every 250 ms the GameDataMgr's Bool flags that turned on are sent to the other
/// players, and the ones received are turned on here. Only off -> on: progress does not go back,
/// and scene-temporary flags that turn off again are not echoed. A flag of the scene a player
/// stands in takes effect when it is entered again.
namespace Core::Nx2Multiplayer::Progress {

void Sync(State& state, Core::Memory::Memory& memory, u64 base);

} // namespace Core::Nx2Multiplayer::Progress
