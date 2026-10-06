#include "support/netw_test.h"

#include "netw/api/nodes/webtorrent_lobby_directory.hpp"
#include "netw/connect/core.hpp"
#include "netw/connect/webrtc_transport.hpp"

namespace TestNetwBrowseSettings {

using namespace godot;
using namespace netw::connect;

TEST_CASE("[Networked][Connect][Hosted] discovery validates before applying") {
    netw::WebTorrentLobbyDirectory *board
        = memnew(netw::WebTorrentLobbyDirectory);
    PackedStringArray urls;
    urls.push_back(String("ws://127.0.0.1:1"));
    Dictionary settings;
    settings["trackers"] = urls;
    settings["signaling_namespace"] = String("private-game");
    CHECK(board->set_browse_settings(settings) == OK);
    settings["trackers"] = int64_t(42);
    CHECK(board->set_browse_settings(settings) == ERR_INVALID_PARAMETER);
    CHECK(board->get_trackers() == urls);
    settings["trackers"] = PackedStringArray();
    settings["unknown"] = true;
    CHECK(board->set_browse_settings(settings) == ERR_INVALID_PARAMETER);
    CHECK(board->get_trackers() == urls);
    memdelete(board);
}

TEST_CASE(
    "[Networked][Connect][Hosted] discovery owns settings after refresh"
) {
    ConnectCore core;
    const RID transport
        = TransportBook::shared().slot_of(StringName("WebRTCMultiplayerPeer"));
    REQUIRE(transport.is_valid());
    Dictionary settings;
    settings["trackers"] = PackedStringArray();
    settings["signaling_namespace"] = String("private-game");
    CHECK(core.set_browse_settings(transport, settings) == OK);
    const Dictionary accepted = settings.duplicate(true);
    settings["signaling_namespace"] = String("mutated");
    core.refresh();
    CHECK(core.set_browse_settings(transport, accepted) == OK);
    CHECK(core.set_browse_settings(transport, settings) == ERR_ALREADY_IN_USE);
}

TEST_CASE("[Networked][Connect][Hosted] explicit empty ICE stays empty") {
    CHECK(WebRTCTransport::servers_differ_from_default(Array()));
    CHECK(!WebRTCTransport::servers_differ_from_default(
        WebRTCTransport::default_ice_servers()
    ));
}

} // namespace TestNetwBrowseSettings
