// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <optional>
#include <string>

/// A minimal UPnP IGD client: asks the home router to forward a UDP port to this PC, so a player
/// hosting the Ocarina of Time multiplayer prototype can be joined from the internet.
namespace Core::Nx2Upnp {

struct Mapping {
    std::string external_ip; ///< The router's public address, as it reports it.
    std::string local_ip;    ///< This PC's address on the router's network.
};

/// Blocks for a few seconds. Returns nothing when no UPnP router answers or it refuses.
std::optional<Mapping> ForwardUdpPort(unsigned short port);

} // namespace Core::Nx2Upnp
