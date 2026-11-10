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
#include "core/arm/debug.h"
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
    bool puppet_installed = false;
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

// Puppet (EDEN_NX2_MP_PUPPET=1): a detour at the start of the physics component's world matrix
// getter (main + 0x136AA0, called by the actor transform update at main + 0x136AE4). For the
// component stored in the control block, it returns the control block's matrix instead, so that
// actor is drawn where the remote player is. The code and the control block live in the zero
// padding at the end of the main module's .text; the guest only reads them.
constexpr u64 GetterOffset = 0x136AA0;
constexpr u32 GetterFirstInstruction = 0xF9412808; // ldr x8, [x0, #0x250]
constexpr u32 GetterSecondInstruction = 0x1E2E1001; // fmov s1, #1.0
constexpr u64 CaveOffset = 0x3C46400;
constexpr u64 ControlOffset = CaveOffset + 0x40; // u64 component, u64 pad, 3 rows of 4 floats

u32 Branch(u64 from, u64 to) {
    return 0x14000000 | ((static_cast<u32>((to - from) >> 2)) & 0x3FFFFFF);
}

bool InstallPuppetDetour(Kernel::KProcess& process, u64 base) {
    auto& memory = process.GetMemory();
    const u64 getter = base + GetterOffset;
    const u64 cave = base + CaveOffset;
    u32 first = 0;
    u32 second = 0;
    std::array<u8, 0x100> cave_bytes{};
    if (!Read(memory, getter, first) || !Read(memory, getter + 4, second) ||
        !Read(memory, cave, cave_bytes)) {
        return false;
    }
    if (first == Branch(getter, cave)) {
        return true;
    }
    if (first != GetterFirstInstruction || second != GetterSecondInstruction ||
        std::ranges::any_of(cave_bytes, [](u8 byte) { return byte != 0; })) {
        LOG_ERROR(Core, "Nx2 multiplayer: the puppet detour does not match this game build");
        return false;
    }
    const u64 control = base + ControlOffset;
    const u32 adr_offset = static_cast<u32>(control - cave);
    const std::array<u32, 7> code{
        // adr x16, control
        0x10000010 | ((adr_offset & 3) << 29) | (((adr_offset >> 2) & 0x7FFFF) << 5),
        0xF9400211, // ldr x17, [x16]
        0xEB00023F, // cmp x17, x0
        0x54000061, // b.ne +12 (to the original first instruction)
        0x91004200, // add x0, x16, #16
        0xD65F03C0, // ret
        GetterFirstInstruction,
    };
    const u32 back = Branch(cave + code.size() * 4, getter + 4);
    u8* const cave_host = memory.GetPointerSilent(cave);
    u8* const getter_host = memory.GetPointerSilent(getter);
    if (cave_host == nullptr || getter_host == nullptr) {
        return false;
    }
    std::memcpy(cave_host, code.data(), code.size() * 4);
    std::memcpy(cave_host + code.size() * 4, &back, 4);

    // The transform update also copies the component's matrix (x27 + 0x80, three rows) to its
    // output (x21) itself at two places: for the puppet, copy the control block's rows instead.
    constexpr std::array<std::pair<u64, u64>, 2> copy_sites{{
        {0x137858, 0x137870}, // ldr q0, [x27, #0x80] ... str q0, [x21, #0x20]
        {0x1382CC, 0x1382E4},
    }};
    for (size_t index = 0; index < copy_sites.size(); ++index) {
        const u64 site = base + copy_sites[index].first;
        const u64 resume = base + copy_sites[index].second;
        const u64 site_cave = cave + 0x80 + index * 0x40;
        u32 original = 0;
        if (!Read(memory, site, original) || original != 0x3DC02360) {
            LOG_ERROR(Core, "Nx2 multiplayer: copy site {:#x} does not match this game build", site);
            return false;
        }
        const auto adr_x17 = [&](u64 at, u64 target) {
            const u32 offset = static_cast<u32>(target - at);
            return 0x10000011u | ((offset & 3) << 29) | (((offset >> 2) & 0x7FFFF) << 5);
        };
        std::array<u32, 14> site_code{
            adr_x17(site_cave, control),
            0xF9400231, // ldr x17, [x17]
            0xEB1B023F, // cmp x17, x27
            0x54000121, // b.ne +36 (to the original load)
            adr_x17(site_cave + 16, control + 16),
            0x3DC00220, // ldr q0, [x17]
            0x3D8002A0, // str q0, [x21]
            0x3DC00620, // ldr q0, [x17, #16]
            0x3D8006A0, // str q0, [x21, #16]
            0x3DC00A20, // ldr q0, [x17, #32]
            0x3D800AA0, // str q0, [x21, #32]
            Branch(site_cave + 44, resume),
            0x3DC02360, // ldr q0, [x27, #0x80]
            Branch(site_cave + 52, site + 4),
        };
        u8* const site_cave_host = memory.GetPointerSilent(site_cave);
        u8* const site_host = memory.GetPointerSilent(site);
        if (site_cave_host == nullptr || site_host == nullptr) {
            return false;
        }
        std::memcpy(site_cave_host, site_code.data(), sizeof(site_code));
        const u32 jump = Branch(site, site_cave);
        std::memcpy(site_host, &jump, 4);
        Core::InvalidateInstructionCacheRange(&process, site, 4);
    }

    const u32 detour = Branch(getter, cave);
    std::memcpy(getter_host, &detour, 4);
    Core::InvalidateInstructionCacheRange(&process, getter, 4);
    Core::InvalidateInstructionCacheRange(&process, cave, 0x100);
    LOG_INFO(Core, "Nx2 multiplayer: puppet detour installed at {:#x} (cave {:#x})", getter, cave);
    return true;
}

/// The control block's matrix: the remote player's position, facing its direction.
void WritePuppet(Kernel::KProcess& process, u64 base, const Player& remote) {
    u8* const control = process.GetMemory().GetPointerSilent(base + ControlOffset);
    if (control == nullptr || !remote.valid) {
        return;
    }
    float fx = remote.facing[0];
    float fz = remote.facing[1];
    const float length = std::sqrt(fx * fx + fz * fz);
    if (length < 0.01f) {
        fx = 0;
        fz = 1;
    } else {
        fx /= length;
        fz /= length;
    }
    // Rows of a 3x4 world matrix; the third row is the forward axis (as in the actor rotation).
    const std::array<float, 12> matrix{
        fz,  0, -fx, remote.position[0],
        0,   1, 0,   remote.position[1],
        fx,  0, fz,  remote.position[2],
    };
    std::memcpy(control + 16, matrix.data(), sizeof(matrix));
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
        state.puppet_installed = false;
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
    if (Env("EDEN_NX2_MP_PUPPET")) {
        if (!state.puppet_installed) {
            state.puppet_installed = InstallPuppetDetour(*process, state.main_base);
        }
        if (state.puppet_installed) {
            WritePuppet(*process, state.main_base, frame.remote);
        }
    }
    frame.valid = link && ReadCamera(memory, state.main_base, *link, frame);
    frame.status = fmt::format("multiplayer: {} | Link {} | camera {} | remote {}", state.network_status,
                               link ? "ok" : "not found", frame.valid ? "ok" : "not found",
                               frame.remote.valid ? "ok" : "none");
    return frame;
}

} // namespace Core::Nx2Multiplayer
