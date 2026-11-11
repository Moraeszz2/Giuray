// SPDX-License-Identifier: GPL-3.0-or-later

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cmath>
#include <cstdlib>
#include <cstring>
#include <deque>
#include <mutex>
#include <optional>
#include <thread>
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
    char action[32];
    u8 has_pad;
    u8 pad[0x138];
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
    // Puppet selection: a background scan of the heap for actors (their vtable), refreshed after
    // a scene change; the nearest usable one becomes the puppet.
    std::mutex scan_mutex;
    std::vector<u64> scanned_actors;
    std::optional<std::thread> scanner;
    std::atomic<bool> scanning{false};
    std::atomic<bool> stop_scan{false};
    std::chrono::steady_clock::time_point last_scan{};
    std::array<float, 3> scan_origin{};
    u64 puppet_actor = 0;
    u64 puppet_component = 0;
    bool puppet_preferred = false;
    // Animation: the AS last queued for the puppet (-1 none) and the remote motion it is chosen by.
    std::string puppet_as;
    u64 puppet_as_actor = 0;
    u64 local_actor = 0;
    bool input_hooked = false;
    bool puppet_as_started = false;
    std::array<float, 3> last_remote{};
    std::chrono::steady_clock::time_point last_remote_time{};
    float remote_speed = 0;
    std::chrono::steady_clock::time_point last_moving_time{};
    u64 scan_generation = 0;
    u64 used_generation = 0;
    std::string network_status;
};

State& GetState() {
    // Never destroyed: a scanner thread still running at exit must not be joined from a static
    // destructor (Tick stops it when the game closes).
    static State* const state = new State;
    return *state;
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

/// The name of the AS in an actor's first AS slot (its body animation): actor + 0x270 -> + 0x40
/// -> + 0x18 -> + 0x18 is the AS player the play call uses, whose slots (0x48 bytes each) start at
/// + 0x20; a slot's AS name is at + 0x10.
std::string ReadAction(Core::Memory::Memory& memory, u64 actor) {
    u64 node = actor + 0x270;
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

/// Whether an actor is a Link (its name, at actor + 0x260, is "Player_Child").
bool IsLink(Core::Memory::Memory& memory, u64 base, u64 actor) {
    u64 vtable;
    u64 name_pointer;
    std::array<char, 13> name{};
    return actor != 0 && Read(memory, actor, vtable) && vtable == base + ActorVtableOffset &&
           Read(memory, actor + 0x260, name_pointer) && name_pointer != 0 &&
           Read(memory, name_pointer, name) && std::memcmp(name.data(), "Player_Child", 13) == 0;
}

/// Reads the local Link. `pinned` keeps the first Link found: a second Player_Child created as a
/// puppet takes over the pointer chains, and the local Link must stay the one already followed.
std::optional<Player> ReadLink(Core::Memory::Memory& memory, u64 base, u64& pinned) {
    if (pinned != 0 && !IsLink(memory, base, pinned)) {
        pinned = 0;
    }
    for (const Chain& chain : ActorChains) {
        auto actor = Follow(memory, base, chain);
        if (pinned != 0) {
            actor = pinned;
        }
        u64 vtable;
        if (!actor || !Read(memory, *actor, vtable) || vtable != base + ActorVtableOffset) {
            continue;
        }
        pinned = *actor;
        Player player{.valid = true};
        std::array<float, 9> rotation;
        if (!Read(memory, *actor + ActorPosition, player.position) ||
            !Read(memory, *actor + ActorRotation, rotation) || !Sane(player.position)) {
            continue;
        }
        // The third column is the actor's forward (+Z) axis.
        player.facing = {rotation[2], rotation[8]};
        player.action = ReadAction(memory, *actor);
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
                      {local.facing[0], local.facing[1]}, {}, {}};
        const char* name = Env("EDEN_NX2_MP_NAME");
        std::strncpy(packet.name, name ? name : "P1", sizeof(packet.name) - 1);
        std::strncpy(packet.action, local.action.c_str(), sizeof(packet.action) - 1);
        packet.has_pad = local.has_pad ? 1 : 0;
        std::memcpy(packet.pad, local.pad.data(), sizeof(packet.pad));
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
        packet.action[sizeof(packet.action) - 1] = '\0';
        state.remote = {.valid = true,
                        .position = {packet.position[0], packet.position[1], packet.position[2]},
                        .facing = {packet.facing[0], packet.facing[1]},
                        .action = packet.action,
                        .name = packet.name};
        state.remote.has_pad = packet.has_pad != 0;
        std::memcpy(state.remote.pad.data(), packet.pad, sizeof(packet.pad));
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

// Puppet animation. The AS play call (main + 0x2022A0, what the AINB node ExecuteASPlay calls)
// takes a 0x30-byte request: +0x00 actor, +0x08 AS name, +0x14 blend frames, +0x18 -1.0 -1.0,
// +0x20 u8 1 = keep an AS of that name already playing,
// +0x21 u8 (zero from the game; 0x5A marks ours), +0x28 node (may be null).
// - A filter at its entry refuses requests for the puppet actor (control block + 8) that are not
//   ours, so its own AI no longer drives its animations.
// - A hook at a dispatch the game runs constantly (main + 0x18D6034, blr x8) swaps the pending
//   request pointer at the start of a scratch area after the main module's .bss (unused, mapped
//   read-write) with zero and, if there was one, plays it on that game thread.
constexpr u64 PlayOffset = 0x2022A0;
constexpr u32 PlayFirstInstruction = 0xD503237F; // pacibsp
constexpr u64 DispatchOffset = 0x18D6034;
constexpr u32 DispatchInstruction = 0xD63F0100; // blr x8
constexpr u64 HookCaveOffset = CaveOffset + 0x600;
constexpr u64 FilterCaveOffset = CaveOffset + 0x680;
constexpr u64 NamesOffset = CaveOffset + 0x700; // "Wait", "Move", "Jump", 16 bytes apart
constexpr u64 ScratchOffset = 0x4E9A500; // pending request pointer, request at + 0x40
constexpr u8 OurRequest = 0x5A;

u32 Literal(u64 from, u64 literal, u32 rt) {
    return 0x58000000 | ((static_cast<u32>((literal - from) >> 2) & 0x7FFFF) << 5) | rt;
}

u32 BranchLink(u64 from, u64 to) {
    return 0x94000000 | ((static_cast<u32>((to - from) >> 2)) & 0x3FFFFFF);
}

bool InstallAnimationHooks(Kernel::KProcess& process, u64 base) {
    auto& memory = process.GetMemory();
    const u64 play = base + PlayOffset;
    const u64 dispatch = base + DispatchOffset;
    const u64 hook = base + HookCaveOffset;
    const u64 filter = base + FilterCaveOffset;
    const u64 scratch = base + ScratchOffset;
    const u64 control = base + ControlOffset;
    u32 play_first = 0;
    u32 dispatch_instruction = 0;
    std::array<u8, 0x200> free_cave{};
    std::array<u8, 0x80> free_scratch{};
    if (!Read(memory, play, play_first) || !Read(memory, dispatch, dispatch_instruction) ||
        !Read(memory, hook, free_cave) || !Read(memory, scratch, free_scratch) ||
        play_first != PlayFirstInstruction || dispatch_instruction != DispatchInstruction ||
        std::ranges::any_of(free_cave, [](u8 byte) { return byte != 0; }) ||
        std::ranges::any_of(free_scratch, [](u8 byte) { return byte != 0; })) {
        LOG_ERROR(Core, "Nx2 multiplayer: the animation hooks do not match this game build");
        return false;
    }
    const std::array<u32, 16> hook_code{
        0xA9BC7BFD, // stp x29, x30, [sp, #-0x40]!
        0xA90107E0, // stp x0, x1, [sp, #0x10]
        0xA9020FE2, // stp x2, x3, [sp, #0x20]
        0xA90323E4, // stp x4, x8, [sp, #0x30]
        Literal(hook + 0x10, hook + 0x40, 16), // ldr x16, scratch
        0xAA1F03F1, // mov x17, xzr
        0xF8F18200, // swpal x17, x0, [x16]
        0xB4000040, // cbz x0, +8
        BranchLink(hook + 0x20, play),
        0xA94323E4, // ldp x4, x8, [sp, #0x30]
        0xA9420FE2, // ldp x2, x3, [sp, #0x20]
        0xA94107E0, // ldp x0, x1, [sp, #0x10]
        0xA8C47BFD, // ldp x29, x30, [sp], #0x40
        DispatchInstruction,
        Branch(hook + 0x38, dispatch + 4),
        0,
    };
    const std::array<u32, 12> filter_code{
        0xF9400010, // ldr x16, [x0]            (the request's actor)
        Literal(filter + 0x04, filter + 0x40, 17), // ldr x17, control
        0xF9400631, // ldr x17, [x17, #8]      (the puppet actor)
        0xEB11021F, // cmp x16, x17
        0x540000C1, // b.ne +24 (play it)
        0x39408410, // ldrb w16, [x0, #0x21]
        0x71016A1F, // cmp w16, #0x5a
        0x54000060, // b.eq +12 (ours: play it)
        0x2A1F03E0, // mov w0, wzr (the puppet's own AI: refused)
        0xD65F03C0, // ret
        PlayFirstInstruction,
        Branch(filter + 0x2C, play + 4),
    };
    u8* const hook_host = memory.GetPointerSilent(hook);
    u8* const filter_host = memory.GetPointerSilent(filter);
    u8* const names_host = memory.GetPointerSilent(base + NamesOffset);
    u8* const play_host = memory.GetPointerSilent(play);
    u8* const dispatch_host = memory.GetPointerSilent(dispatch);
    if (!hook_host || !filter_host || !names_host || !play_host || !dispatch_host) {
        return false;
    }
    std::memcpy(hook_host, hook_code.data(), sizeof(hook_code));
    std::memcpy(hook_host + 0x40, &scratch, sizeof(scratch));
    std::memcpy(filter_host, filter_code.data(), sizeof(filter_code));
    std::memcpy(filter_host + 0x40, &control, sizeof(control));
    std::memcpy(names_host + 0x00, "Wait", 5);
    std::memcpy(names_host + 0x10, "Move", 5);
    std::memcpy(names_host + 0x20, "Jump", 5);
    const u32 to_filter = Branch(play, filter);
    const u32 to_hook = Branch(dispatch, hook);
    std::memcpy(play_host, &to_filter, 4);
    std::memcpy(dispatch_host, &to_hook, 4);
    Core::InvalidateInstructionCacheRange(&process, play, 4);
    Core::InvalidateInstructionCacheRange(&process, dispatch, 4);
    Core::InvalidateInstructionCacheRange(&process, hook, 0x180);
    LOG_INFO(Core, "Nx2 multiplayer: animation hooks installed (play {:#x}, dispatch {:#x})", play,
             dispatch);
    return true;
}

/// Queues an AS for the puppet by name: the dispatch hook plays it on the next game thread that
/// passes there. The name is copied to the scratch area (+ 0x80), rewritten only when it changes.
void QueuePuppetAs(Kernel::KProcess& process, u64 base, u64 actor, const std::string& name,
                   float blend) {
    u8* const scratch = process.GetMemory().GetPointerSilent(base + ScratchOffset);
    if (scratch == nullptr || name.empty() || name.size() >= 32) {
        return;
    }
    char* const name_copy = reinterpret_cast<char*>(scratch + 0x80);
    if (name != name_copy) {
        std::memset(name_copy, 0, 32);
        std::memcpy(name_copy, name.data(), name.size());
    }
    std::array<u8, 0x30> request{};
    const u64 name_address = base + ScratchOffset + 0x80;
    const float unset = -1.0f;
    std::memcpy(request.data() + 0x00, &actor, 8);
    std::memcpy(request.data() + 0x08, &name_address, 8);
    std::memcpy(request.data() + 0x14, &blend, 4);
    std::memcpy(request.data() + 0x18, &unset, 4);
    std::memcpy(request.data() + 0x1C, &unset, 4);
    // +0x20 = 1: the play call leaves an AS of the same name that is already playing alone,
    // so asking every tick keeps it running instead of restarting it from its first frame.
    request[0x20] = 1;
    request[0x21] = OurRequest;
    std::memcpy(scratch + 0x40, request.data(), request.size());
    const u64 pending = base + ScratchOffset + 0x40;
    std::atomic_ref<u64>{*reinterpret_cast<u64*>(scratch)}.store(pending, std::memory_order_release);
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
    InstallAnimationHooks(process, base);
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
    // Rows of a 3x4 world matrix turned about Y: its third column, the forward axis, is the facing.
    const std::array<float, 12> matrix{
        fz,  0, fx, remote.position[0],
        0,   1, 0,  remote.position[1],
        -fx, 0, fz, remote.position[2],
    };
    std::memcpy(control + 16, matrix.data(), sizeof(matrix));
}

/// The physics component an actor's transform comes from: [[[[actor + 0x270] + 0x50] + 0x20] + 0x190].
std::optional<u64> ComponentOf(Core::Memory::Memory& memory, u64 actor) {
    u64 node = actor;
    for (const u64 offset : {0x270ULL, 0x50ULL, 0x20ULL, 0x190ULL}) {
        if (!Read(memory, node + offset, node) || node == 0) {
            return std::nullopt;
        }
    }
    std::array<float, 12> matrix;
    if (!Read(memory, node + 0x80, matrix) ||
        !Sane({matrix[3], matrix[7], matrix[11]}) || std::fabs(matrix[5] - 1.0f) > 0.2f) {
        return std::nullopt;
    }
    return node;
}

void StartScan(State& state, Kernel::KProcess& process, u64 base) {
    if (state.scanning.exchange(true)) {
        return;
    }
    if (state.scanner && state.scanner->joinable()) {
        state.scanner->join();
    }
    const u64 vtable = base + ActorVtableOffset;
    auto& page_table = process.GetPageTable();
    const u64 heap_start = GetInteger(page_table.GetHeapRegionStart());
    const u64 heap_end = heap_start + page_table.GetHeapRegionSize();
    Core::Memory::Memory* const memory = &process.GetMemory();
    state.scanner.emplace([&state, memory, vtable, heap_start, heap_end] {
        std::vector<u64> actors;
        for (u64 page = heap_start; page < heap_end; page += Core::Memory::YUZU_PAGESIZE) {
            const u8* const bytes = memory->GetPointerSilent(page);
            if (bytes == nullptr) {
                continue;
            }
            if (state.stop_scan) {
                break;
            }
            for (u64 offset = 0; offset < Core::Memory::YUZU_PAGESIZE; offset += 8) {
                u64 value;
                std::memcpy(&value, bytes + offset, sizeof(value));
                if (value == vtable) {
                    actors.push_back(page + offset);
                }
            }
        }
        {
            std::scoped_lock lock{state.scan_mutex};
            state.scanned_actors = std::move(actors);
            ++state.scan_generation;
        }
        state.scanning = false;
    });
}

/// The actor's name (actor + 0x260 points to it, e.g. "Player_Child").
std::string NameOf(Core::Memory::Memory& memory, u64 actor) {
    u64 pointer = 0;
    std::array<char, 64> text{};
    if (!Read(memory, actor + 0x260, pointer) || pointer == 0 || !Read(memory, pointer, text)) {
        return {};
    }
    text.back() = '\0';
    return std::string{text.data()};
}

/// The actor a puppet is preferably made of (EDEN_NX2_MP_PUPPET_NAME): Dark Link has Link's body
/// and animations.
const std::string& PreferredPuppet() {
    static const std::string name = [] {
        const char* value = Env("EDEN_NX2_MP_PUPPET_NAME");
        return std::string{value ? value : "Enemy_MiddleBoss_DarkLink"};
    }();
    return name;
}

/// EDEN_NX2_MP_INPUT=1: the puppet is a second Link driven by the remote player's controller
/// (player 2) instead of drawn where the remote player is.
bool InputMode() {
    static const bool enabled = [] {
        const char* value = Env("EDEN_NX2_MP_INPUT");
        return value && std::strcmp(value, "1") == 0;
    }();
    return enabled;
}

// The game's player input update (main + 0x574AB0, run for each Link with the Link's input
// object in x19, whose + 0x18 is the actor) reads the ControllerMgr's player controller
// (x22 + 0xB0) at two places. For the puppet (control block + 8) it reads a copy of that
// controller in the scratch area instead, whose state Lichii writes from the remote player's.
constexpr std::array<u64, 2> InputLoadOffsets{0x574B44, 0x574F20};
constexpr u32 InputLoadInstruction = 0xF9405AD4; // ldr x20, [x22, #0xb0]
constexpr u64 InputCaveOffset = CaveOffset + 0x800;
constexpr u64 ControllerMgrGot = 0x4C7EF30;
constexpr u64 FakePadOffset = ScratchOffset + 0x200; // 0x1F0 bytes
constexpr u64 PadSize = 0x1F0;
constexpr u64 PadStateStart = 0x8;

std::optional<u64> PlayerController(Core::Memory::Memory& memory, u64 base) {
    u64 got;
    u64 manager;
    u64 controller;
    if (!Read(memory, base + ControllerMgrGot, got) || !Read(memory, got, manager) ||
        manager == 0 || !Read(memory, manager + 0xB0, controller) || controller == 0) {
        return std::nullopt;
    }
    return controller;
}

bool InstallInputHook(Kernel::KProcess& process, u64 base) {
    auto& memory = process.GetMemory();
    const auto controller = PlayerController(memory, base);
    u8* const fake = memory.GetPointerSilent(base + FakePadOffset);
    if (!controller || fake == nullptr) {
        return false;
    }
    // The copy starts as the real controller (its vtable and links stay valid); only its pad
    // state is rewritten afterwards.
    std::array<u8, PadSize> original;
    if (!Read(memory, *controller, original)) {
        return false;
    }
    std::memcpy(fake, original.data(), PadSize);
    const u64 actor_slot = base + ControlOffset + 8;
    const u64 fake_address = base + FakePadOffset;
    for (size_t i = 0; i < InputLoadOffsets.size(); ++i) {
        const u64 site = base + InputLoadOffsets[i];
        const u64 cave = base + InputCaveOffset + i * 0x40;
        u8* const site_host = memory.GetPointerSilent(site);
        u8* const cave_host = memory.GetPointerSilent(cave);
        u32 current;
        if (!site_host || !cave_host) {
            return false;
        }
        std::memcpy(&current, site_host, 4);
        if (current != InputLoadInstruction) {
            LOG_WARNING(Core, "Nx2 multiplayer: input load at {:#x} is {:#010x}, not patched", site,
                        current);
            return false;
        }
        const std::array<u32, 8> code{
            InputLoadInstruction, // ldr x20, [x22, #0xb0]
            0xF9400E70,           // ldr x16, [x19, #0x18] (the Link)
            0x580000D1,           // ldr x17, =control block + 8 (literal at + 0x20)
            0xF9400231,           // ldr x17, [x17] (the puppet)
            0xEB11021F,           // cmp x16, x17
            0x54000041,           // b.ne +8
            0x58000094,           // ldr x20, =fake controller (literal at + 0x28)
            Branch(cave + 0x1C, site + 4),
        };
        std::memcpy(cave_host, code.data(), sizeof(code));
        std::memcpy(cave_host + 0x20, &actor_slot, sizeof(actor_slot));
        std::memcpy(cave_host + 0x28, &fake_address, sizeof(fake_address));
        Core::InvalidateInstructionCacheRange(&process, cave, 0x40);
    }
    for (size_t i = 0; i < InputLoadOffsets.size(); ++i) {
        const u64 site = base + InputLoadOffsets[i];
        const u32 jump = Branch(site, base + InputCaveOffset + i * 0x40);
        std::memcpy(memory.GetPointerSilent(site), &jump, 4);
        Core::InvalidateInstructionCacheRange(&process, site, 4);
    }
    LOG_INFO(Core, "Nx2 multiplayer: input hook installed (the puppet reads the remote controller)");
    return true;
}

/// Reads the local player controller's pad state.
void ReadLocalPad(Core::Memory::Memory& memory, u64 base, Player& local) {
    const auto controller = PlayerController(memory, base);
    local.has_pad = controller && Read(memory, *controller + PadStateStart, local.pad);
}

/// Writes the remote player's pad state into the puppet's controller (neutral when none).
void WriteRemotePad(Core::Memory::Memory& memory, u64 base, const Player& remote) {
    u8* const fake = memory.GetPointerSilent(base + FakePadOffset + PadStateStart);
    if (fake == nullptr) {
        return;
    }
    if (remote.valid && remote.has_pad) {
        std::memcpy(fake, remote.pad.data(), remote.pad.size());
    } else {
        // Nothing held, triggered or released, sticks centred.
        std::memset(fake + (0x10 - PadStateStart), 0, 0x10);
        std::memset(fake + (0x120 - PadStateStart), 0, 0x10);
    }
}

/// Keeps a puppet chosen: an actor named PreferredPuppet() if one exists, else the nearest actor
/// 2 to 25 units from Link with a physics component, none of Link's own parts. While the puppet
/// is not the preferred actor, the actors are scanned again every 5 s to find one (it may be
/// created later). Returns a short status.
std::string UpdatePuppet(State& state, Kernel::KProcess& process, u64 base, const Player& link,
                         const Player& remote) {
    auto& memory = process.GetMemory();
    u8* const control = memory.GetPointerSilent(base + ControlOffset);
    if (control == nullptr) {
        return "puppet: no control block";
    }
    const auto set_component = [&](u64 component) {
        const u64 drawn = InputMode() ? 0 : component;
        std::memcpy(control, &drawn, sizeof(drawn));
        // The animation filter refuses the puppet actor's own AS requests.
        const u64 actor = component != 0 ? state.puppet_actor : 0;
        std::memcpy(control + 8, &actor, sizeof(actor));
    };
    if (!remote.valid) {
        set_component(0);
        return "puppet: idle";
    }
    const auto distance = [](const std::array<float, 3>& a, const std::array<float, 3>& b) {
        return std::sqrt((a[0] - b[0]) * (a[0] - b[0]) + (a[1] - b[1]) * (a[1] - b[1]) +
                         (a[2] - b[2]) * (a[2] - b[2]));
    };
    const auto now = std::chrono::steady_clock::now();
    // Still valid: the component chain holds and no scene change moved Link far away.
    u64 generation;
    {
        std::scoped_lock lock{state.scan_mutex};
        generation = state.scan_generation;
    }
    const bool fresh_scan = generation != state.used_generation;
    if (state.puppet_component != 0 && ComponentOf(memory, state.puppet_actor) == state.puppet_component &&
        distance(link.position, state.scan_origin) < 40.0f &&
        (state.puppet_preferred || !fresh_scan)) {
        if (!state.puppet_preferred && !state.scanning &&
            now - state.last_scan > std::chrono::seconds{5}) {
            state.last_scan = now;
            StartScan(state, process, base);
        }
        set_component(state.puppet_component);
        return fmt::format("puppet: {} {:#x}", state.puppet_preferred ? PreferredPuppet() : "actor",
                           state.puppet_actor);
    }
    state.used_generation = generation;
    set_component(0);
    state.puppet_actor = 0;
    state.puppet_component = 0;
    std::vector<u64> actors;
    {
        std::scoped_lock lock{state.scan_mutex};
        actors = state.scanned_actors;
    }
    if (actors.empty() || distance(link.position, state.scan_origin) >= 40.0f) {
        if (!state.scanning && now - state.last_scan > std::chrono::seconds{5}) {
            state.last_scan = now;
            state.scan_origin = link.position;
            {
                std::scoped_lock lock{state.scan_mutex};
                state.scanned_actors.clear();
            }
            StartScan(state, process, base);
        }
        return "puppet: looking for actors";
    }
    // An actor with the preferred name first, the one whose physics body (the component's own
    // matrix, which the detour leaves alone) is farthest from Link: a Dark Link far away idles
    // (its AI sleeps out of range) while it is drawn where the remote player is.
    state.puppet_preferred = false;
    float farthest = -1.0f;
    for (const u64 actor : actors) {
        if (actor == state.local_actor || NameOf(memory, actor) != PreferredPuppet()) {
            continue;
        }
        const auto component = ComponentOf(memory, actor);
        std::array<float, 12> body;
        if (!component || !Read(memory, *component + 0x80, body)) {
            continue;
        }
        const float d = distance({body[3], body[7], body[11]}, link.position);
        if (d > farthest) {
            farthest = d;
            state.puppet_actor = actor;
            state.puppet_component = *component;
            state.puppet_preferred = true;
        }
    }
    if (state.puppet_preferred) {
        LOG_INFO(Core, "Nx2 multiplayer: puppet is {} {:#x} (component {:#x}), body {:.1f} from Link",
                 PreferredPuppet(), state.puppet_actor, state.puppet_component, farthest);
        set_component(state.puppet_component);
        return fmt::format("puppet: {} {:#x}", PreferredPuppet(), state.puppet_actor);
    }
    // Link's own components (he is the actor at his exact position and his parts around it).
    float best = 1e9f;
    for (const u64 actor : actors) {
        std::array<float, 3> position;
        if (!Read(memory, actor + ActorPosition, position) || !Sane(position)) {
            continue;
        }
        const float d = distance(position, link.position);
        if (d < 2.0f || d > 25.0f || d >= best) {
            continue;
        }
        const auto component = ComponentOf(memory, actor);
        if (!component) {
            continue;
        }
        best = d;
        state.puppet_actor = actor;
        state.puppet_component = *component;
    }
    if (state.puppet_component == 0) {
        return "puppet: no actor nearby";
    }
    LOG_INFO(Core, "Nx2 multiplayer: puppet is actor {:#x} (component {:#x}), {:.1f} from Link",
             state.puppet_actor, state.puppet_component, best);
    set_component(state.puppet_component);
    return fmt::format("puppet: actor {:#x}", state.puppet_actor);
}

/// Plays Move while the remote player moves and Wait while it stands, on the puppet.
void UpdatePuppetAnimation(State& state, Kernel::KProcess& process, u64 base, const Player& remote) {
    auto& memory = process.GetMemory();
    const auto now = std::chrono::steady_clock::now();
    if (!remote.valid || state.puppet_actor == 0) {
        state.puppet_as.clear();
        return;
    }
    const float dt = std::chrono::duration<float>(now - state.last_remote_time).count();
    if (dt >= 0.05f) {
        float moved = 0;
        for (int i = 0; i < 3; ++i) {
            moved += (remote.position[i] - state.last_remote[i]) * (remote.position[i] - state.last_remote[i]);
        }
        const float speed = std::sqrt(moved) / dt;
        // A jump (scene change) does not count as running.
        state.remote_speed = speed > 30.0f ? 0.0f : speed;
        if (state.remote_speed > 0.6f) {
            state.last_moving_time = now;
        }
        state.last_remote = remote.position;
        state.last_remote_time = now;
    }
    // Raw speed both ways so the puppet starts and stops with the remote player; standing still
    // only counts after 0.12 s, so a tick without a new packet does not stop the run.
    const bool moving = now - state.last_moving_time < std::chrono::milliseconds(120);
    // The remote Link's own AS when the peer sends it (the puppet shares Link's AS names), else
    // Move or Wait from its motion.
    const std::string wanted = !remote.action.empty() ? remote.action : moving ? "Move" : "Wait";
    if (wanted != state.puppet_as || state.puppet_as_actor != state.puppet_actor) {
        LOG_INFO(Core, "Nx2 multiplayer: puppet plays {} (remote speed {:.1f})", wanted,
                 state.remote_speed);
        state.puppet_as = wanted;
        state.puppet_as_actor = state.puppet_actor;
        state.puppet_as_started = false;
    }
    // Loops (standing, moving, turning) are asked for every tick: the game's AS nodes request
    // theirs again while they run, and a single request froze the pose. Anything else (attacks,
    // rolls, jumps) only until the puppet plays it, or asking again restarts it when it ends.
    const bool loop = wanted.find("Wait") != std::string::npos ||
                      wanted.find("Move") != std::string::npos ||
                      wanted.find("Turn") != std::string::npos;
    if (!loop && !state.puppet_as_started && ReadAction(memory, state.puppet_actor) == wanted) {
        state.puppet_as_started = true;
    }
    if (loop || !state.puppet_as_started) {
        QueuePuppetAs(process, base, state.puppet_actor, wanted, wanted == "Wait" ? 6.0f : 3.0f);
    }
}

} // Anonymous namespace

void Shutdown() {
    State& state = GetState();
    state.stop_scan = true;
    if (state.scanner && state.scanner->joinable()) {
        state.scanner->join();
    }
    state.scanner.reset();
    state.stop_scan = false;
    state.scanning = false;
    state.process_id = ~0ULL;
    state.puppet_actor = 0;
    state.puppet_component = 0;
}

bool Enabled() {
    static const bool enabled = Env("EDEN_NX2_MP") != nullptr;
    return enabled;
}

Frame Tick(Core::System& system) {
    Frame frame;
    State& state = GetState();
    Kernel::KProcess* const process = system.IsPoweredOn() ? system.ApplicationProcess() : nullptr;
    const auto stop_scanner = [&state] {
        state.stop_scan = true;
        if (state.scanner && state.scanner->joinable()) {
            state.scanner->join();
        }
        state.scanner.reset();
        state.stop_scan = false;
        state.scanning = false;
        std::scoped_lock lock{state.scan_mutex};
        state.scanned_actors.clear();
    };
    if (process == nullptr || process->GetProgramId() != OcarinaProgramId) {
        // The scan reads the old process's memory: stop it before that memory goes away.
        stop_scanner();
        state.process_id = ~0ULL;
        frame.status = "multiplayer: waiting for Ocarina of Time";
        return frame;
    }
    if (process->GetProcessId() != state.process_id) {
        stop_scanner();
        state.puppet_actor = 0;
        state.puppet_component = 0;
        state.process_id = process->GetProcessId();
        state.main_base = FindMainBase(*process);
        state.puppet_installed = false;
        state.local_actor = 0;
        LOG_INFO(Core, "Nx2 multiplayer: main module at {:#x}", state.main_base);
        if (!state.socket && !state.ghost_mode) {
            SetUpNetwork(state);
        }
    }
    auto& memory = process->GetMemory();
    const auto link = ReadLink(memory, state.main_base, state.local_actor);
    if (link) {
        frame.local = *link;
        ReadLocalPad(memory, state.main_base, frame.local);
    }
    Exchange(state, frame.local);
    frame.remote = state.remote;
    std::string puppet_status;
    if (Env("EDEN_NX2_MP_PUPPET")) {
        if (!state.puppet_installed) {
            state.puppet_installed = InstallPuppetDetour(*process, state.main_base);
            state.input_hooked = false;
        }
        if (InputMode() && state.puppet_installed) {
            if (!state.input_hooked) {
                state.input_hooked = InstallInputHook(*process, state.main_base);
            }
            WriteRemotePad(memory, state.main_base, frame.remote);
        }
        if (state.puppet_installed && link) {
            WritePuppet(*process, state.main_base, frame.remote);
            puppet_status = UpdatePuppet(state, *process, state.main_base, *link, frame.remote);
            // Experiment switch: EDEN_NX2_MP_ANIM=0 leaves the puppet's animations alone.
            const char* anim = Env("EDEN_NX2_MP_ANIM");
            if (!anim || std::strcmp(anim, "0") != 0) {
                UpdatePuppetAnimation(state, *process, state.main_base, frame.remote);
            }
        }
    }
    frame.valid = link && ReadCamera(memory, state.main_base, *link, frame);
    frame.status = fmt::format("multiplayer: {} | Link {} | camera {} | remote {}{}{}",
                               state.network_status, link ? "ok" : "not found",
                               frame.valid ? "ok" : "not found", frame.remote.valid ? "ok" : "none",
                               puppet_status.empty() ? "" : " | ", puppet_status);
    return frame;
}

} // namespace Core::Nx2Multiplayer
