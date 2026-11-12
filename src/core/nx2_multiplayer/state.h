// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <array>
#include <atomic>
#include <chrono>
#include <deque>
#include <mutex>
#include <optional>
#include <string>
#include <thread>
#include <vector>

#include <boost/asio.hpp>

#include "common/common_types.h"
#include "core/nx2_multiplayer/code.h"
#include "core/nx2_multiplayer/multiplayer.h"
#include "core/nx2_multiplayer/room.h"

namespace Core::Nx2Multiplayer {

using Clock = std::chrono::steady_clock;
using Endpoint = boost::asio::ip::udp::endpoint;

constexpr size_t MaxRemotes = Code::MaxRemotes;

// What the players send each other by UDP. The host relays: a joining player talks to the host
// only, and the host forwards each player's packets to the others. Both run the same game build,
// so a Bool flag's index in the GameDataMgr's table is the same everywhere.

constexpr u32 PlayerMagic = 0x32544F4F; // "OOT2"
constexpr u32 FlagMagic = 0x46544F4F;   // "OOTF"
constexpr u32 AckMagic = 0x41544F4F;    // "OOTA"
constexpr size_t FlagsPerPacket = 120;
constexpr u8 HostId = 0;
constexpr u8 NoId = 0xFF; ///< A joining player's own packets: the host fills in its id.

/// A player's Link, about 30 times a second.
struct PlayerPacket {
    u32 magic;
    u32 sequence;
    u8 id;
    u8 reserved[3];
    float position[3];
    float facing[2];
    char name[16];
    char action[32];
    u8 has_pad;
    u8 pad[0x138];
    float camera[2];
};

/// Bool flags that turned on, sent until acknowledged (each hop acknowledges).
struct FlagPacket {
    u32 magic;
    u32 sequence;
    u32 count;
    u16 indices[FlagsPerPacket];
};

struct AckPacket {
    u32 magic;
    u32 sequence;
};

/// Who this player sends to: for a joining player the host, for the host each joined player.
struct Peer {
    Endpoint endpoint;
    u8 id = HostId;
    Clock::time_point last_seen{};
    std::deque<FlagPacket> unacked;
    Clock::time_point last_flag_send{};
};

/// Another player, and the second Link and Navi that stand for them here.
struct Remote {
    u8 id = NoId; ///< NoId: a free slot.
    Player player;
    Clock::time_point time{};
    u64 link = 0;        ///< Their Link actor here, or 0.
    u64 spawned_for = 0; ///< The local Link it was created for (one per scene).
    u64 settled_actor = 0;
    Clock::time_point settled_since{};
    Clock::time_point last_spawn{};
    Clock::time_point last_correction{};
    Clock::time_point drift_since{};
    u64 navi = 0;
    Clock::time_point navi_requested{};
    Clock::time_point navi_born{};
    bool navi_set_up = false;
};

struct NetworkState {
    boost::asio::io_context io;
    std::optional<boost::asio::ip::udp::socket> socket;
    bool host = true;
    std::vector<Peer> peers;
    std::vector<Endpoint> candidates; ///< Room members not heard from yet (sent to, to open NATs).
    u32 sequence = 0;
    std::string status;
    std::optional<Rooms::Room> room;
    std::optional<Endpoint> stun_server;
    Clock::time_point last_stun{};
    bool restart = false; ///< StartRoom asked for a new socket and room.
    bool leave = false;   ///< LeaveRoom asked to stop.
    bool left = false;    ///< Left by the player: not opened again for a new game.
};

struct LinkState {
    u64 local = 0;     ///< The local Link actor, kept once found.
    int creating = -1; ///< The remote a Link is being created for, or -1.
    Clock::time_point create_time{};
};

struct NaviState {
    // Background heap scan for actors (their vtable), to find the Navis.
    std::mutex scan_mutex;
    std::vector<u64> scanned_actors;
    std::optional<std::thread> scanner;
    std::atomic<bool> scanning{false};
    std::atomic<bool> stop_scan{false};
    Clock::time_point last_scan{};
    u64 scene_link = 0; ///< The local Link the scan results belong to (a scene).
    u64 main = 0;       ///< The scene's own Navi.
    int creating = -1;  ///< The remote a Navi is being created for, or -1.
};

struct ProgressState {
    u64 entries = 0;
    u32 count = 0;
    std::vector<u8> values;
    Clock::time_point last_scan{};
    u32 sequence = 0;
    std::vector<u16> incoming;
};

struct PatchState {
    bool core_tried = false; ///< The core patches are tried once per game.
    bool core = false;
    bool input = false;
    bool navi_tried = false;
    bool navi = false;
};

struct State {
    u64 process_id = ~0ULL;
    u64 main_base = 0;
    NetworkState network;
    std::array<Remote, MaxRemotes> remotes;
    LinkState link;
    NaviState navi;
    ProgressState progress;
    PatchState patches;
};

/// The one multiplayer session (never destroyed: see Shutdown).
State& GetState();

/// An environment setting, or null when it is unset or empty.
const char* Env(const char* name);

/// Whether an on-by-default switch is on (unset, or anything but "0").
bool SwitchOn(const char* name);

/// The remote slot for a player id, taken if free; null when all are taken.
Remote* RemoteFor(State& state, u8 id);

/// Whether a remote slot is a player that sent something lately.
bool Active(const Remote& remote);

} // namespace Core::Nx2Multiplayer
