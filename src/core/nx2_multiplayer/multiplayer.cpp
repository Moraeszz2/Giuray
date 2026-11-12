// SPDX-License-Identifier: GPL-3.0-or-later

#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <cstring>

#include <fmt/format.h>

#include "common/logging.h"
#include "core/core.h"
#include "core/hle/kernel/k_process.h"
#include "core/nx2_multiplayer/guest.h"
#include "core/nx2_multiplayer/layout.h"
#include "core/nx2_multiplayer/multiplayer.h"
#include "core/nx2_multiplayer/navis.h"
#include "core/nx2_multiplayer/network.h"
#include "core/nx2_multiplayer/patches.h"
#include "core/nx2_multiplayer/progress.h"
#include "core/nx2_multiplayer/remote_links.h"
#include "core/nx2_multiplayer/state.h"

namespace Core::Nx2Multiplayer {
namespace {

void SetEnv(const char* name, const std::string& value) {
#ifdef _WIN32
    _putenv_s(name, value.c_str());
#else
    if (value.empty()) {
        unsetenv(name);
    } else {
        setenv(name, value.c_str(), 1);
    }
#endif
}

/// A new game (or none): everything that pointed into the old one is forgotten.
void ForgetGame(State& state) {
    Navis::StopScan(state);
    state.process_id = ~0ULL;
    state.main_base = 0;
    state.patches = {};
    state.link = {};
    state.navi.main = 0;
    state.navi.creating = -1;
    state.navi.scene_link = 0;
    state.progress.entries = 0;
    state.progress.incoming.clear();
    for (Remote& remote : state.remotes) {
        const u8 id = remote.id;
        const Player player = remote.player;
        const Clock::time_point time = remote.time;
        remote = {};
        remote.id = id;
        remote.player = player;
        remote.time = time;
    }
}

/// The local Link and camera, and the local player's controller and camera direction.
std::optional<Player> ReadLocal(State& state, Core::Memory::Memory& memory, Frame& frame) {
    std::array<u64, MaxRemotes> others;
    for (size_t i = 0; i < MaxRemotes; ++i) {
        others[i] = state.remotes[i].link;
    }
    const auto link = ReadLink(memory, state.main_base, state.link.local, others);
    if (!link) {
        return std::nullopt;
    }
    frame.local = *link;
    RemoteLinks::ReadLocalPad(memory, state.main_base, frame.local);
    frame.valid = ReadCamera(memory, state.main_base, *link, frame);
    if (frame.valid) {
        const float cx = frame.at[0] - frame.eye[0];
        const float cz = frame.at[2] - frame.eye[2];
        const float length = std::sqrt(cx * cx + cz * cz);
        if (length > 1e-3f) {
            frame.local.camera = {cx / length, cz / length};
        }
    }
    return link;
}

} // Anonymous namespace

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

bool SwitchOn(const char* name) {
    const char* value = Env(name);
    return !value || std::strcmp(value, "0") != 0;
}

Remote* RemoteFor(State& state, u8 id) {
    for (Remote& remote : state.remotes) {
        if (remote.id == id) {
            return &remote;
        }
    }
    // A free slot, else one whose player has been gone the longest (its Link is kept).
    Remote* oldest = nullptr;
    for (Remote& remote : state.remotes) {
        if (remote.id == NoId) {
            remote.id = id;
            return &remote;
        }
        if (!Active(remote) && (!oldest || remote.time < oldest->time)) {
            oldest = &remote;
        }
    }
    if (oldest) {
        oldest->id = id;
    }
    return oldest;
}

bool Active(const Remote& remote) {
    return remote.id != NoId && remote.player.valid &&
           Clock::now() - remote.time < std::chrono::seconds{2};
}

bool Enabled() {
    // Not cached: StartRoom turns it on while a game runs.
    return Env("EDEN_NX2_MP") != nullptr;
}

void StartRoom(const std::string& code, const std::string& name) {
    SetEnv("EDEN_NX2_MP", "1");
    SetEnv("EDEN_NX2_MP_PEER", "");
    SetEnv("EDEN_NX2_MP_ROOM", code.empty() ? "HOST" : code);
    SetEnv("EDEN_NX2_MP_NAME", name.empty() ? "P1" : name);
    GetState().network.restart = true;
}

void LeaveRoom() {
    NetworkState& network = GetState().network;
    network.restart = false;
    network.leave = true;
}

std::string RoomCode() {
    State& state = GetState();
    return state.network.room ? state.network.room->code : std::string{};
}

bool InRoom() {
    const NetworkState& network = GetState().network;
    return network.restart || (network.socket.has_value() && !network.leave);
}

std::string NetworkStatus() {
    const NetworkState& network = GetState().network;
    if (network.restart) {
        return "abrindo a sala (o jogo precisa estar aberto)";
    }
    return network.status;
}

bool Connected() {
    State& state = GetState();
    return std::ranges::any_of(state.remotes, [](const Remote& remote) { return Active(remote); });
}

std::vector<std::string> PlayerNames() {
    std::vector<std::string> names;
    for (const Remote& remote : GetState().remotes) {
        if (Active(remote)) {
            names.push_back(remote.player.name.empty() ? fmt::format("P{}", remote.id + 1)
                                                       : remote.player.name);
        }
    }
    return names;
}

void Shutdown() {
    ForgetGame(GetState());
}

Frame Tick(Core::System& system) {
    Frame frame;
    State& state = GetState();
    NetworkState& network = state.network;
    Kernel::KProcess* const process = system.IsPoweredOn() ? system.ApplicationProcess() : nullptr;
    if (process == nullptr || process->GetProgramId() != Layout::OcarinaProgramId) {
        // The scan reads the old process's memory: stop it before that memory goes away.
        ForgetGame(state);
        frame.status = "multiplayer: esperando Ocarina of Time";
        return frame;
    }
    if (network.leave) {
        network.leave = false;
        network.restart = false;
        network.left = true;
        Network::Close(state);
    }
    if (network.restart) {
        network.restart = false;
        network.left = false;
        Network::Close(state);
        Network::Open(state);
    }
    if (process->GetProcessId() != state.process_id) {
        ForgetGame(state);
        state.process_id = process->GetProcessId();
        state.main_base = FindMainBase(*process);
        LOG_INFO(Core, "Nx2 multiplayer: main module at {:#x}", state.main_base);
        if (!network.socket && !network.left) {
            Network::Open(state);
        }
    }
    const u64 base = state.main_base;
    auto& memory = process->GetMemory();
    const auto link = ReadLocal(state, memory, frame);
    Network::Exchange(state, frame.local);
    if (SwitchOn("EDEN_NX2_MP_SYNC")) {
        Progress::Sync(state, memory, base);
    }
    if (!state.patches.core_tried) {
        state.patches.core_tried = true;
        state.patches.core = Patches::InstallCore(*process, base);
    }
    if (state.patches.core) {
        if (!state.patches.input) {
            state.patches.input = Patches::InstallInput(*process, base);
        }
        if (link) {
            RemoteLinks::Update(state, *process, base, *link, frame);
        }
        if (SwitchOn("EDEN_NX2_MP_NAVI")) {
            if (!state.patches.navi_tried) {
                state.patches.navi_tried = true;
                state.patches.navi = Patches::InstallNaviFilter(*process, base);
            }
            if (state.patches.navi) {
                Navis::Update(state, *process, base);
            }
        }
    }
    for (const Remote& remote : state.remotes) {
        if (Active(remote)) {
            frame.remotes.push_back(
                {.name = remote.player.name.empty() ? fmt::format("P{}", remote.id + 1)
                                                    : remote.player.name,
                 .body = remote.link ? RemoteLinks::BodyOf(memory, remote.link) : std::nullopt});
        }
    }
    frame.status = fmt::format("multiplayer: {}{}", network.socket ? network.status : "fora da sala",
                               link ? "" : " | Link nao encontrado");
    return frame;
}

} // namespace Core::Nx2Multiplayer
