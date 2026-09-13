#include "support/loopback_rig.h"

#if defined(NETW_TIER_HOSTED)

#include "netw/api/netw_multiplayer.hpp"
#include "netw/api/replication_core.hpp"
#include "netw/wire/attribution.hpp"
#include "netw/wire/registry.hpp"

namespace TestWireRefusalSessionLaws {

using namespace godot;
using netw::NetwMultiplayer;
using netw_test::LoopbackRig;

int64_t verdict_count(
    const Dictionary &p_snapshot,
    int64_t p_peer,
    int64_t p_channel,
    const char *p_verdict
) {
    const Dictionary refusals = p_snapshot[StringName("refusals")];
    if (!refusals.has(p_peer)) {
        return 0;
    }
    const Dictionary channels = refusals[p_peer];
    if (!channels.has(p_channel)) {
        return 0;
    }
    const Dictionary verdicts = channels[p_channel];
    return verdicts.has(String(p_verdict))
        ? int64_t(verdicts[String(p_verdict)])
        : 0;
}

TEST_CASE(
    "[Networked][Wire][SceneTree] FS1 the residual is reported as its own row "
    "beside the datagram counter and the attributed sum, never distributed "
    "across the channels that did not spend it"
) {
    LoopbackRig rig(1);
    rig.step_ticks(6);

    NetwMultiplayer *host = rig.server();
    const Dictionary snapshot = host->attribution_snapshot();
    const Dictionary residual = snapshot[StringName("residual")];

    const bool holds_rows = residual.has(StringName("datagram_bytes_out"))
        && residual.has(StringName("attributed_out"))
        && residual.has(StringName("framing_out"))
        && residual.has(StringName("staged_dropped_out"))
        && residual.has(StringName("residual_out"))
        && residual.has(StringName("datagram_bytes_in"))
        && residual.has(StringName("attributed_in"))
        && residual.has(StringName("residual_in"))
        && residual.has(StringName("wire_bytes_out"))
        && residual.has(StringName("wire_bytes_in"))
        && residual.has(StringName("datagrams_out"))
        && residual.has(StringName("datagrams_in"))
        && residual.has(StringName("carrier_overhead_out"))
        && residual.has(StringName("carrier_overhead_in"));
    CHECK(holds_rows);

    const bool wire_is_the_larger_count
        = int64_t(residual[StringName("wire_bytes_out")])
        >= int64_t(residual[StringName("datagram_bytes_out")]);
    CHECK(wire_is_the_larger_count);

    NETW_CHECK_EQ(
        int64_t(residual[StringName("datagram_bytes_out")]),
        host->get_sent_bytes()
    );
    NETW_CHECK_EQ(
        int64_t(residual[StringName("attributed_out")]),
        host->attribution_book().get_attributed_out()
    );

    const int64_t out = int64_t(residual[StringName("datagram_bytes_out")]);
    const int64_t attributed = int64_t(residual[StringName("attributed_out")]);
    const int64_t framing = int64_t(residual[StringName("framing_out")]);
    NETW_CHECK_EQ(
        int64_t(residual[StringName("residual_out")]),
        out - attributed - framing
    );
    const bool dropped_stands_apart
        = int64_t(residual[StringName("staged_dropped_out")])
        == host->attribution_book().get_staged_dropped_out();
    CHECK(dropped_stands_apart);

    NETW_CHECK_EQ(
        int64_t(residual[StringName("carrier_overhead_out")]),
        int64_t(residual[StringName("wire_bytes_out")]) - attributed - framing
    );
    NETW_CHECK_EQ(
        int64_t(residual[StringName("residual_in")]),
        host->get_received_bytes()
            - host->attribution_book().get_attributed_in()
    );
}

TEST_CASE(
    "[Networked][Wire][SceneTree] FS2 a frame for a route the receiver does "
    "not hold is counted against its sender and channel, because a datagram "
    "the delivery history says arrived is not a row the receiver applied"
) {
    LoopbackRig rig(1);
    rig.step_ticks(4);

    NetwMultiplayer *guest = rig.client(0);
    const int64_t before = guest->attribution_book().get_refused_in();
    const int64_t channel = netw::wire::builtin_channel(StringName("SYNC_ROW"));
    REQUIRE(channel > 0);

    PackedByteArray payload;
    payload.push_back(0x00);
    guest->get_replication_plane()
        ->dispatch(99991, 0, channel, payload, String(), 1, false, -1);

    const Dictionary snapshot = guest->attribution_snapshot();
    const int64_t unrouted = verdict_count(snapshot, 1, channel, "unrouted");
    const int64_t gated = verdict_count(snapshot, 1, channel, "gate");
    const bool counted = unrouted + gated >= 1;
    const bool total_moved
        = guest->attribution_book().get_refused_in() > before;
    CHECK(counted);
    CHECK(total_moved);
}

TEST_CASE(
    "[Networked][Wire][SceneTree] FS3 a live session's per peer and channel "
    "frame counts sum to the frames the book heard, so a refusal ratio is "
    "taken against a denominator the same pass wrote"
) {
    LoopbackRig rig(1);
    rig.step_ticks(4);

    NetwMultiplayer *guest = rig.client(0);
    const int64_t channel = netw::wire::builtin_channel(StringName("SYNC_ROW"));
    PackedByteArray payload;
    payload.push_back(0x00);
    for (int at = 0; at < 3; ++at) {
        guest->get_replication_plane()
            ->dispatch(99991 + at, 0, channel, payload, String(), 1, false, -1);
    }

    const Dictionary snapshot = guest->attribution_snapshot();
    const Dictionary frames = snapshot[StringName("frames_in_by_channel")];

    int64_t counted = 0;
    const Array peers = frames.keys();
    for (int at = 0; at < peers.size(); ++at) {
        const Dictionary channels = frames[peers[at]];
        const Array ids = channels.keys();
        for (int row = 0; row < ids.size(); ++row) {
            counted += int64_t(channels[ids[row]]);
        }
    }
    NETW_CHECK_EQ(counted, guest->attribution_book().get_frames_in());
    const bool heard = counted > 0;
    CHECK(heard);
}

} // namespace TestWireRefusalSessionLaws

#endif
