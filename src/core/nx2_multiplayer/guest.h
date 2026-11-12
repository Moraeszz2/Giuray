// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <algorithm>
#include <array>
#include <cstring>
#include <optional>
#include <span>
#include <string>
#include <string_view>

#include "common/common_types.h"
#include "core/memory.h"
#include "core/nx2_multiplayer/layout.h"
#include "core/nx2_multiplayer/multiplayer.h"

namespace Kernel {
class KProcess;
}

/// Reading the game's memory: actors, Link, the camera.
namespace Core::Nx2Multiplayer {

/// The main module's code starts at the second executable code mapping (the first is rtld).
u64 FindMainBase(Kernel::KProcess& process);

/// Reads like the debug server does (GetPointerSilent: no commit, log or GPU download).
template <typename T>
bool Read(Core::Memory::Memory& memory, u64 address, T& out) {
    u8* const bytes = reinterpret_cast<u8*>(&out);
    for (size_t offset = 0; offset < sizeof(T);) {
        const u64 current = address + offset;
        if ((current >> 48) != 0) {
            return false;
        }
        const size_t length =
            (std::min)(static_cast<size_t>(Core::Memory::YUZU_PAGESIZE -
                                           (current & Core::Memory::YUZU_PAGEMASK)),
                       sizeof(T) - offset);
        const u8* const source = memory.GetPointerSilent(current);
        if (source == nullptr) {
            return false;
        }
        std::memcpy(bytes + offset, source, length);
        offset += length;
    }
    return true;
}

/// Finite and not absurdly far: a position that was really read.
bool Sane(const std::array<float, 3>& v);

/// The actor's name, e.g. "Player_Child".
std::string NameOf(Core::Memory::Memory& memory, u64 actor);

/// Whether `actor` is an actor named `expected`.
bool IsNamed(Core::Memory::Memory& memory, u64 base, u64 actor, std::string_view expected);
bool IsLink(Core::Memory::Memory& memory, u64 base, u64 actor);
bool IsNavi(Core::Memory::Memory& memory, u64 base, u64 actor);

/// The AS in an actor's body animation slot.
std::string ReadAction(Core::Memory::Memory& memory, u64 actor);

/// Reads the local Link. `pinned` keeps the first Link found: a second Player_Child takes over
/// the pointer chains, and the local Link must stay the one already followed. The remote players'
/// Links (`others`) are never taken for the local one.
std::optional<Player> ReadLink(Core::Memory::Memory& memory, u64 base, u64& pinned,
                               std::span<const u64> others);

/// The newest Link: a Player_Child created after the local one takes over Link's pointer chains.
u64 ChainLink(Core::Memory::Memory& memory, u64 base);

/// The camera's eye and target, if a camera chain points near Link.
bool ReadCamera(Core::Memory::Memory& memory, u64 base, const Player& link, Frame& frame);

/// The physics component an actor's transform comes from; its world matrix (3 rows of 4 floats,
/// the position in the last column) is at + 0x80.
std::optional<u64> ComponentOf(Core::Memory::Memory& memory, u64 actor);

/// The ControllerMgr's player controller.
std::optional<u64> PlayerController(Core::Memory::Memory& memory, u64 base);

} // namespace Core::Nx2Multiplayer
