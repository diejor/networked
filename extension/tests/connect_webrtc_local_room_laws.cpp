#include "support/netw_test.h"

#if defined(NETW_TIER_HOSTED)

#include "netw/connect/webrtc_transport.hpp"

namespace TestWebRTCLocalRoom {

using godot::String;
using netw::connect::WebRTCTransport;

TEST_CASE(
    "[Networked][Connect][Room] a room hosted on this machine is "
    "remembered and forgotten, because a same-machine join must skip the "
    "relay configuration rather than warn its way through it"
) {
    const String room = "netwtestroom00000001";
    CHECK(!WebRTCTransport::is_local_room(room));

    WebRTCTransport::register_local_room(room);
    CHECK(WebRTCTransport::is_local_room(room));

    WebRTCTransport::register_local_room(room);
    WebRTCTransport::unregister_local_room(room);
    CHECK(!WebRTCTransport::is_local_room(room));

    WebRTCTransport::register_local_room(String());
    CHECK(!WebRTCTransport::is_local_room(String()));
}

} // namespace TestWebRTCLocalRoom

#endif
