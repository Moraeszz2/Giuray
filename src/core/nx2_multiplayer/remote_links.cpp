// SPDX-License-Identifier: GPL-3.0-or-later

#include <atomic>
#include <cmath>
#include <cstring>
#include <limits>

#include "common/logging.h"
#include "core/hle/kernel/k_process.h"
#include "core/nx2_multiplayer/guest.h"
#include "core/nx2_multiplayer/layout.h"
#include "core/nx2_multiplayer/remote_links.h"

namespace Core::Nx2Multiplayer::RemoteLinks {
namespace {

float Distance(const std::array<float, 3>& a, const std::array<float, 3>& b) {
    return std::sqrt((a[0] - b[0]) * (a[0] - b[0]) + (a[1] - b[1]) * (a[1] - b[1]) +
                     (a[2] - b[2]) * (a[2] - b[2]));
}

bool AtOrigin(const std::array<float, 3>& p) {
    return std::fabs(p[0]) < 1.0f && std::fabs(p[1]) < 1.0f && std::fabs(p[2]) < 1.0f;
}

/// Takes the Link a create request made: the newest Link on the pointer chains, if it is neither
/// the local one nor another remote's.
void TakeCreatedLink(State& state, Core::Memory::Memory& memory, u64 base) {
    if (state.link.creating < 0) {
        return;
    }
    const u64 newest = ChainLink(memory, base);
    const bool taken = std::ranges::any_of(
        state.remotes, [newest](const Remote& remote) { return remote.link == newest; });
    if (newest != 0 && newest != state.link.local && !taken) {
        state.remotes[state.link.creating].link = newest;
        LOG_INFO(Core, "Nx2 multiplayer: remote player {}'s Link is {:#x}",
                 state.remotes[state.link.creating].id, newest);
        state.link.creating = -1;
    } else if (Clock::now() - state.link.create_time > std::chrono::seconds{10}) {
        state.link.creating = -1;
    }
}

/// Creates a remote player's Link where they are (once per scene, when none exists).
void Spawn(State& state, Core::Memory::Memory& memory, u64 base, Remote& remote, size_t slot,
           const Player& link) {
    const auto now = Clock::now();
    const Player& player = remote.player;
    if (remote.link != 0 || !Active(remote) || state.link.local == 0 ||
        state.link.creating >= 0 || remote.spawned_for == state.link.local ||
        now - remote.last_spawn < std::chrono::seconds{10}) {
        return;
    }
    // Not while a scene loads: a Link created then takes the camera and the controls, and the
    // players stand at the origin until they are placed. Both must have been somewhere else, and
    // the local Link the same actor in the same area, for a few seconds.
    if (AtOrigin(link.position) || AtOrigin(player.position)) {
        remote.settled_since = {};
        return;
    }
    if (remote.settled_since == Clock::time_point{} || remote.settled_actor != state.link.local) {
        remote.settled_since = now;
        remote.settled_actor = state.link.local;
    }
    if (now - remote.settled_since < std::chrono::seconds{8} ||
        Distance(link.position, player.position) > 60.0f) {
        return; // not settled yet, or in another area
    }
    if (!QueueCreate(memory, base, "Player_Child", player.position)) {
        return;
    }
    remote.last_spawn = now;
    remote.spawned_for = state.link.local;
    state.link.creating = static_cast<int>(slot);
    state.link.create_time = now;
    LOG_INFO(Core, "Nx2 multiplayer: creating remote player {}'s Link at ({:.1f}, {:.1f}, {:.1f})",
             remote.id, player.position[0], player.position[1], player.position[2]);
}

/// Writes the remote player's pad state into their Link's controller (neutral when there is
/// none). The left stick is turned from the remote camera to the local one, so the Link walks the
/// same way in the world; while the remote stick rests and the Link stands up to 3 units off, the
/// stick walks it back to the remote player's position.
void WritePad(Core::Memory::Memory& memory, u64 base, const Remote& remote, size_t slot,
              const Frame& frame) {
    using namespace Layout;
    u8* const pad = memory.GetPointerSilent(base + FakeControllers + slot * FakeControllerStride +
                                            PadStateStart);
    if (pad == nullptr) {
        return;
    }
    const Player& player = remote.player;
    if (!Active(remote) || !player.has_pad) {
        // Nothing held, triggered or released, sticks centred.
        std::memset(pad + (0x10 - PadStateStart), 0, 0x10);
        std::memset(pad + (LeftStick - PadStateStart), 0, 0x10);
        return;
    }
    std::memcpy(pad, player.pad.data(), player.pad.size());
    constexpr size_t Stick = LeftStick - PadStateStart;
    std::array<float, 2> stick;
    std::memcpy(stick.data(), pad + Stick, sizeof(stick));
    // Stick y runs along the camera's forward (fx, fz), stick x along (-fz, fx).
    const auto [rfx, rfz] = player.camera;
    const float wx = stick[1] * rfx - stick[0] * rfz;
    const float wz = stick[1] * rfz + stick[0] * rfx;
    std::array<float, 2> local_camera{0.0f, 1.0f};
    if (frame.valid) {
        const float cx = frame.at[0] - frame.eye[0];
        const float cz = frame.at[2] - frame.eye[2];
        const float length = std::sqrt(cx * cx + cz * cz);
        if (length > 1e-3f) {
            local_camera = {cx / length, cz / length};
        }
    }
    const auto [lfx, lfz] = local_camera;
    float dx = wx;
    float dz = wz;
    if (std::hypot(wx, wz) < 0.1f && remote.link != 0) {
        if (const auto body = BodyOf(memory, remote.link)) {
            const float ox = player.position[0] - (*body)[0];
            const float oz = player.position[2] - (*body)[2];
            const float distance = std::hypot(ox, oz);
            if (distance > 0.5f && distance <= 3.0f) {
                const float strength = (std::min)(1.0f, distance / 1.5f);
                dx = ox / distance * strength;
                dz = oz / distance * strength;
            }
        }
    }
    stick = {dx * -lfz + dz * lfx, dx * lfx + dz * lfz};
    std::memcpy(pad + Stick, stick.data(), sizeof(stick));
}

/// Moves a remote Link's body to the remote player when the two have drifted apart: queues the
/// game's physics teleport for the dispatch hook.
void Correct(Core::Memory::Memory& memory, u64 base, Remote& remote) {
    using namespace Layout;
    const auto now = Clock::now();
    if (!Active(remote) || remote.link == 0 ||
        now - remote.last_correction < std::chrono::milliseconds(500)) {
        return;
    }
    const auto body = BodyOf(memory, remote.link);
    u64 table;
    u64 physics;
    if (!body || !Read(memory, remote.link + ActorPhysics, table) || table == 0 ||
        !Read(memory, table + 0x50, physics) || physics == 0) {
        return;
    }
    const float distance = Distance(*body, remote.player.position);
    // Small drift is walked off (WritePad steers); a teleport only for a drift over 3 units that
    // lasts a second (stuck, fallen), never for another area (a scene loading).
    if (distance < 3.0f || distance > 60.0f) {
        remote.drift_since = {};
        return;
    }
    if (remote.drift_since == Clock::time_point{}) {
        remote.drift_since = now;
    }
    if (now - remote.drift_since < std::chrono::seconds{1}) {
        return;
    }
    u8* const request = memory.GetPointerSilent(base + TeleportRequest);
    u64* const pending = reinterpret_cast<u64*>(memory.GetPointerSilent(base + PendingTeleport));
    if (request == nullptr || pending == nullptr || std::atomic_ref<u64>{*pending}.load() != 0) {
        return;
    }
    remote.drift_since = {};
    const float nan = std::numeric_limits<float>::quiet_NaN();
    std::memcpy(request, &physics, 8);
    std::memcpy(request + 8, remote.player.position.data(), 12);
    std::memcpy(request + 0x14, &nan, 4);
    std::atomic_ref<u64>{*pending}.store(base + TeleportRequest, std::memory_order_release);
    remote.last_correction = now;
    LOG_DEBUG(Core, "Nx2 multiplayer: remote player {}'s Link moved {:.1f}", remote.id, distance);
}

} // Anonymous namespace

void ReadLocalPad(Core::Memory::Memory& memory, u64 base, Player& local) {
    const auto controller = PlayerController(memory, base);
    local.has_pad = controller && Read(memory, *controller + Layout::PadStateStart, local.pad);
}

bool QueueCreate(Core::Memory::Memory& memory, u64 base, std::string_view actor_name,
                 const std::array<float, 3>& position) {
    using namespace Layout;
    u8* const request_host = memory.GetPointerSilent(base + CreateRequest);
    u64* const pending = reinterpret_cast<u64*>(memory.GetPointerSilent(base + PendingCreate));
    u64 manager_got;
    u64 manager;
    u64 scale_address;
    std::array<float, 3> scale;
    if (request_host == nullptr || pending == nullptr || actor_name.size() >= 0x10 ||
        std::atomic_ref<u64>{*pending}.load() != 0 ||
        !Read(memory, base + ActorMgrGot, manager_got) || !Read(memory, manager_got, manager) ||
        manager == 0 || !Read(memory, base + DefaultScaleGot, scale_address) ||
        !Read(memory, scale_address, scale)) {
        return false;
    }
    // + 0 ActorMgr, + 8 name pointer, + 0x10 name, + 0x20 parameters (0xA0: position, rotation,
    // scale, instance name pointer at + 0x40, 3 at + 0x98), + 0xC0 instance name.
    std::array<u8, 0xD0> request{};
    const u64 address = base + CreateRequest;
    const u64 name = address + 0x10;
    const u64 instance = address + 0xC0;
    constexpr std::array<float, 9> identity{1, 0, 0, 0, 1, 0, 0, 0, 1};
    std::memcpy(request.data(), &manager, 8);
    std::memcpy(request.data() + 0x08, &name, 8);
    std::memcpy(request.data() + 0x10, actor_name.data(), actor_name.size());
    std::memcpy(request.data() + 0x20, position.data(), 12);
    std::memcpy(request.data() + 0x2C, identity.data(), sizeof(identity));
    std::memcpy(request.data() + 0x50, scale.data(), sizeof(scale));
    std::memcpy(request.data() + 0x60, &instance, 8);
    request[0x20 + 0x98] = 3;
    std::memcpy(request.data() + 0xC0, "MpPuppet", 9);
    std::memcpy(request_host, request.data(), request.size());
    std::atomic_ref<u64>{*pending}.store(address, std::memory_order_release);
    return true;
}

std::optional<std::array<float, 3>> BodyOf(Core::Memory::Memory& memory, u64 actor) {
    const auto component = ComponentOf(memory, actor);
    std::array<float, 12> matrix;
    if (!component || !Read(memory, *component + 0x80, matrix)) {
        return std::nullopt;
    }
    return std::array<float, 3>{matrix[3], matrix[7], matrix[11]};
}

void Update(State& state, Kernel::KProcess& process, u64 base, const Player& link,
            const Frame& frame) {
    auto& memory = process.GetMemory();
    TakeCreatedLink(state, memory, base);
    u64* const links = reinterpret_cast<u64*>(memory.GetPointerSilent(base + Layout::RemoteLinks));
    for (size_t slot = 0; slot < state.remotes.size(); ++slot) {
        Remote& remote = state.remotes[slot];
        if (remote.link != 0 && !IsLink(memory, base, remote.link)) {
            remote.link = 0; // gone (a scene change): it may be created again
            remote.spawned_for = 0;
        }
        Spawn(state, memory, base, remote, slot, link);
        // A remote Link keeps reading its fake controller even while its player is away, or it
        // would read the local player's.
        if (links != nullptr) {
            std::atomic_ref<u64>{links[slot]}.store(remote.link);
        }
        WritePad(memory, base, remote, slot, frame);
        Correct(memory, base, remote);
    }
}

} // namespace Core::Nx2Multiplayer::RemoteLinks
