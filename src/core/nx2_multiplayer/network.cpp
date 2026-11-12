// SPDX-License-Identifier: GPL-3.0-or-later

#include <algorithm>
#include <cstdlib>
#include <cstring>
#include <string_view>

#include <fmt/format.h>

#include "common/logging.h"
#include "core/nx2_multiplayer/network.h"

namespace Core::Nx2Multiplayer::Network {
namespace {

using boost::asio::ip::udp;

constexpr auto Connected = std::chrono::seconds{2};
constexpr auto PeerTimeout = std::chrono::seconds{30};
constexpr size_t MaxUnacked = 64;

unsigned short Port() {
    const char* text = Env("EDEN_NX2_MP_PORT");
    return static_cast<unsigned short>(text ? std::atoi(text) : 7777);
}

std::optional<Endpoint> ParseAddress(const std::string& text) {
    const size_t colon = text.rfind(':');
    if (colon == std::string::npos) {
        return std::nullopt;
    }
    boost::system::error_code error;
    const auto address = boost::asio::ip::make_address_v4(text.substr(0, colon), error);
    if (error) {
        return std::nullopt;
    }
    return Endpoint{address, static_cast<unsigned short>(std::atoi(text.c_str() + colon + 1))};
}

Peer* FindPeer(NetworkState& network, const Endpoint& endpoint) {
    const auto it = std::ranges::find(network.peers, endpoint, &Peer::endpoint);
    return it == network.peers.end() ? nullptr : &*it;
}

void Send(NetworkState& network, const void* data, size_t size, const Endpoint& to) {
    boost::system::error_code error;
    network.socket->send_to(boost::asio::buffer(data, size), to, 0, error);
}

/// A joining player's first packet: it becomes a peer with the lowest free player id.
Peer* AddPeer(NetworkState& network, const Endpoint& from) {
    if (network.peers.size() >= MaxRemotes) {
        return nullptr;
    }
    u8 id = 1;
    while (std::ranges::any_of(network.peers, [id](const Peer& peer) { return peer.id == id; })) {
        ++id;
    }
    std::erase(network.candidates, from);
    network.peers.push_back({.endpoint = from, .id = id});
    LOG_INFO(Core, "Nx2 multiplayer: player {} joined from {}", id, from.address().to_string());
    return &network.peers.back();
}

/// Learns the other players' addresses from the room board.
void FollowRoom(NetworkState& network) {
    std::vector<std::string> addresses;
    {
        std::scoped_lock lock{network.room->mutex};
        addresses = network.room->peer_addresses;
    }
    if (!network.host) {
        if (addresses.empty()) {
            return;
        }
        const auto host = ParseAddress(addresses.front());
        if (!host) {
            return;
        }
        if (network.peers.empty()) {
            network.peers.push_back({.endpoint = *host, .id = HostId});
        } else if (network.peers.front().endpoint != *host) {
            network.peers.front().endpoint = *host; // the host came back from elsewhere
        }
        return;
    }
    network.candidates.clear();
    for (const std::string& text : addresses) {
        if (const auto address = ParseAddress(text); address && !FindPeer(network, *address)) {
            network.candidates.push_back(*address);
        }
    }
}

void SendLocal(NetworkState& network, const Player& local) {
    if (!local.valid) {
        return;
    }
    PlayerPacket packet{};
    packet.magic = PlayerMagic;
    packet.sequence = ++network.sequence;
    packet.id = network.host ? HostId : NoId;
    std::memcpy(packet.position, local.position.data(), sizeof(packet.position));
    std::memcpy(packet.facing, local.facing.data(), sizeof(packet.facing));
    const char* name = Env("EDEN_NX2_MP_NAME");
    std::strncpy(packet.name, name ? name : "P1", sizeof(packet.name) - 1);
    std::strncpy(packet.action, local.action.c_str(), sizeof(packet.action) - 1);
    packet.has_pad = local.has_pad ? 1 : 0;
    std::memcpy(packet.pad, local.pad.data(), sizeof(packet.pad));
    std::memcpy(packet.camera, local.camera.data(), sizeof(packet.camera));
    for (const Peer& peer : network.peers) {
        Send(network, &packet, sizeof(packet), peer.endpoint);
    }
    // Sending to the room's new players opens this side's router for their packets.
    for (const Endpoint& candidate : network.candidates) {
        Send(network, &packet, sizeof(packet), candidate);
    }
}

void ReceivePlayer(State& state, PlayerPacket packet, const Endpoint& from) {
    NetworkState& network = state.network;
    const auto now = Clock::now();
    if (network.host) {
        Peer* peer = FindPeer(network, from);
        if (peer == nullptr && (peer = AddPeer(network, from)) == nullptr) {
            return; // the room is full
        }
        peer->last_seen = now;
        packet.id = peer->id;
        for (const Peer& other : network.peers) {
            if (other.endpoint != from) {
                Send(network, &packet, sizeof(packet), other.endpoint);
            }
        }
    } else {
        if (packet.id == NoId) {
            return;
        }
        if (!network.peers.empty()) {
            network.peers.front().last_seen = now;
        }
    }
    Remote* const remote = RemoteFor(state, packet.id);
    if (remote == nullptr) {
        return;
    }
    packet.name[sizeof(packet.name) - 1] = '\0';
    packet.action[sizeof(packet.action) - 1] = '\0';
    Player& player = remote->player;
    player.valid = true;
    std::memcpy(player.position.data(), packet.position, sizeof(packet.position));
    std::memcpy(player.facing.data(), packet.facing, sizeof(packet.facing));
    player.action = packet.action;
    player.name = packet.name;
    player.has_pad = packet.has_pad != 0;
    std::memcpy(player.pad.data(), packet.pad, sizeof(packet.pad));
    std::memcpy(player.camera.data(), packet.camera, sizeof(packet.camera));
    remote->time = now;
}

void ReceiveFlags(State& state, const FlagPacket& flags, const Endpoint& from) {
    const u32 count = (std::min)(flags.count, static_cast<u32>(FlagsPerPacket));
    state.progress.incoming.insert(state.progress.incoming.end(), flags.indices,
                                   flags.indices + count);
    const AckPacket ack{AckMagic, flags.sequence};
    Send(state.network, &ack, sizeof(ack), from);
    if (state.network.host) {
        QueueFlags(state, {flags.indices, count}, &from);
    }
}

void Receive(State& state) {
    NetworkState& network = state.network;
    for (;;) {
        std::array<u8, 2048> datagram;
        Endpoint from;
        boost::system::error_code error;
        const size_t size = network.socket->receive_from(boost::asio::buffer(datagram), from, 0,
                                                         error);
        if (error) {
            break;
        }
        if (network.room && network.stun_server && from == *network.stun_server) {
            if (const auto address = Rooms::StunAddress({datagram.data(), size})) {
                std::scoped_lock lock{network.room->mutex};
                if (network.room->public_address != *address) {
                    network.room->public_address = *address;
                    LOG_INFO(Core, "Nx2 multiplayer: room {}: this player is at {}",
                             network.room->code, *address);
                }
            }
            continue;
        }
        u32 magic = 0;
        if (size >= sizeof(magic)) {
            std::memcpy(&magic, datagram.data(), sizeof(magic));
        }
        if (magic == PlayerMagic && size == sizeof(PlayerPacket)) {
            PlayerPacket packet;
            std::memcpy(&packet, datagram.data(), sizeof(packet));
            ReceivePlayer(state, packet, from);
        } else if (magic == FlagMagic && size == sizeof(FlagPacket)) {
            FlagPacket flags;
            std::memcpy(&flags, datagram.data(), sizeof(flags));
            ReceiveFlags(state, flags, from);
        } else if (magic == AckMagic && size == sizeof(AckPacket)) {
            AckPacket ack;
            std::memcpy(&ack, datagram.data(), sizeof(ack));
            if (Peer* const peer = FindPeer(network, from)) {
                std::erase_if(peer->unacked, [&](const FlagPacket& sent) {
                    return sent.sequence == ack.sequence;
                });
            }
        }
    }
}

/// Sends what is not acknowledged yet, again every half second.
void ResendFlags(NetworkState& network) {
    const auto now = Clock::now();
    for (Peer& peer : network.peers) {
        if (peer.unacked.empty() || now - peer.last_flag_send < std::chrono::milliseconds{500}) {
            continue;
        }
        peer.last_flag_send = now;
        for (const FlagPacket& packet : peer.unacked) {
            Send(network, &packet, sizeof(packet), peer.endpoint);
        }
    }
}

void UpdateStatus(NetworkState& network) {
    const auto now = Clock::now();
    if (network.host) {
        std::erase_if(network.peers, [&](const Peer& peer) {
            const bool gone = now - peer.last_seen > PeerTimeout;
            if (gone) {
                LOG_INFO(Core, "Nx2 multiplayer: player {} left", peer.id);
            }
            return gone;
        });
    }
    const auto active = std::ranges::count_if(
        network.peers, [&](const Peer& peer) { return now - peer.last_seen < Connected; });
    if (network.room) {
        network.room->connected = active > 0;
    }
    const std::string where = network.room ? fmt::format("sala {}", network.room->code)
                                            : fmt::format("UDP {}", Port());
    if (network.host) {
        network.status = active == 0 ? fmt::format("{}: esperando jogadores", where)
                         : active == 1
                             ? fmt::format("{}: 1 jogador conectado", where)
                             : fmt::format("{}: {} jogadores conectados", where, active);
    } else if (active > 0) {
        network.status = fmt::format("{}: conectado", where);
    } else if (network.peers.empty()) {
        network.status = fmt::format("{}: procurando o host", where);
    } else {
        network.status = fmt::format("{}: conectando a {}", where,
                                     network.peers.front().endpoint.address().to_string());
    }
}

} // Anonymous namespace

void Open(State& state) {
    NetworkState& network = state.network;
    const unsigned short port = Port();
    try {
        network.socket.emplace(network.io, udp::endpoint(udp::v4(), port));
        network.socket->non_blocking(true);
        network.host = true;
        if (const char* peer = Env("EDEN_NX2_MP_PEER")) {
            // Straight to a host: "ip:port" (port 7777 when left out).
            const std::string text{peer};
            const size_t colon = text.rfind(':');
            udp::resolver resolver{network.io};
            const auto host = *resolver
                                   .resolve(udp::v4(), text.substr(0, colon),
                                            colon == std::string::npos ? "7777"
                                                                       : text.substr(colon + 1))
                                   .begin();
            network.host = false;
            network.peers.push_back({.endpoint = host, .id = HostId});
        } else if (const char* room = Env("EDEN_NX2_MP_ROOM")) {
            network.host = std::string_view{room} == "HOST";
            network.room.emplace();
            network.room->host = network.host;
            network.room->code = network.host ? Rooms::NewCode() : std::string{room};
            udp::resolver resolver{network.io};
            network.stun_server =
                *resolver.resolve(udp::v4(), "stun.l.google.com", "19302").begin();
            Rooms::Start(*network.room);
        }
        UpdateStatus(network);
    } catch (const std::exception& error) {
        network.status = fmt::format("erro de rede: {}", error.what());
        network.socket.reset();
    }
    LOG_INFO(Core, "Nx2 multiplayer: {}", network.status);
}

void Close(State& state) {
    NetworkState& network = state.network;
    if (network.room) {
        Rooms::Stop(*network.room);
        network.room.reset();
    }
    network.socket.reset();
    network.peers.clear();
    network.candidates.clear();
    network.stun_server.reset();
    network.last_stun = {};
    network.status = "fora da sala";
    for (Remote& remote : state.remotes) {
        remote.player.valid = false;
        remote.time = {};
    }
}

void Exchange(State& state, const Player& local) {
    NetworkState& network = state.network;
    if (!network.socket) {
        return;
    }
    const auto now = Clock::now();
    if (network.room) {
        FollowRoom(network);
        // This socket's public address (again every 20 s, which also keeps the router's mapping
        // open), for the board.
        bool known;
        {
            std::scoped_lock lock{network.room->mutex};
            known = !network.room->public_address.empty();
        }
        if (network.stun_server && now - network.last_stun > std::chrono::seconds{known ? 20 : 1}) {
            network.last_stun = now;
            const auto request = Rooms::StunRequest();
            Send(network, request.data(), request.size(), *network.stun_server);
        }
    }
    SendLocal(network, local);
    Receive(state);
    ResendFlags(network);
    UpdateStatus(network);
}

void QueueFlags(State& state, std::span<const u16> indices, const Endpoint* except) {
    for (Peer& peer : state.network.peers) {
        if (except != nullptr && peer.endpoint == *except) {
            continue;
        }
        for (size_t at = 0; at < indices.size(); at += FlagsPerPacket) {
            FlagPacket packet{FlagMagic, ++state.progress.sequence, 0, {}};
            packet.count = static_cast<u32>((std::min)(FlagsPerPacket, indices.size() - at));
            std::copy_n(indices.begin() + static_cast<std::ptrdiff_t>(at), packet.count,
                        packet.indices);
            peer.unacked.push_back(packet);
        }
        while (peer.unacked.size() > MaxUnacked) {
            peer.unacked.pop_front();
        }
        peer.last_flag_send = {}; // send now
    }
}

} // namespace Core::Nx2Multiplayer::Network
