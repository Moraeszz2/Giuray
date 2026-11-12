// SPDX-License-Identifier: GPL-3.0-or-later

#include <algorithm>
#include <array>
#include <cstring>
#include <vector>

#include "common/logging.h"
#include "core/arm/debug.h"
#include "core/hle/kernel/k_process.h"
#include "core/nx2_multiplayer/code.h"
#include "core/nx2_multiplayer/guest.h"
#include "core/nx2_multiplayer/layout.h"
#include "core/nx2_multiplayer/patches.h"

namespace Core::Nx2Multiplayer::Patches {
namespace {

// The instructions the patches replace.
constexpr u32 GetterFirst = 0xF9412808;  // ldr x8, [x0, #0x250]
constexpr u32 GetterSecond = 0x1E2E1001; // fmov s1, #1.0
constexpr u32 CopyLoad = 0x3DC02360;     // ldr q0, [x27, #0x80]
constexpr u32 InputLoad = 0xF9405AD4;    // ldr x20, [x22, #0xb0]
constexpr u32 DispatchCall = 0xD63F0100; // blr x8

/// Where the transform update copies a component's matrix rows itself: the load, and where to go
/// on after the three rows are stored.
constexpr std::array<std::pair<u64, u64>, 2> CopySites{{
    {0x137858, 0x137870},
    {0x1382CC, 0x1382E4},
}};

/// The player input update's two loads of the player controller.
constexpr std::array<u64, 2> InputSites{0x574B44, 0x574F20};

u32 ReadWord(Core::Memory::Memory& memory, u64 at) {
    u32 word = 0;
    Read(memory, at, word);
    return word;
}

bool IsZero(Core::Memory::Memory& memory, u64 at, size_t size) {
    std::vector<u8> bytes(size);
    for (size_t i = 0; i < size; ++i) {
        if (!Read(memory, at + i, bytes[i])) {
            return false;
        }
    }
    return std::ranges::all_of(bytes, [](u8 byte) { return byte == 0; });
}

bool Write(Kernel::KProcess& process, u64 at, const std::vector<u32>& words) {
    u8* const host = process.GetMemory().GetPointerSilent(at);
    if (host == nullptr) {
        return false;
    }
    std::memcpy(host, words.data(), words.size() * 4);
    Core::InvalidateInstructionCacheRange(&process, at, words.size() * 4);
    return true;
}

bool Jump(Kernel::KProcess& process, u64 from, u64 to) {
    return Write(process, from, {Code::Branch(from, to)});
}

} // Anonymous namespace

bool InstallCore(Kernel::KProcess& process, u64 base) {
    using namespace Layout;
    auto& memory = process.GetMemory();
    const u64 getter = base + MatrixGetter;
    const u64 play = base + AsPlay;
    const u64 dispatch = base + Dispatch;
    bool matches = ReadWord(memory, getter) == GetterFirst &&
                   ReadWord(memory, getter + 4) == GetterSecond &&
                   ReadWord(memory, play) == Code::Pacibsp &&
                   ReadWord(memory, dispatch) == DispatchCall &&
                   IsZero(memory, base + Cave, CaveUsedEnd - Cave) &&
                   IsZero(memory, base + PlayFilterCave, InputCaves - PlayFilterCave) &&
                   IsZero(memory, base + DispatchHookCave, 0x200) &&
                   IsZero(memory, base + Scratch, 0x80);
    for (const auto& [site, resume] : CopySites) {
        matches = matches && ReadWord(memory, base + site) == CopyLoad;
    }
    if (!matches) {
        LOG_ERROR(Core, "Nx2 multiplayer: the game does not match the patches (another build?)");
        return false;
    }
    const u64 table = base + DrawTable;
    bool written =
        Write(process, base + DrawDetourCave,
              Code::DrawDetour(base + DrawDetourCave, getter, GetterFirst, table)) &&
        Write(process, base + PlayFilterCave,
              Code::PlayFilter(base + PlayFilterCave, play, base + RemoteLinks)) &&
        Write(process, base + DispatchHookCave,
              Code::DispatchHook(base + DispatchHookCave, dispatch, DispatchCall, base + Scratch,
                                 play, base + ForceSetPosition, base + CreateActor));
    for (size_t i = 0; i < CopySites.size(); ++i) {
        const u64 cave = base + CopySiteCaves + i * 0x80;
        written = written && Write(process, cave,
                                   Code::CopySite(cave, base + CopySites[i].first,
                                                  base + CopySites[i].second, CopyLoad, table));
    }
    if (!written) {
        return false;
    }
    for (size_t i = 0; i < CopySites.size(); ++i) {
        Jump(process, base + CopySites[i].first, base + CopySiteCaves + i * 0x80);
    }
    Jump(process, getter, base + DrawDetourCave);
    Jump(process, play, base + PlayFilterCave);
    Jump(process, dispatch, base + DispatchHookCave);
    LOG_INFO(Core, "Nx2 multiplayer: game patches installed");
    return true;
}

bool InstallInput(Kernel::KProcess& process, u64 base) {
    using namespace Layout;
    auto& memory = process.GetMemory();
    const auto controller = PlayerController(memory, base);
    std::array<u8, ControllerSize> original;
    if (!controller || !Read(memory, *controller, original)) {
        return false;
    }
    for (const u64 site : InputSites) {
        if (ReadWord(memory, base + site) != InputLoad) {
            LOG_ERROR(Core, "Nx2 multiplayer: the input load at {:#x} does not match", site);
            return false;
        }
    }
    // Each fake controller starts as the real one (its vtable and links stay valid); only its pad
    // state is rewritten afterwards.
    std::array<u64, Code::MaxRemotes> fakes;
    for (size_t i = 0; i < fakes.size(); ++i) {
        fakes[i] = base + FakeControllers + i * FakeControllerStride;
        u8* const fake = memory.GetPointerSilent(fakes[i]);
        if (fake == nullptr) {
            return false;
        }
        std::memcpy(fake, original.data(), original.size());
    }
    for (size_t i = 0; i < InputSites.size(); ++i) {
        const u64 cave = base + InputCaves + i * 0x80;
        if (!Write(process, cave,
                   Code::InputLoad(cave, base + InputSites[i], InputLoad, base + RemoteLinks,
                                   fakes))) {
            return false;
        }
    }
    for (size_t i = 0; i < InputSites.size(); ++i) {
        Jump(process, base + InputSites[i], base + InputCaves + i * 0x80);
    }
    LOG_INFO(Core, "Nx2 multiplayer: input patch installed");
    return true;
}

bool InstallNaviFilter(Kernel::KProcess& process, u64 base) {
    using namespace Layout;
    auto& memory = process.GetMemory();
    const u64 update = base + AiUpdate;
    const u64 cave = base + NaviFilterCave;
    if (ReadWord(memory, update) != Code::Pacibsp || !IsZero(memory, cave, 0x80)) {
        LOG_ERROR(Core, "Nx2 multiplayer: the Navi filter does not match this game build");
        return false;
    }
    if (!Write(process, cave,
               Code::NaviFilter(cave, update, Code::Pacibsp, base + MainNaviSlot,
                                base + NaviUpdatesSlot))) {
        return false;
    }
    Jump(process, update, cave);
    LOG_INFO(Core, "Nx2 multiplayer: Navi filter installed");
    return true;
}

} // namespace Core::Nx2Multiplayer::Patches
