// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <array>

#include "common/common_types.h"

/// Where things are in Ocarina of Time (0400486024DEC000). Offsets are from the main module's first
/// code page (see FindMainBase); they hold for the one game build the prototype was made with, and
/// every patch checks the instructions it replaces before writing.
namespace Core::Nx2Multiplayer::Layout {

constexpr u64 OcarinaProgramId = 0x0400486024DEC000ULL;

// Actors. Every actor has the same vtable; its name ("Player_Child", "Npc_Navi", ...) is a
// pointer at + 0x260.
constexpr u64 ActorVtable = 0x85929E18 - 0x80E04000;
constexpr u64 ActorName = 0x260;
constexpr u64 ActorPhysics = 0x270; ///< [+ 0x270] + 0x50 is the physics holder.
constexpr u64 ActorRotation = 0x2F8;
constexpr u64 ActorPosition = 0x31C;
constexpr u64 ActorPendingPosition = 0x358; ///< Moves an actor that has no physics body.

/// Pointer chains to the local Link and to the camera, found with the debug server's pointer_scan
/// and checked across boots.
struct Chain {
    u64 main_offset;
    std::array<u64, 3> offsets;
    size_t depth;
};
constexpr std::array<Chain, 6> LinkChains{{
    {0x4e98438, {0xb8, 0x40, 0x0}, 3},
    {0x4e6ba20, {0x78, 0x40, 0x0}, 3},
    {0x4e44480, {0x78, 0x40, 0x0}, 3},
    {0x4e43b68, {0x38, 0x40, 0x0}, 3},
    {0x4e21108, {0x340, 0x40, 0x0}, 3},
    {0x4e353b0, {0x78, 0x40, 0x0}, 3},
}};
constexpr std::array<Chain, 6> CameraChains{{
    {0x4e21668, {0x3f0}, 1},
    {0x4e95568, {0x390}, 1},
    {0x4e93a30, {0x2e8}, 1},
    {0x4e93cb0, {0x2b0}, 1},
    {0x4e93d50, {0x278}, 1},
    {0x4e93718, {0x240}, 1},
}};

// Game functions and globals the patches call or read.
constexpr u64 MatrixGetter = 0x136AA0;         ///< Physics component world matrix getter.
constexpr u64 AsPlay = 0x2022A0;               ///< What the AINB node ExecuteASPlay calls.
constexpr u64 CreateActor = 0x372A70;          ///< ActorMgr's create request.
constexpr u64 ForceSetPosition = 0x4C6630;     ///< The physics teleport (ExecuteActorForceSetPos).
constexpr u64 PlayerInputUpdate = 0x574AB0;    ///< Run for each Link.
constexpr u64 Dispatch = 0x18D6034;            ///< A `blr x8` the game passes all the time.
constexpr u64 AiUpdate = 0x2C23780;            ///< Actor AI update (x0 = actor).
constexpr u64 ActorMgrGot = 0x4C7F3E0;
constexpr u64 DefaultScaleGot = 0x4C7E0B8;
constexpr u64 ControllerMgrGot = 0x4C7EF30;
constexpr u64 GameDataMgrSlot = 0x4E842A8;

// The code cave: zero padding at the end of the main module's .text. The guest only reads it.
constexpr u64 Cave = 0x3C46400;
constexpr u64 DrawDetourCave = Cave;           ///< The matrix getter detour (0x40 bytes).
constexpr u64 DrawTable = Cave + 0x100;        ///< Code::MaxRemotes entries (Navis drawn).
constexpr u64 CopySiteCaves = Cave + 0x200;    ///< Two 0x80-byte caves.
constexpr u64 CaveUsedEnd = Cave + 0x300;      ///< The draw patches use the cave up to here.
constexpr u64 PlayFilterCave = Cave + 0x680;
constexpr u64 InputCaves = Cave + 0x800;       ///< Two 0x80-byte caves.
constexpr u64 DispatchHookCave = Cave + 0x900; ///< 0xC0 bytes.
constexpr u64 NaviFilterCave = Cave + 0xA00;

// The scratch area: unused read-write memory after the main module's .bss (the rest of its page).
// It starts with three pending request pointers the dispatch hook takes and runs on a game thread.
constexpr u64 Scratch = 0x4E9A500;
constexpr u64 PendingPlay = Scratch;             ///< An AS play request (not used any more).
constexpr u64 PendingTeleport = Scratch + 0x8;   ///< Request at TeleportRequest.
constexpr u64 PendingCreate = Scratch + 0x10;    ///< Request at CreateRequest.
constexpr u64 TeleportRequest = Scratch + 0xC0;  ///< Physics holder, position, NaN.
constexpr u64 MainNaviSlot = Scratch + 0x100;    ///< The scene's own Navi, for the Navi filter.
constexpr u64 NaviUpdatesSlot = Scratch + 0x108; ///< AI updates let through to the other Navis.
constexpr u64 RemoteLinks = Scratch + 0x110;     ///< Code::MaxRemotes remote Link actors.
constexpr u64 CreateRequest = Scratch + 0x140;   ///< 0xD0 bytes.
constexpr u64 FakeControllers = Scratch + 0x240; ///< One per remote Link, 0x200 apart.
constexpr u64 FakeControllerStride = 0x200;

// The ControllerMgr's player controller: its pad state (held, triggered and released buttons,
// repeat counters, sticks) is bytes + 0x8 to + 0x140, the left stick at + 0x120.
constexpr u64 ControllerSize = 0x1F0;
constexpr u64 PadStateStart = 0x8;
constexpr u64 LeftStick = 0x120;

// The GameDataMgr keeps one table per data type; the Bool table's entry count is at + 0x10 and
// its entries at [+ 0x18]. A Bool entry is 0x18 bytes: + 0xC is 1 once the flag has been
// written, the value is at + 0x16 (and again at + 0x17).
constexpr u64 BoolCount = 0x10;
constexpr u64 BoolEntries = 0x18;
constexpr u64 BoolEntrySize = 0x18;
constexpr u64 BoolWritten = 0xC;
constexpr u64 BoolValue = 0x16;

} // namespace Core::Nx2Multiplayer::Layout
