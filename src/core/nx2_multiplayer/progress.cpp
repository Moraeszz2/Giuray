// SPDX-License-Identifier: GPL-3.0-or-later

#include "common/logging.h"
#include "core/memory.h"
#include "core/nx2_multiplayer/guest.h"
#include "core/nx2_multiplayer/layout.h"
#include "core/nx2_multiplayer/network.h"
#include "core/nx2_multiplayer/progress.h"

namespace Core::Nx2Multiplayer::Progress {

void Sync(State& state, Core::Memory::Memory& memory, u64 base) {
    using namespace Layout;
    ProgressState& progress = state.progress;
    u64 manager;
    u32 count;
    u64 entries;
    if (!Read(memory, base + GameDataMgrSlot, manager) || manager == 0 ||
        !Read(memory, manager + BoolCount, count) || !Read(memory, manager + BoolEntries, entries) ||
        count == 0 || count > 0x10000 || entries == 0) {
        return;
    }
    const auto value_of = [&](u32 index) {
        const u8* const value =
            memory.GetPointerSilent(entries + index * BoolEntrySize + BoolValue);
        return value ? *value : u8{0};
    };
    if (entries != progress.entries || count != progress.count) {
        // A new table (a game started or loaded): this player's own flags are the baseline.
        progress.entries = entries;
        progress.count = count;
        progress.values.resize(count);
        for (u32 i = 0; i < count; ++i) {
            progress.values[i] = value_of(i);
        }
        progress.incoming.clear();
        LOG_INFO(Core, "Nx2 multiplayer: progress sync follows {} Bool flags", count);
        return;
    }
    // The other players' flags: turned on here, and known so they are not sent back.
    for (const u16 index : progress.incoming) {
        if (index >= count) {
            continue;
        }
        const u64 entry = entries + index * BoolEntrySize;
        u8* const written = memory.GetPointerSilent(entry + BoolWritten);
        u8* const value = memory.GetPointerSilent(entry + BoolValue);
        if (written && value && *value == 0) {
            *written = 1;
            value[0] = 1;
            value[1] = 1;
        }
        progress.values[index] = 1;
    }
    if (!progress.incoming.empty()) {
        LOG_INFO(Core, "Nx2 multiplayer: {} flags from the other players", progress.incoming.size());
        progress.incoming.clear();
    }
    const auto now = Clock::now();
    if (now - progress.last_scan < std::chrono::milliseconds{250}) {
        return;
    }
    progress.last_scan = now;
    std::vector<u16> turned_on;
    for (u32 i = 0; i < count; ++i) {
        const u8 value = value_of(i);
        if (value != 0 && progress.values[i] == 0) {
            turned_on.push_back(static_cast<u16>(i));
        }
        progress.values[i] = value;
    }
    if (!turned_on.empty()) {
        LOG_INFO(Core, "Nx2 multiplayer: {} flags turned on, sending", turned_on.size());
        Network::QueueFlags(state, turned_on, nullptr);
    }
}

} // namespace Core::Nx2Multiplayer::Progress
