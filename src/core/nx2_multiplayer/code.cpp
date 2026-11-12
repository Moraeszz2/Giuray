// SPDX-License-Identifier: GPL-3.0-or-later

#include <cstring>

#include "core/nx2_multiplayer/code.h"

namespace Core::Nx2Multiplayer::Code {
namespace {

constexpr u32 X0 = 0, X16 = 16, X17 = 17, X20 = 20;
constexpr u32 Eq = 0;

/// Appends instructions at a known guest address; labels are patched once their address is known.
class Writer {
public:
    explicit Writer(u64 base_) : base{base_} {}

    u64 Here() const {
        return base + words.size() * 4;
    }

    void Emit(u32 word) {
        words.push_back(word);
    }

    void Emit64(u64 value) {
        words.push_back(static_cast<u32>(value));
        words.push_back(static_cast<u32>(value >> 32));
    }

    /// Reserves a word for a branch to a label; Bind writes it.
    size_t Reserve() {
        words.push_back(0);
        return words.size() - 1;
    }

    void BindCond(size_t index, u32 cond) {
        const u64 from = base + index * 4;
        words[index] = 0x54000000 | ((static_cast<u32>((Here() - from) >> 2) & 0x7FFFF) << 5) |
                       cond;
    }

    void BindBranch(size_t index) {
        words[index] = Branch(base + index * 4, Here());
    }

    void Align8() {
        if (words.size() % 2 != 0) {
            words.push_back(0);
        }
    }

    std::vector<u32> Take() {
        return std::move(words);
    }

private:
    u64 base;
    std::vector<u32> words;
};

/// ldr xt, [xn, #imm] (64-bit, unsigned offset).
u32 LoadImmediate(u32 rt, u32 rn, u32 imm) {
    return 0xF9400000 | ((imm / 8) << 10) | (rn << 5) | rt;
}

/// cmp xn, xm.
u32 Compare(u32 rn, u32 rm) {
    return 0xEB000000 | (rm << 16) | (rn << 5) | 31;
}

/// add xd, xn, #imm.
u32 AddImmediate(u32 rd, u32 rn, u32 imm) {
    return 0x91000000 | (imm << 10) | (rn << 5) | rd;
}

/// adr xd, target.
u32 Address(u64 from, u64 target, u32 rd) {
    const u32 offset = static_cast<u32>(target - from);
    return 0x10000000 | ((offset & 3) << 29) | (((offset >> 2) & 0x7FFFF) << 5) | rd;
}

} // Anonymous namespace

u32 Branch(u64 from, u64 to) {
    return 0x14000000 | ((static_cast<u32>((to - from) >> 2)) & 0x3FFFFFF);
}

u32 BranchLink(u64 from, u64 to) {
    return 0x94000000 | ((static_cast<u32>((to - from) >> 2)) & 0x3FFFFFF);
}

u32 LoadLiteral(u64 from, u64 literal, u32 rt) {
    return 0x58000000 | ((static_cast<u32>((literal - from) >> 2) & 0x7FFFF) << 5) | rt;
}

std::vector<u32> DrawDetour(u64 at, u64 getter, u32 first, u64 table) {
    Writer w{at};
    w.Emit(Address(w.Here(), table, X16));
    std::array<size_t, MaxRemotes> found{};
    for (size_t i = 0; i < MaxRemotes; ++i) {
        w.Emit(LoadImmediate(X17, X16, 0));
        w.Emit(Compare(X17, X0));
        found[i] = w.Reserve();
        if (i + 1 < MaxRemotes) {
            w.Emit(AddImmediate(X16, X16, static_cast<u32>(DrawEntrySize)));
        }
    }
    w.Emit(first);
    w.Emit(Branch(w.Here(), getter + 4));
    for (const size_t index : found) {
        w.BindCond(index, Eq);
    }
    w.Emit(AddImmediate(X0, X16, static_cast<u32>(DrawEntryMatrix)));
    w.Emit(Ret);
    return w.Take();
}

std::vector<u32> CopySite(u64 at, u64 site, u64 resume, u32 original, u64 table) {
    // Only x17 is free here: the table address is formed again for each entry.
    Writer w{at};
    std::array<size_t, MaxRemotes> found{};
    for (size_t i = 0; i < MaxRemotes; ++i) {
        w.Emit(Address(w.Here(), table + i * DrawEntrySize, X17));
        w.Emit(LoadImmediate(X17, X17, 0));
        w.Emit(Compare(X17, 27));
        found[i] = w.Reserve();
    }
    w.Emit(original);
    w.Emit(Branch(w.Here(), site + 4));
    std::array<size_t, MaxRemotes> to_copy{};
    for (size_t i = 0; i < MaxRemotes; ++i) {
        w.BindCond(found[i], Eq);
        w.Emit(Address(w.Here(), table + i * DrawEntrySize + DrawEntryMatrix, X17));
        to_copy[i] = w.Reserve();
    }
    for (const size_t index : to_copy) {
        w.BindBranch(index);
    }
    w.Emit(0x3DC00220); // ldr q0, [x17]
    w.Emit(0x3D8002A0); // str q0, [x21]
    w.Emit(0x3DC00620); // ldr q0, [x17, #16]
    w.Emit(0x3D8006A0); // str q0, [x21, #16]
    w.Emit(0x3DC00A20); // ldr q0, [x17, #32]
    w.Emit(0x3D800AA0); // str q0, [x21, #32]
    w.Emit(Branch(w.Here(), resume));
    return w.Take();
}

std::vector<u32> PlayFilter(u64 at, u64 play, u64 links) {
    Writer w{at};
    // The literal goes after the code; its address is known once the code length is.
    constexpr size_t CodeWords = 1 + 4 * MaxRemotes + 1 + 5 + 2;
    const u64 literal = (at + CodeWords * 4 + 7) & ~u64{7};
    w.Emit(LoadImmediate(X16, X0, 0)); // the request's actor
    std::array<size_t, MaxRemotes> remote{};
    for (size_t i = 0; i < MaxRemotes; ++i) {
        w.Emit(LoadLiteral(w.Here(), literal, X17));
        w.Emit(LoadImmediate(X17, X17, static_cast<u32>(i * 8)));
        w.Emit(Compare(X16, X17));
        remote[i] = w.Reserve();
    }
    const size_t to_play = w.Reserve();
    for (const size_t index : remote) {
        w.BindCond(index, Eq);
    }
    w.Emit(0x39408410); // ldrb w16, [x0, #0x21]
    w.Emit(0x71016A1F); // cmp w16, #0x5a
    const size_t ours = w.Reserve();
    w.Emit(0x2A1F03E0); // mov w0, wzr (refused)
    w.Emit(Ret);
    w.BindBranch(to_play);
    w.BindCond(ours, Eq);
    w.Emit(Pacibsp);
    w.Emit(Branch(w.Here(), play + 4));
    w.Align8();
    w.Emit64(links);
    return w.Take();
}

std::vector<u32> InputLoad(u64 at, u64 site, u32 original, u64 links,
                           const std::array<u64, MaxRemotes>& controllers) {
    Writer w{at};
    constexpr size_t CodeWords = 2 + 4 * MaxRemotes + 1 + 2 * MaxRemotes + 1;
    const u64 links_literal = (at + CodeWords * 4 + 7) & ~u64{7};
    const auto controller_literal = [&](size_t i) { return links_literal + 8 + i * 8; };
    w.Emit(original);                   // ldr x20, [x22, #0xb0]
    w.Emit(LoadImmediate(X16, 19, 0x18)); // the Link
    std::array<size_t, MaxRemotes> remote{};
    for (size_t i = 0; i < MaxRemotes; ++i) {
        w.Emit(LoadLiteral(w.Here(), links_literal, X17));
        w.Emit(LoadImmediate(X17, X17, static_cast<u32>(i * 8)));
        w.Emit(Compare(X16, X17));
        remote[i] = w.Reserve();
    }
    std::array<size_t, MaxRemotes + 1> back{};
    back[MaxRemotes] = w.Reserve();
    for (size_t i = 0; i < MaxRemotes; ++i) {
        w.BindCond(remote[i], Eq);
        w.Emit(LoadLiteral(w.Here(), controller_literal(i), X20));
        back[i] = w.Reserve();
    }
    for (const size_t index : back) {
        w.BindBranch(index);
    }
    w.Emit(Branch(w.Here(), site + 4));
    w.Align8();
    w.Emit64(links);
    for (const u64 controller : controllers) {
        w.Emit64(controller);
    }
    return w.Take();
}

std::vector<u32> DispatchHook(u64 at, u64 dispatch, u32 original, u64 scratch, u64 play,
                              u64 teleport, u64 create) {
    const u64 literal = at + 0xB8;
    std::vector<u32> words{
        0xA9BC7BFD,                          // stp x29, x30, [sp, #-0x40]!
        0xA90107E0,                          // stp x0, x1, [sp, #0x10]
        0xA9020FE2,                          // stp x2, x3, [sp, #0x20]
        0xA90323E4,                          // stp x4, x8, [sp, #0x30]
        LoadLiteral(at + 0x10, literal, 16), // ldr x16, scratch
        0xAA1F03F1,                          // mov x17, xzr
        0xF8F18200,                          // swpal x17, x0, [x16]
        0xB4000040,                          // cbz x0, +8
        BranchLink(at + 0x20, play),
        LoadLiteral(at + 0x24, literal, 16), // ldr x16, scratch
        0x91002210,                          // add x16, x16, #8
        0xAA1F03F1,                          // mov x17, xzr
        0xF8F18209,                          // swpal x17, x9, [x16]
        0xB40000C9,                          // cbz x9, +0x18
        0xF9400120,                          // ldr x0, [x9]
        0x91002121,                          // add x1, x9, #8
        0x91005122,                          // add x2, x9, #0x14
        0x52800023,                          // mov w3, #1
        BranchLink(at + 0x48, teleport),
        LoadLiteral(at + 0x4C, literal, 16), // ldr x16, scratch
        0x91004210,                          // add x16, x16, #0x10
        0xAA1F03F1,                          // mov x17, xzr
        0xF8F18209,                          // swpal x17, x9, [x16]
        0xB40001C9,                          // cbz x9, +0x38
        0xD10083FF,                          // sub sp, sp, #0x20 (three zero stack arguments)
        0xA9007FFF,                          // stp xzr, xzr, [sp]
        0xA9017FFF,                          // stp xzr, xzr, [sp, #0x10]
        0xF9400120,                          // ldr x0, [x9] (ActorMgr)
        0x91002121,                          // add x1, x9, #8 (&name pointer)
        0x91008122,                          // add x2, x9, #0x20 (parameters)
        0xAA1F03E3,                          // mov x3, xzr
        0x52800024,                          // mov w4, #1
        0xAA1F03E5,                          // mov x5, xzr
        0xAA1F03E6,                          // mov x6, xzr
        0xAA1F03E7,                          // mov x7, xzr
        BranchLink(at + 0x8C, create),
        0x910083FF, // add sp, sp, #0x20
        0xA94323E4, // ldp x4, x8, [sp, #0x30]
        0xA9420FE2, // ldp x2, x3, [sp, #0x20]
        0xA94107E0, // ldp x0, x1, [sp, #0x10]
        0xA8C47BFD, // ldp x29, x30, [sp], #0x40
        original,
        Branch(at + 0xA8, dispatch + 4),
    };
    words.resize(0xB8 / 4, 0);
    words.push_back(static_cast<u32>(scratch));
    words.push_back(static_cast<u32>(scratch >> 32));
    return words;
}

std::vector<u32> NaviFilter(u64 at, u64 update, u32 first, u64 main_navi, u64 updates) {
    std::vector<u32> words{
        0xF9413010,                         // ldr x16, [x0, #0x260] (the actor's name)
        0xB4000290,                         // cbz x16, run
        0xF9400211,                         // ldr x17, [x16]
        LoadLiteral(at + 0x0C, at + 0x60, 9), // ldr x9, ="Npc_Navi"
        0xEB09023F,                         // cmp x17, x9
        0x54000201,                         // b.ne run
        0x39402211,                         // ldrb w17, [x16, #8]
        0x350001D1,                         // cbnz w17, run
        LoadLiteral(at + 0x20, at + 0x68, 9), // ldr x9, =main Navi slot
        0xF9400129,                         // ldr x9, [x9]
        0xB4000169,                         // cbz x9, run (no Navi filtered)
        0xEB09001F,                         // cmp x0, x9
        0x54000120,                         // b.eq run (the scene's Navi)
        LoadLiteral(at + 0x34, at + 0x70, 16), // ldr x16, =counter
        0xB9400211,                         // ldr w17, [x16]
        0x340000B1,                         // cbz w17, skip
        0x51000631,                         // sub w17, w17, #1
        0xB9000211,                         // str w17, [x16]
        0x14000003,                         // b run
        0xD503201F,                         // nop
        Ret,                                // skip
        first,                              // run
        Branch(at + 0x58, update + 4),
        0,
    };
    u64 name;
    std::memcpy(&name, "Npc_Navi", 8);
    for (const u64 value : {name, main_navi, updates}) {
        words.push_back(static_cast<u32>(value));
        words.push_back(static_cast<u32>(value >> 32));
    }
    return words;
}

} // namespace Core::Nx2Multiplayer::Code
