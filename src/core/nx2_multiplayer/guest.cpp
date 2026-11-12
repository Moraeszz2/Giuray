// SPDX-License-Identifier: GPL-3.0-or-later

#include <cmath>

#include "core/hle/kernel/k_page_table.h"
#include "core/hle/kernel/k_process.h"
#include "core/hle/kernel/svc_types.h"
#include "core/hle/result.h"
#include "core/nx2_multiplayer/guest.h"

namespace Core::Nx2Multiplayer {
namespace {

std::optional<u64> Follow(Core::Memory::Memory& memory, u64 base, const Layout::Chain& chain) {
    u64 node = base + chain.main_offset;
    for (size_t i = 0; i < chain.depth; ++i) {
        u64 value;
        if (!Read(memory, node, value) || value == 0) {
            return std::nullopt;
        }
        node = value + chain.offsets[i];
    }
    return node;
}

bool IsActor(Core::Memory::Memory& memory, u64 base, u64 actor) {
    u64 vtable;
    return actor != 0 && Read(memory, actor, vtable) && vtable == base + Layout::ActorVtable;
}

} // Anonymous namespace

u64 FindMainBase(Kernel::KProcess& process) {
    auto& page_table = process.GetPageTable();
    u64 address = 0;
    int code_regions = 0;
    for (int guard = 0; guard < 100000; ++guard) {
        Kernel::KMemoryInfo info{};
        Kernel::Svc::PageInfo page_info{};
        if (R_FAILED(
                page_table.QueryInfo(std::addressof(info), std::addressof(page_info), address))) {
            return 0;
        }
        const auto svc = info.GetSvcMemoryInfo();
        if (svc.state == Kernel::Svc::MemoryState::Code &&
            svc.permission == Kernel::Svc::MemoryPermission::ReadExecute && ++code_regions == 2) {
            return svc.base_address;
        }
        const u64 next = svc.base_address + svc.size;
        if (next <= address) {
            return 0;
        }
        address = next;
    }
    return 0;
}

bool Sane(const std::array<float, 3>& v) {
    return std::isfinite(v[0]) && std::isfinite(v[1]) && std::isfinite(v[2]) &&
           std::fabs(v[0]) < 1e5f && std::fabs(v[1]) < 1e5f && std::fabs(v[2]) < 1e5f;
}

std::string NameOf(Core::Memory::Memory& memory, u64 actor) {
    u64 pointer = 0;
    std::array<char, 64> text{};
    if (!Read(memory, actor + Layout::ActorName, pointer) || pointer == 0 ||
        !Read(memory, pointer, text)) {
        return {};
    }
    text.back() = '\0';
    return std::string{text.data()};
}

bool IsNamed(Core::Memory::Memory& memory, u64 base, u64 actor, std::string_view expected) {
    u64 name_pointer;
    std::array<char, 16> name{};
    return IsActor(memory, base, actor) && Read(memory, actor + Layout::ActorName, name_pointer) &&
           name_pointer != 0 && Read(memory, name_pointer, name) && expected.size() < name.size() &&
           std::memcmp(name.data(), expected.data(), expected.size() + 1) == 0;
}

bool IsLink(Core::Memory::Memory& memory, u64 base, u64 actor) {
    return IsNamed(memory, base, actor, "Player_Child");
}

bool IsNavi(Core::Memory::Memory& memory, u64 base, u64 actor) {
    return IsNamed(memory, base, actor, "Npc_Navi");
}

/// The AS player the play call uses is actor + 0x270 -> + 0x40 -> + 0x18 -> + 0x18; its slots
/// (0x48 bytes each) start at + 0x20, and a slot's AS name is at + 0x10.
std::string ReadAction(Core::Memory::Memory& memory, u64 actor) {
    u64 node = actor + Layout::ActorPhysics;
    for (const u64 offset : {0x40, 0x18, 0x18, 0x20, 0x0, 0x10}) {
        u64 value;
        if (!Read(memory, node, value) || value == 0) {
            return {};
        }
        node = value + offset;
    }
    u64 text;
    std::array<char, 32> name{};
    if (!Read(memory, node, text) || text == 0 || !Read(memory, text, name)) {
        return {};
    }
    name.back() = '\0';
    return name.data();
}

std::optional<Player> ReadLink(Core::Memory::Memory& memory, u64 base, u64& pinned,
                               std::span<const u64> others) {
    if (pinned != 0 && !IsLink(memory, base, pinned)) {
        pinned = 0;
    }
    for (const auto& chain : Layout::LinkChains) {
        auto actor = Follow(memory, base, chain);
        if (pinned != 0) {
            actor = pinned;
        }
        if (!actor || std::ranges::find(others, *actor) != others.end() ||
            !IsActor(memory, base, *actor)) {
            continue;
        }
        pinned = *actor;
        Player player{.valid = true};
        std::array<float, 9> rotation;
        if (!Read(memory, *actor + Layout::ActorPosition, player.position) ||
            !Read(memory, *actor + Layout::ActorRotation, rotation) || !Sane(player.position)) {
            continue;
        }
        // The third column is the actor's forward (+Z) axis.
        player.facing = {rotation[2], rotation[8]};
        player.action = ReadAction(memory, *actor);
        return player;
    }
    return std::nullopt;
}

u64 ChainLink(Core::Memory::Memory& memory, u64 base) {
    for (const auto& chain : Layout::LinkChains) {
        const auto actor = Follow(memory, base, chain);
        if (actor && IsLink(memory, base, *actor)) {
            return *actor;
        }
    }
    return 0;
}

bool ReadCamera(Core::Memory::Memory& memory, u64 base, const Player& link, Frame& frame) {
    for (const auto& chain : Layout::CameraChains) {
        const auto camera = Follow(memory, base, chain);
        if (!camera || !Read(memory, *camera, frame.eye) || !Read(memory, *camera + 12, frame.at) ||
            !Sane(frame.eye) || !Sane(frame.at)) {
            continue;
        }
        float distance = 0;
        for (int i = 0; i < 3; ++i) {
            distance += (frame.eye[i] - link.position[i]) * (frame.eye[i] - link.position[i]);
        }
        // A camera chain that went stale points at something far from Link.
        if (std::sqrt(distance) < 60.0f) {
            return true;
        }
    }
    return false;
}

/// [[[[actor + 0x270] + 0x50] + 0x20] + 0x190], if its matrix looks like an upright body.
std::optional<u64> ComponentOf(Core::Memory::Memory& memory, u64 actor) {
    u64 node = actor;
    for (const u64 offset : {0x270ULL, 0x50ULL, 0x20ULL, 0x190ULL}) {
        if (!Read(memory, node + offset, node) || node == 0) {
            return std::nullopt;
        }
    }
    std::array<float, 12> matrix;
    if (!Read(memory, node + 0x80, matrix) || !Sane({matrix[3], matrix[7], matrix[11]}) ||
        std::fabs(matrix[5] - 1.0f) > 0.2f) {
        return std::nullopt;
    }
    return node;
}

std::optional<u64> PlayerController(Core::Memory::Memory& memory, u64 base) {
    u64 got;
    u64 manager;
    u64 controller;
    if (!Read(memory, base + Layout::ControllerMgrGot, got) || !Read(memory, got, manager) ||
        manager == 0 || !Read(memory, manager + 0xB0, controller) || controller == 0) {
        return std::nullopt;
    }
    return controller;
}

} // namespace Core::Nx2Multiplayer
