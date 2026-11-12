// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include "common/common_types.h"
#include "core/nx2_multiplayer/state.h"

namespace Kernel {
class KProcess;
}

/// The remote Links' Navis, ornaments: an Npc_Navi each whose AI never runs (two Navis corrupt the
/// list ExecuteNaviPushMovementInfo keeps for the one Navi), drawn over its Link's shoulder.
namespace Core::Nx2Multiplayer::Navis {

void Update(State& state, Kernel::KProcess& process, u64 base);

/// Stops the background actor scan; call before the game's memory goes away.
void StopScan(State& state);

} // namespace Core::Nx2Multiplayer::Navis
