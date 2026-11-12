// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include "common/common_types.h"

namespace Kernel {
class KProcess;
}

/// The game patches. Each checks the instructions it replaces and the cave it writes to first, and
/// refuses (logging why) on another game build.
namespace Core::Nx2Multiplayer::Patches {

/// The draw detour (Navis drawn where the multiplayer puts them), the AS play filter and the
/// dispatch hook (teleports and actor creation on a game thread).
bool InstallCore(Kernel::KProcess& process, u64 base);

/// The remote Links read their fake controllers. Fails until the game has its controller.
bool InstallInput(Kernel::KProcess& process, u64 base);

/// The other Navis get no AI.
bool InstallNaviFilter(Kernel::KProcess& process, u64 base);

} // namespace Core::Nx2Multiplayer::Patches
