// SPDX-License-Identifier: GPL-3.0-or-later

#include <atomic>
#include <cstring>

#include "common/logging.h"
#include "core/hle/kernel/k_page_table.h"
#include "core/hle/kernel/k_process.h"
#include "core/nx2_multiplayer/guest.h"
#include "core/nx2_multiplayer/layout.h"
#include "core/nx2_multiplayer/navis.h"
#include "core/nx2_multiplayer/remote_links.h"

namespace Core::Nx2Multiplayer::Navis {
namespace {

/// Scans the heap for actors (their vtable) on a thread.
void StartScan(NaviState& navi, Kernel::KProcess& process, u64 base) {
    if (navi.scanning.exchange(true)) {
        return;
    }
    if (navi.scanner && navi.scanner->joinable()) {
        navi.scanner->join();
    }
    const u64 vtable = base + Layout::ActorVtable;
    auto& page_table = process.GetPageTable();
    const u64 heap_start = GetInteger(page_table.GetHeapRegionStart());
    const u64 heap_end = heap_start + page_table.GetHeapRegionSize();
    Core::Memory::Memory* const memory = &process.GetMemory();
    navi.scanner.emplace([&navi, memory, vtable, heap_start, heap_end] {
        std::vector<u64> actors;
        for (u64 page = heap_start; page < heap_end && !navi.stop_scan;
             page += Core::Memory::YUZU_PAGESIZE) {
            const u8* const bytes = memory->GetPointerSilent(page);
            if (bytes == nullptr) {
                continue;
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
            std::scoped_lock lock{navi.scan_mutex};
            navi.scanned_actors = std::move(actors);
        }
        navi.scanning = false;
    });
}

void ForgetNavi(Remote& remote) {
    remote.navi = 0;
    remote.navi_requested = {};
    remote.navi_born = {};
    remote.navi_set_up = false;
}

/// Finds the scene's Navi and the ones created for the remote Links in the last scan, scans
/// again while one is missing, and asks for the next missing one.
void FindNavis(State& state, Kernel::KProcess& process, u64 base, u64* main_slot) {
    auto& memory = process.GetMemory();
    NaviState& navi = state.navi;
    const auto now = Clock::now();
    std::vector<u64> actors;
    {
        std::scoped_lock lock{navi.scan_mutex};
        actors = navi.scanned_actors;
    }
    std::vector<u64> found;
    for (const u64 actor : actors) {
        if (IsNavi(memory, base, actor)) {
            found.push_back(actor);
        }
    }
    const bool asked = navi.creating >= 0 ||
                       std::ranges::any_of(state.remotes, [](const Remote& r) { return r.navi; });
    if (navi.main == 0 && found.size() == 1 && !asked) {
        navi.main = found[0];
    }
    if (navi.main == 0) {
        if (!navi.scanning && now - navi.last_scan > std::chrono::seconds{3}) {
            navi.last_scan = now;
            StartScan(navi, process, base);
        }
        return;
    }
    // A Navi neither the scene's nor a remote Link's is the one asked for last.
    for (const u64 actor : found) {
        const bool known = actor == navi.main ||
                           std::ranges::any_of(state.remotes,
                                               [actor](const Remote& r) { return r.navi == actor; });
        if (!known && navi.creating >= 0) {
            Remote& remote = state.remotes[navi.creating];
            ForgetNavi(remote);
            remote.navi = actor;
            navi.creating = -1;
            LOG_INFO(Core, "Nx2 multiplayer: remote player {}'s Navi is {:#x}", remote.id, actor);
        }
    }
    Remote* missing = nullptr;
    size_t missing_slot = 0;
    for (size_t slot = 0; slot < state.remotes.size(); ++slot) {
        if (state.remotes[slot].link != 0 && state.remotes[slot].navi == 0) {
            missing = &state.remotes[slot];
            missing_slot = slot;
            break;
        }
    }
    if (missing == nullptr) {
        return;
    }
    if (!navi.scanning && now - navi.last_scan > std::chrono::seconds{3}) {
        navi.last_scan = now;
        StartScan(navi, process, base);
    }
    if (navi.creating >= 0 &&
        now - state.remotes[navi.creating].navi_requested < std::chrono::seconds{20}) {
        return; // the last one asked for is not there yet
    }
    const auto body = RemoteLinks::BodyOf(memory, missing->link);
    if (!body) {
        return;
    }
    // The scene's Navi is known before another one exists, or the filter would stop both.
    std::atomic_ref<u64>{*main_slot}.store(navi.main);
    if (RemoteLinks::QueueCreate(memory, base, "Npc_Navi",
                                 {(*body)[0], (*body)[1] + 1.4f, (*body)[2]})) {
        navi.creating = static_cast<int>(missing_slot);
        missing->navi_requested = now;
        LOG_INFO(Core, "Nx2 multiplayer: creating remote player {}'s Navi", missing->id);
    }
}

/// Keeps a remote Link's Navi over its shoulder. Created with no AI, a Navi never finishes
/// setting up (no model update, no glow): a few seconds after it exists, one AI update is let
/// through, once.
void PlaceNavi(Core::Memory::Memory& memory, u64 base, Remote& remote, u8* entry) {
    const auto now = Clock::now();
    if (remote.navi_born == Clock::time_point{}) {
        remote.navi_born = now;
    } else if (!remote.navi_set_up && now - remote.navi_born > std::chrono::seconds{5}) {
        if (u32* const updates =
                reinterpret_cast<u32*>(memory.GetPointerSilent(base + Layout::NaviUpdatesSlot))) {
            std::atomic_ref<u32>{*updates}.fetch_add(1);
            remote.navi_set_up = true;
        }
    }
    const auto body = RemoteLinks::BodyOf(memory, remote.link);
    if (!body) {
        return;
    }
    const std::array<float, 3> position{(*body)[0] + 0.45f, (*body)[1] + 1.0f, (*body)[2]};
    if (const auto component = ComponentOf(memory, remote.navi); component && entry) {
        const std::array<float, 12> matrix{1, 0,           0, position[0], 0, 1,
                                           0, position[1], 0, 0,           1, position[2]};
        std::memcpy(entry + Code::DrawEntryMatrix, matrix.data(), sizeof(matrix));
        std::atomic_ref<u64>{*reinterpret_cast<u64*>(entry)}.store(*component);
    } else if (u8* const pending =
                   memory.GetPointerSilent(remote.navi + Layout::ActorPendingPosition)) {
        // No physics body: the actor's pending position moves it.
        std::memcpy(pending, position.data(), sizeof(position));
    }
}

} // Anonymous namespace

void Update(State& state, Kernel::KProcess& process, u64 base) {
    auto& memory = process.GetMemory();
    NaviState& navi = state.navi;
    u64* const main_slot = reinterpret_cast<u64*>(memory.GetPointerSilent(base + Layout::MainNaviSlot));
    u8* const table = memory.GetPointerSilent(base + Layout::DrawTable);
    if (main_slot == nullptr || table == nullptr) {
        return;
    }
    if (navi.scene_link != state.link.local) {
        // Another scene: the last scan's actors are gone.
        navi.scene_link = state.link.local;
        std::scoped_lock lock{navi.scan_mutex};
        navi.scanned_actors.clear();
    }
    if (navi.main != 0 && !IsNavi(memory, base, navi.main)) {
        navi.main = 0;
        navi.creating = -1;
        for (Remote& remote : state.remotes) {
            ForgetNavi(remote);
        }
        std::atomic_ref<u64>{*main_slot}.store(0);
    }
    for (Remote& remote : state.remotes) {
        if (remote.navi != 0 && !IsNavi(memory, base, remote.navi)) {
            ForgetNavi(remote);
        }
    }
    if (std::ranges::any_of(state.remotes,
                            [](const Remote& r) { return r.link != 0 && r.navi == 0; })) {
        FindNavis(state, process, base, main_slot);
    }
    for (size_t slot = 0; slot < state.remotes.size(); ++slot) {
        Remote& remote = state.remotes[slot];
        u8* const entry = table + slot * Code::DrawEntrySize;
        if (remote.navi != 0 && remote.link != 0) {
            PlaceNavi(memory, base, remote, entry);
        } else {
            std::atomic_ref<u64>{*reinterpret_cast<u64*>(entry)}.store(0);
        }
    }
}

void StopScan(State& state) {
    NaviState& navi = state.navi;
    navi.stop_scan = true;
    if (navi.scanner && navi.scanner->joinable()) {
        navi.scanner->join();
    }
    navi.scanner.reset();
    navi.stop_scan = false;
    navi.scanning = false;
    std::scoped_lock lock{navi.scan_mutex};
    navi.scanned_actors.clear();
}

} // namespace Core::Nx2Multiplayer::Navis
