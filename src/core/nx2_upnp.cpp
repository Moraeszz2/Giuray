// SPDX-License-Identifier: GPL-3.0-or-later

#include <array>
#include <cctype>
#include <chrono>
#include <string_view>
#include <thread>

#include <boost/asio.hpp>
#include <fmt/format.h>

#include "common/logging.h"
#include "core/nx2_upnp.h"

namespace Core::Nx2Upnp {
namespace {

using boost::asio::ip::tcp;
using boost::asio::ip::udp;

struct Url {
    std::string host;
    std::string port;
    std::string path;
};

std::optional<Url> ParseUrl(std::string_view url) {
    constexpr std::string_view scheme = "http://";
    if (!url.starts_with(scheme)) {
        return std::nullopt;
    }
    url.remove_prefix(scheme.size());
    const size_t slash = url.find('/');
    const std::string_view authority = url.substr(0, slash);
    const size_t colon = authority.find(':');
    return Url{std::string{authority.substr(0, colon)},
               colon == std::string_view::npos ? "80" : std::string{authority.substr(colon + 1)},
               slash == std::string_view::npos ? "/" : std::string{url.substr(slash)}};
}

/// The value of an HTTP header line ("Name: value"), name compared without case.
std::string Header(std::string_view response, std::string_view name) {
    size_t pos = 0;
    while (pos < response.size()) {
        const size_t end = response.find("\r\n", pos);
        const std::string_view line = response.substr(
            pos, end == std::string_view::npos ? std::string_view::npos : end - pos);
        if (line.size() > name.size() && line[name.size()] == ':') {
            bool same = true;
            for (size_t i = 0; i < name.size() && same; ++i) {
                same = std::tolower(static_cast<unsigned char>(line[i])) ==
                       std::tolower(static_cast<unsigned char>(name[i]));
            }
            if (same) {
                std::string_view value = line.substr(name.size() + 1);
                while (!value.empty() && value.front() == ' ') {
                    value.remove_prefix(1);
                }
                return std::string{value};
            }
        }
        if (end == std::string_view::npos) {
            break;
        }
        pos = end + 2;
    }
    return {};
}

/// The text between <tag> and </tag> after `from`.
std::string Element(std::string_view xml, std::string_view tag, size_t from = 0) {
    const std::string open = fmt::format("<{}>", tag);
    const std::string close = fmt::format("</{}>", tag);
    const size_t start = xml.find(open, from);
    if (start == std::string_view::npos) {
        return {};
    }
    const size_t end = xml.find(close, start);
    if (end == std::string_view::npos) {
        return {};
    }
    return std::string{xml.substr(start + open.size(), end - start - open.size())};
}

/// One HTTP request, the whole response back; `local` gets this PC's address on that network.
std::string Http(const Url& url, const std::string& request, std::string* local) {
    boost::asio::io_context io;
    tcp::socket socket{io};
    tcp::resolver resolver{io};
    boost::asio::connect(socket, resolver.resolve(url.host, url.port));
    if (local) {
        *local = socket.local_endpoint().address().to_string();
    }
    boost::asio::write(socket, boost::asio::buffer(request));
    std::string response;
    boost::system::error_code error;
    std::array<char, 4096> chunk;
    for (;;) {
        const size_t read = socket.read_some(boost::asio::buffer(chunk), error);
        response.append(chunk.data(), read);
        if (error) {
            break;
        }
    }
    return response;
}

std::string Soap(const Url& control, std::string_view service, std::string_view action,
                 std::string_view arguments) {
    const std::string body = fmt::format(
        "<?xml version=\"1.0\"?><s:Envelope xmlns:s=\"http://schemas.xmlsoap.org/soap/envelope/\" "
        "s:encodingStyle=\"http://schemas.xmlsoap.org/soap/encoding/\"><s:Body><u:{} "
        "xmlns:u=\"{}\">{}</u:{}></s:Body></s:Envelope>",
        action, service, arguments, action);
    const std::string request = fmt::format(
        "POST {} HTTP/1.1\r\nHost: {}:{}\r\nContent-Type: text/xml; charset=\"utf-8\"\r\n"
        "SOAPAction: \"{}#{}\"\r\nContent-Length: {}\r\nConnection: close\r\n\r\n{}",
        control.path, control.host, control.port, service, action, body.size(), body);
    return Http(control, request, nullptr);
}

} // Anonymous namespace

std::optional<Mapping> ForwardUdpPort(unsigned short port) {
    try {
        // SSDP: ask the network's Internet gateway devices where their description is.
        boost::asio::io_context io;
        udp::socket socket{io, udp::endpoint(udp::v4(), 0)};
        const std::string search =
            "M-SEARCH * HTTP/1.1\r\nHOST: 239.255.255.250:1900\r\nMAN: \"ssdp:discover\"\r\n"
            "MX: 2\r\nST: urn:schemas-upnp-org:device:InternetGatewayDevice:1\r\n\r\n";
        socket.send_to(boost::asio::buffer(search),
                       udp::endpoint(boost::asio::ip::make_address("239.255.255.250"), 1900));
        socket.non_blocking(true);
        std::string location;
        const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds{3};
        while (location.empty() && std::chrono::steady_clock::now() < deadline) {
            std::array<char, 2048> reply;
            udp::endpoint from;
            boost::system::error_code error;
            const size_t size = socket.receive_from(boost::asio::buffer(reply), from, 0, error);
            if (error) {
                std::this_thread::sleep_for(std::chrono::milliseconds{50});
                continue;
            }
            location = Header(std::string_view{reply.data(), size}, "location");
        }
        const auto description_url = ParseUrl(location);
        if (!description_url) {
            LOG_INFO(Core, "Nx2 multiplayer: no UPnP router answered");
            return std::nullopt;
        }
        std::string local_ip;
        const std::string description =
            Http(*description_url,
                 fmt::format("GET {} HTTP/1.1\r\nHost: {}:{}\r\nConnection: close\r\n\r\n",
                             description_url->path, description_url->host, description_url->port),
                 &local_ip);
        // The WAN connection service (IP or PPP) and its control URL.
        std::string service;
        std::string control_path;
        for (const std::string_view type : {"urn:schemas-upnp-org:service:WANIPConnection:1",
                                            "urn:schemas-upnp-org:service:WANIPConnection:2",
                                            "urn:schemas-upnp-org:service:WANPPPConnection:1"}) {
            const size_t at = description.find(fmt::format("<serviceType>{}</serviceType>", type));
            if (at != std::string::npos) {
                service = type;
                control_path = Element(description, "controlURL", at);
                break;
            }
        }
        if (service.empty() || control_path.empty()) {
            LOG_INFO(Core, "Nx2 multiplayer: the UPnP router has no WAN connection service");
            return std::nullopt;
        }
        Url control = *description_url;
        if (const auto absolute = ParseUrl(control_path)) {
            control = *absolute;
        } else {
            control.path = control_path.starts_with('/') ? control_path : "/" + control_path;
        }
        const std::string added = Soap(
            control, service, "AddPortMapping",
            fmt::format("<NewRemoteHost></NewRemoteHost><NewExternalPort>{}</NewExternalPort>"
                        "<NewProtocol>UDP</NewProtocol><NewInternalPort>{}</NewInternalPort>"
                        "<NewInternalClient>{}</NewInternalClient><NewEnabled>1</NewEnabled>"
                        "<NewPortMappingDescription>Lichii multiplayer</NewPortMappingDescription>"
                        "<NewLeaseDuration>0</NewLeaseDuration>",
                        port, port, local_ip));
        if (added.find(" 200 ") == std::string::npos) {
            LOG_INFO(Core, "Nx2 multiplayer: the UPnP router refused to forward UDP {}", port);
            return std::nullopt;
        }
        const std::string external =
            Element(Soap(control, service, "GetExternalIPAddress", ""), "NewExternalIPAddress");
        LOG_INFO(Core, "Nx2 multiplayer: UPnP forwards UDP {} from {} to {}", port, external,
                 local_ip);
        return Mapping{external, local_ip};
    } catch (const std::exception& error) {
        LOG_INFO(Core, "Nx2 multiplayer: UPnP failed: {}", error.what());
        return std::nullopt;
    }
}

} // namespace Core::Nx2Upnp
