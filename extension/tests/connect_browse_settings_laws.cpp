#include "support/netw_test.h"

#include "netw/connect/core.hpp"
#include "netw/connect/webrtc_transport.hpp"

namespace TestNetwBrowseSettings {

using namespace godot;
using namespace netw::connect;

TEST_CASE("[Networked][Connect][Hosted] discovery validates before applying") {
    WebRTCTransport transport;
    Dictionary settings;
    settings["trackers"] = PackedStringArray();
    settings["signaling_namespace"] = String("private-game");
    CHECK(transport.set_browse_settings(settings) == OK);
    CHECK(!transport.can_browse());
    const Dictionary accepted = transport.client_settings();
    settings["trackers"] = int64_t(42);
    CHECK(transport.set_browse_settings(settings) == ERR_INVALID_PARAMETER);
    CHECK(transport.client_settings() == accepted);
    settings["trackers"] = PackedStringArray();
    settings["unknown"] = true;
    CHECK(transport.set_browse_settings(settings) == ERR_INVALID_PARAMETER);
    CHECK(transport.client_settings() == accepted);
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
