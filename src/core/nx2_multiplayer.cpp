// SPDX-License-Identifier: GPL-3.0-or-later

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdlib>
#include <cstring>
#include <deque>
#include <optional>
#include <span>
#include <string>
#include <vector>

#include <boost/asio.hpp>
#include <fmt/format.h>

#include "common/common_types.h"
#include "common/logging.h"
#include "core/core.h"
#include "core/hle/kernel/k_page_table.h"
#include "core/hle/kernel/k_process.h"
#include "core/hle/result.h"
#include "core/hle/kernel/svc_types.h"
#include "core/memory.h"
#include "core/nx2_multiplayer.h"

namespace Core::Nx2Multiplayer {
namespace {

constexpr u64 OcarinaProgramId = 0x0400486024DEC000ULL;
constexpr u32 PacketMagic = 0x4D544F4F; // "OOTM"

struct Chain {
    u64 main_offset;
    std::vector<u64> offsets;
};

// Found with the debug server's pointer_scan and checked across boots (actor: two boots and a
// scene change; camera: one boot). Offsets are from the main module's first code page.
const std::vector<Chain> ActorChains{
    {0x4e98438, {0xb8, 0x40, 0x0}}, {0x4e6ba20, {0x78, 0x40, 0x0}},
    {0x4e44480, {0x78, 0x40, 0x0}}, {0x4e43b68, {0x38, 0x40, 0x0}},
    {0x4e21108, {0x340, 0x40, 0x0}}, {0x4e353b0, {0x78, 0x40, 0x0}},
};
const std::vector<Chain> CameraChains{
    {0x4e21668, {0x3f0}}, {0x4e95568, {0x390}}, {0x4e93a30, {0x2e8}},
    {0x4e93cb0, {0x2b0}}, {0x4e93d50, {0x278}}, {0x4e93718, {0x240}},
};
constexpr u64 ActorVtableOffset = 0x85929E18 - 0x80E04000;
constexpr u64 ActorRotation = 0x2F8;
constexpr u64 ActorPosition = 0x31C;

struct Packet {
    u32 magic;
    u32 sequence;
    float position[3];
    float facing[2];
    char name[16];
};

struct State {
    u64 process_id = ~0ULL;
    u64 main_base = 0;
    boost::asio::io_context io;
    std::optional<boost::asio::ip::udp::socket> socket;
    std::optional<boost::asio::ip::udp::endpoint> peer;
    u32 sequence = 0;
    Player remote;
    std::chrono::steady_clock::time_point remote_time{};
    std::deque<std::pair<std::chrono::steady_clock::time_point, Player>> ghost;
    bool ghost_mode = false;
    std::string network_status;
};

State& GetState() {
    static State state;
    return state;
}

const char* Env(const char* name) {
    const char* value = std::getenv(name);
    return value && *value ? value : nullptr;
}

/// The main module's code starts at the second executable code mapping (the first is rtld).
u64 FindMainBase(Kernel::KProcess& process) {
    auto& page_table = process.GetPageTable();
    u64 address = 0;
    int code_regions = 0;
    for (int guard = 0; guard < 100000; ++guard) {
        Kernel::KMemoryInfo info{};
        Kernel::Svc::PageInfo page_info{};
        if (R_FAILED(page_table.QueryInfo(std::addressof(info), std::addressof(page_info), address))) {
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

/// Reads like the debug server does (GetPointerSilent: no commit, log or GPU download).
template <typename T>
bool Read(Core::Memory::Memory& memory, u64 address, T& out) {
    u8* const bytes = reinterpret_cast<u8*>(&out);
    for (size_t offset = 0; offset < sizeof(T);) {
        const u64 current = address + offset;
        if ((current >> 48) != 0) {
            return false;
        }
        const size_t length = (std::min)(static_cast<size_t>(Core::Memory::YUZU_PAGESIZE -
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

std::optional<u64> Follow(Core::Memory::Memory& memory, u64 base, const Chain& chain) {
    u64 node = base + chain.main_offset;
    for (const u64 offset : chain.offsets) {
        u64 value;
        if (!Read(memory, node, value) || value == 0) {
            return std::nullopt;
        }
        node = value + offset;
    }
    return node;
}

bool Sane(const std::array<float, 3>& v) {
    return std::isfinite(v[0]) && std::isfinite(v[1]) && std::isfinite(v[2]) &&
           std::fabs(v[0]) < 1e5f && std::fabs(v[1]) < 1e5f && std::fabs(v[2]) < 1e5f;
}

std::optional<Player> ReadLink(Core::Memory::Memory& memory, u64 base) {
    for (const Chain& chain : ActorChains) {
        const auto actor = Follow(memory, base, chain);
        u64 vtable;
        if (!actor || !Read(memory, *actor, vtable) || vtable != base + ActorVtableOffset) {
            continue;
        }
        Player player{.valid = true};
        std::array<float, 9> rotation;
        if (!Read(memory, *actor + ActorPosition, player.position) ||
            !Read(memory, *actor + ActorRotation, rotation) || !Sane(player.position)) {
            continue;
        }
        // The third row is the actor's forward axis.
        player.facing = {rotation[6], rotation[8]};
        return player;
    }
    return std::nullopt;
}

bool ReadCamera(Core::Memory::Memory& memory, u64 base, const Player& link, Frame& frame) {
    for (const Chain& chain : CameraChains) {
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

void SetUpNetwork(State& state) {
    using boost::asio::ip::udp;
    state.ghost_mode = Env("EDEN_NX2_MP_GHOST") != nullptr;
    if (state.ghost_mode) {
        state.network_status = "ghost (local Link, 3 s late)";
        return;
    }
    const char* port_text = Env("EDEN_NX2_MP_PORT");
    const unsigned short port = static_cast<unsigned short>(port_text ? std::atoi(port_text) : 7777);
    try {
        state.socket.emplace(state.io, udp::endpoint(udp::v4(), port));
        state.socket->non_blocking(true);
        if (const char* peer = Env("EDEN_NX2_MP_PEER")) {
            const std::string text{peer};
            const auto colon = text.rfind(':');
            const std::string host = text.substr(0, colon);
            const std::string peer_port = colon == std::string::npos ? "7777" : text.substr(colon + 1);
            udp::resolver resolver{state.io};
            state.peer = *resolver.resolve(udp::v4(), host, peer_port).begin();
        }
        state.network_status = fmt::format("UDP {} -> {}", port, Env("EDEN_NX2_MP_PEER") ? Env("EDEN_NX2_MP_PEER") : "(no peer)");
    } catch (const std::exception& error) {
        state.network_status = fmt::format("network error: {}", error.what());
        state.socket.reset();
    }
    LOG_INFO(Core, "Nx2 multiplayer: {}", state.network_status);
}

void Exchange(State& state, const Player& local) {
    const auto now = std::chrono::steady_clock::now();
    if (state.ghost_mode) {
        state.ghost.emplace_back(now, local);
        while (!state.ghost.empty() && now - state.ghost.front().first > std::chrono::seconds{3}) {
            state.remote = state.ghost.front().second;
            state.remote.name = "Ghost";
            state.remote_time = now;
            state.ghost.pop_front();
        }
        return;
    }
    if (!state.socket) {
        return;
    }
    boost::system::error_code error;
    if (state.peer && local.valid) {
        Packet packet{PacketMagic, ++state.sequence,
                      {local.position[0], local.position[1], local.position[2]},
                      {local.facing[0], local.facing[1]}, {}};
        const char* name = Env("EDEN_NX2_MP_NAME");
        std::strncpy(packet.name, name ? name : "P1", sizeof(packet.name) - 1);
        state.socket->send_to(boost::asio::buffer(&packet, sizeof(packet)), *state.peer, 0, error);
    }
    for (;;) {
        Packet packet{};
        boost::asio::ip::udp::endpoint from;
        const size_t size = state.socket->receive_from(boost::asio::buffer(&packet, sizeof(packet)), from, 0, error);
        if (error || size != sizeof(packet)) {
            break;
        }
        if (packet.magic != PacketMagic) {
            continue;
        }
        packet.name[sizeof(packet.name) - 1] = '\0';
        state.remote = {.valid = true,
                        .position = {packet.position[0], packet.position[1], packet.position[2]},
                        .facing = {packet.facing[0], packet.facing[1]},
                        .name = packet.name};
        state.remote_time = now;
    }
    if (now - state.remote_time > std::chrono::seconds{2}) {
        state.remote.valid = false;
    }
}

} // Anonymous namespace

bool Enabled() {
    static const bool enabled = Env("EDEN_NX2_MP") != nullptr;
    return enabled;
}

Frame Tick(Core::System& system) {
    Frame frame;
    State& state = GetState();
    Kernel::KProcess* const process = system.IsPoweredOn() ? system.ApplicationProcess() : nullptr;
    if (process == nullptr || process->GetProgramId() != OcarinaProgramId) {
        frame.status = "multiplayer: waiting for Ocarina of Time";
        return frame;
    }
    if (process->GetProcessId() != state.process_id) {
        state.process_id = process->GetProcessId();
        state.main_base = FindMainBase(*process);
        LOG_INFO(Core, "Nx2 multiplayer: main module at {:#x}", state.main_base);
        if (!state.socket && !state.ghost_mode) {
            SetUpNetwork(state);
        }
    }
    auto& memory = process->GetMemory();
    const auto link = ReadLink(memory, state.main_base);
    if (link) {
        frame.local = *link;
    }
    Exchange(state, frame.local);
    frame.remote = state.remote;
    frame.valid = link && ReadCamera(memory, state.main_base, *link, frame);
    frame.status = fmt::format("multiplayer: {} | Link {} | camera {} | remote {}", state.network_status,
                               link ? "ok" : "not found", frame.valid ? "ok" : "not found",
                               frame.remote.valid ? "ok" : "none");
    return frame;
}

} // namespace Core::Nx2Multiplayer
