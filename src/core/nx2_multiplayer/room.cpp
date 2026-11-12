// SPDX-License-Identifier: GPL-3.0-or-later

#include <algorithm>
#include <chrono>
#include <random>
#include <string_view>
#include <vector>

#include <boost/asio.hpp>
#include <fmt/format.h>

#include "common/logging.h"
#include "core/nx2_multiplayer/room.h"

namespace Core::Nx2Multiplayer::Rooms {
namespace {

constexpr u32 StunCookie = 0x2112A442;
constexpr std::string_view Board = "ntfy.sh";

/// One plain HTTP request to the board; the body of the answer.
std::string Http(std::string_view method, std::string_view path, std::string_view body) {
    using boost::asio::ip::tcp;
    boost::asio::io_context io;
    tcp::socket socket{io};
    tcp::resolver resolver{io};
    boost::asio::connect(socket, resolver.resolve(std::string{Board}, "80"));
    const std::string request = fmt::format(
        "{} {} HTTP/1.1\r\nHost: {}\r\nContent-Length: {}\r\nConnection: close\r\n\r\n{}", method,
        path, Board, body.size(), body);
    boost::asio::write(socket, boost::asio::buffer(request));
    std::string response;
    std::array<char, 4096> chunk;
    boost::system::error_code error;
    for (;;) {
        const size_t read = socket.read_some(boost::asio::buffer(chunk), error);
        response.append(chunk.data(), read);
        if (error) {
            break;
        }
    }
    const size_t body_start = response.find("\r\n\r\n");
    return body_start == std::string::npos ? std::string{} : response.substr(body_start + 4);
}

/// The "message" fields of the board's JSON lines, newest last.
std::vector<std::string> Messages(std::string_view json_lines) {
    std::vector<std::string> messages;
    constexpr std::string_view key = "\"message\":\"";
    size_t pos = 0;
    while ((pos = json_lines.find(key, pos)) != std::string_view::npos) {
        pos += key.size();
        const size_t end = json_lines.find('"', pos);
        if (end == std::string_view::npos) {
            break;
        }
        messages.emplace_back(json_lines.substr(pos, end - pos));
        pos = end;
    }
    return messages;
}

void BoardLoop(Room& room) {
    const std::string topic = fmt::format("/giuray-oot-{}", room.code);
    const char own = room.host ? 'H' : 'J';
    const char other = room.host ? 'J' : 'H';
    std::string announced;
    auto last_announce = std::chrono::steady_clock::time_point{};
    while (!room.stop) {
        try {
            std::string address;
            {
                std::scoped_lock lock{room.mutex};
                address = room.public_address;
            }
            // Announce this player's address (again whenever it changes, and now and then).
            if (!address.empty()) {
                const std::string message = fmt::format("{} {}", own, address);
                if (message != announced ||
                    std::chrono::steady_clock::now() - last_announce > std::chrono::minutes{2}) {
                    Http("POST", topic, message);
                    announced = message;
                    last_announce = std::chrono::steady_clock::now();
                }
            }
            // The others' addresses on the board (the last 10 minutes): every joining player's
            // for the host, the host's latest for a joining player.
            std::vector<std::string> found;
            for (const std::string& message :
                 Messages(Http("GET", topic + "/json?poll=1&since=10m", ""))) {
                if (message.size() > 2 && message[0] == other && message[1] == ' ') {
                    const std::string peer = message.substr(2);
                    if (!room.host) {
                        found = {peer};
                    } else if (std::ranges::find(found, peer) == found.end()) {
                        found.push_back(peer);
                    }
                }
            }
            {
                std::scoped_lock lock{room.mutex};
                for (const std::string& peer : found) {
                    if (std::ranges::find(room.peer_addresses, peer) ==
                        room.peer_addresses.end()) {
                        LOG_INFO(Core, "Nx2 multiplayer: room {}: a player is at {}", room.code,
                                 peer);
                    }
                }
                if (!found.empty()) {
                    room.peer_addresses = std::move(found);
                }
            }
        } catch (const std::exception& error) {
            LOG_WARNING(Core, "Nx2 multiplayer: room {}: {}", room.code, error.what());
        }
        // The board allows about one request every 5 s. Once connected, a joining player looks
        // every 30 s (the host may come back from another address), the host every 10 s (more
        // players may join).
        const int tenths = !room.connected ? 50 : room.host ? 100 : 300;
        for (int i = 0; i < tenths && !room.stop; ++i) {
            std::this_thread::sleep_for(std::chrono::milliseconds{100});
        }
    }
}

} // Anonymous namespace

std::string NewCode() {
    constexpr std::string_view letters = "ABCDEFGHJKLMNPQRSTUVWXYZ23456789";
    std::random_device device;
    std::string code;
    for (int i = 0; i < 6; ++i) {
        code += letters[device() % letters.size()];
    }
    return code;
}

void Start(Room& room) {
    room.board.emplace([&room] { BoardLoop(room); });
}

void Stop(Room& room) {
    room.stop = true;
    if (room.board && room.board->joinable()) {
        room.board->join();
    }
    room.board.reset();
}

std::array<u8, 20> StunRequest() {
    std::array<u8, 20> request{};
    request[1] = 0x01; // binding request, no attributes
    request[4] = static_cast<u8>(StunCookie >> 24);
    request[5] = static_cast<u8>(StunCookie >> 16);
    request[6] = static_cast<u8>(StunCookie >> 8);
    request[7] = static_cast<u8>(StunCookie);
    std::random_device device;
    for (size_t i = 8; i < 20; ++i) {
        request[i] = static_cast<u8>(device());
    }
    return request;
}

std::optional<std::string> StunAddress(std::span<const u8> data) {
    const auto u16_at = [&](size_t at) { return static_cast<u32>(data[at] << 8 | data[at + 1]); };
    if (data.size() < 20 || u16_at(0) != 0x0101 || (u16_at(4) << 16 | u16_at(6)) != StunCookie) {
        return std::nullopt;
    }
    for (size_t at = 20; at + 4 <= data.size();) {
        const u32 type = u16_at(at);
        const u32 length = u16_at(at + 2);
        if (at + 4 + length > data.size()) {
            break;
        }
        // XOR-MAPPED-ADDRESS (or the plain MAPPED-ADDRESS), IPv4.
        if ((type == 0x0020 || type == 0x0001) && length >= 8 && data[at + 5] == 0x01) {
            const bool xored = type == 0x0020;
            const u32 port = u16_at(at + 6) ^ (xored ? StunCookie >> 16 : 0);
            std::array<u8, 4> ip;
            for (size_t i = 0; i < 4; ++i) {
                ip[i] =
                    data[at + 8 + i] ^ (xored ? static_cast<u8>(StunCookie >> (24 - 8 * i)) : 0);
            }
            return fmt::format("{}.{}.{}.{}:{}", ip[0], ip[1], ip[2], ip[3], port);
        }
        at += 4 + ((length + 3) & ~3u);
    }
    return std::nullopt;
}

} // namespace Core::Nx2Multiplayer::Rooms
