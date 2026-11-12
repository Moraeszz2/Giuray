// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <array>
#include <vector>

#include "common/common_types.h"

/// The ARM64 code the multiplayer patches write into the game. Every builder takes the absolute
/// guest addresses it is placed at and returns the words; nothing here touches the game.
namespace Core::Nx2Multiplayer::Code {

/// Remote players (and so second Links, their controllers and Navis) the patches handle.
constexpr size_t MaxRemotes = 3;

/// One entry of the draw table: a physics component and the world matrix drawn for it.
constexpr u64 DrawEntrySize = 0x40;
constexpr u64 DrawEntryMatrix = 0x10;

constexpr u32 Ret = 0xD65F03C0;
constexpr u32 Pacibsp = 0xD503237F;

u32 Branch(u64 from, u64 to);
u32 BranchLink(u64 from, u64 to);
u32 LoadLiteral(u64 from, u64 literal, u32 rt);

/// The matrix getter detour: a component found in the draw table gets that entry's matrix.
/// `first` is the getter's first instruction, run for every other component.
std::vector<u32> DrawDetour(u64 at, u64 getter, u32 first, u64 table);

/// A place where the transform update copies a component's matrix rows (x27 + 0x80) to its
/// output (x21) itself: for a component in the draw table, the entry's rows are copied instead.
/// `original` is the replaced instruction (ldr q0, [x27, #0x80]); `resume` follows the copy.
std::vector<u32> CopySite(u64 at, u64 site, u64 resume, u32 original, u64 table);

/// The AS play filter: requests for an actor in `links` (MaxRemotes actor pointers) are refused
/// unless marked ours (+ 0x21 = 0x5A), so the remote Links' own AI does not play their AS.
std::vector<u32> PlayFilter(u64 at, u64 play, u64 links);

/// The player input load: for a Link in `links`, the matching fake controller is read instead of
/// the player controller.
std::vector<u32> InputLoad(u64 at, u64 site, u32 original, u64 links,
                           const std::array<u64, MaxRemotes>& controllers);

/// The dispatch hook: runs the three pending requests at `scratch` (AS play, teleport, actor
/// creation), then the replaced `blr x8`.
std::vector<u32> DispatchHook(u64 at, u64 dispatch, u32 original, u64 scratch, u64 play,
                              u64 teleport, u64 create);

/// The AI filter: another Npc_Navi than the scene's own one (at `main_navi`) gets no AI update,
/// except one per count of the u32 at `updates`.
std::vector<u32> NaviFilter(u64 at, u64 update, u32 first, u64 main_navi, u64 updates);

} // namespace Core::Nx2Multiplayer::Code
