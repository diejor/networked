#include "support/netw_test.h"

#include "netw/api/netw_multiplayer.hpp"

using namespace godot;

namespace TestNetwSessionSendTo {

using godot::PackedByteArray;
using godot::Ref;
using godot::String;
using netw::NetwMultiplayerCore;

constexpr int64_t SYNC_CHANNEL = 19;
constexpr int64_t CLOCK_PING_CHANNEL = 11;
constexpr int64_t PEER = 5;

PackedByteArray payload(int p_size) {
    PackedByteArray out;
    out.resize(p_size);
    for (int index = 0; index < p_size; ++index) {
        out.set(index, uint8_t(index));
    }
    return out;
}

Ref<NetwMultiplayerCore> configured_core() {
    Ref<NetwMultiplayerCore> core;
    core.instantiate();
    core->get_clock_handle()->engine.set_configured(true);
    return core;
}

TEST_CASE(
    "[Networked][Transport][Hosted] S1 a frame on an aggregating channel waits "
    "in the peer's run rather than leaving on its own"
) {
    const Ref<NetwMultiplayerCore> core = configured_core();

    NETW_CHECK_EQ(
        core->send_to(PEER, 3, SYNC_CHANNEL, payload(4), false, 0, String(), false),
        godot::OK
    );

    NETW_CHECK_EQ(core->carrier_pending(PEER, false), 8);
    NETW_CHECK_EQ(core->carrier_pending(PEER, true), 0);
}

TEST_CASE(
    "[Networked][Transport][Hosted] S2 an unconfigured clock never aggregates, "
    "because the tick pump is what would have flushed the run"
) {
    Ref<NetwMultiplayerCore> core;
    core.instantiate();

    core->send_to(PEER, 3, SYNC_CHANNEL, payload(4), false, 0, String(), false);

    NETW_CHECK_EQ(core->carrier_pending(PEER, false), 0);
}

TEST_CASE(
    "[Networked][Transport][Hosted] S3 an immediate channel leaves now even "
    "with a running clock, and even when the sender asks to batch"
) {
    const Ref<NetwMultiplayerCore> core = configured_core();

    core->send_to(
        PEER,
        0,
        CLOCK_PING_CHANNEL,
        payload(4),
        false,
        0,
        String(),
        true
    );

    NETW_CHECK_EQ(core->carrier_pending(PEER, false), 0);
}

TEST_CASE(
    "[Networked][Transport][Hosted] S4 a negative route is refused and stages "
    "nothing, because it addresses no entity and no peer-scoped stream"
) {
    const Ref<NetwMultiplayerCore> core = configured_core();

    NETW_CHECK_EQ(
        core->send_to(PEER, -1, SYNC_CHANNEL, payload(4), false, 0, String(), false),
        godot::ERR_INVALID_PARAMETER
    );

    NETW_CHECK_EQ(core->carrier_pending(PEER, false), 0);
}

TEST_CASE(
    "[Networked][Transport][Hosted] S5 the reliable and unreliable runs of one "
    "peer are separate, so a reliable frame never rides an unreliable stamp"
) {
    const Ref<NetwMultiplayerCore> core = configured_core();

    core->send_to(PEER, 3, SYNC_CHANNEL, payload(4), true, 0, String(), false);

    NETW_CHECK_EQ(core->carrier_pending(PEER, true), 8);
    NETW_CHECK_EQ(core->carrier_pending(PEER, false), 0);
}

} // namespace TestNetwSessionSendTo
