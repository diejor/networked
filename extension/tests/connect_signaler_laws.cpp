#include "support/netw_test.h"

#include "godot/local_vector.hpp"
#include "godot/variant.hpp"
#include "netw/api/webrtc_signaler.hpp"
#include "netw/connect/signaler.hpp"

namespace netw::connect {

struct TrackerSignalerProbe {
    static void arm(
        TrackerSignaler &p_signaler,
        const godot::String &p_room,
        int64_t p_peer
    ) {
        p_signaler.local_peer = p_peer;
        p_signaler.is_server = p_peer == 1;
        p_signaler.room = p_room;
        p_signaler.hash = TrackerSignaler::derived_info_hash(
            p_signaler.signaling_namespace,
            p_room
        );
        p_signaler.local_peer_id = p_signaler.peer_id_for(p_peer);
        if (p_peer != 1) {
            p_signaler.server_peer_id
                = TrackerSignaler::host_peer_id(p_signaler.hash);
        }
    }

    static int64_t parked(const TrackerSignaler &p_signaler) {
        return int64_t(p_signaler.pending.size());
    }

    static godot::Dictionary parked_at(
        const TrackerSignaler &p_signaler,
        int64_t p_at
    ) {
        return p_signaler.pending[p_at].message;
    }

    static void feed(
        TrackerSignaler &p_signaler,
        const godot::Dictionary &p_packet
    ) {
        p_signaler.read_packet(p_packet);
    }
};

} // namespace netw::connect

namespace TestNetwConnectSignaler {

using namespace godot;
using netw::connect::SignalerEvent;
using netw::connect::TrackerSignaler;
using netw::connect::TrackerSignalerProbe;

PackedStringArray no_trackers() {
    PackedStringArray urls;
    urls.push_back("ws://127.0.0.1:9999");
    return urls;
}

Dictionary ice_at(const char *p_name) {
    Dictionary one;
    one["candidate"] = String(p_name);
    one["sdpMid"] = String("0");
    one["sdpMLineIndex"] = int64_t(0);
    return one;
}

Dictionary bundle_of(const char *p_type, const char *p_sdp, int64_t p_count) {
    Array candidates;
    for (int64_t at = 0; at < p_count; at++) {
        candidates.push_back(ice_at("candidate:x 1 udp 1 10.0.0.1 1 typ host"));
    }
    Dictionary payload;
    payload["type"] = String(p_type);
    payload["sdp"] = String(p_sdp);
    payload["candidates"] = candidates;
    return payload;
}

Dictionary packet_from(
    const String &p_hash,
    const String &p_peer,
    const Dictionary &p_answer
) {
    Dictionary packet;
    packet["info_hash"] = p_hash;
    packet["peer_id"] = p_peer;
    packet["answer"] = p_answer;
    return packet;
}

TEST_CASE(
    "[Networked][Connect][Hosted] a host's address is derived from the room "
    "hash, so a joiner addresses it directly instead of waiting for the "
    "tracker to introduce them"
) {
    const String hash = "a1b2c3d4e5f60718293a";
    CHECK(
        TrackerSignaler::host_peer_id(hash) == String("a1b2c3d4e50000000001")
    );
    NETW_CHECK_EQ(int(TrackerSignaler::host_peer_id(hash).length()), 20);
}

TEST_CASE(
    "[Networked][Connect][Hosted] an address carries the engine peer id in "
    "its last ten glyphs, because a tracker id is all the session ever gets "
    "back and every route is keyed by the engine id"
) {
    NETW_CHECK_EQ(
        int(TrackerSignaler::peer_of_address("a1b2c3d4e50000000001")),
        1
    );
    NETW_CHECK_EQ(
        int(TrackerSignaler::peer_of_address("ffffffffff0000000042")),
        42
    );
    NETW_CHECK_EQ(int(TrackerSignaler::peer_of_address("too-short")), 0);
}

TEST_CASE(
    "[Networked][Connect][Hosted] a namespace salts the room into its hash "
    "while a bare twenty-glyph room is its own, so a short code a person can "
    "read and a raw hash both address one swarm"
) {
    const String room = "XYZ12";
    CHECK(
        TrackerSignaler::derived_info_hash("my_game", room)
        == (String("my_game") + String(":") + room).sha1_text().substr(0, 20)
    );

    const String raw = "0123456789abcdef0123";
    CHECK(TrackerSignaler::derived_info_hash(String(), raw) == raw);

    const String odd = "not-twenty";
    CHECK(
        TrackerSignaler::derived_info_hash(String(), odd)
        == odd.sha1_text().substr(0, 20)
    );
}

TEST_CASE(
    "[Networked][Connect][Hosted] a room a host opens is a five-glyph code "
    "under a namespace and a twenty-glyph hash without one, because only a "
    "namespaced swarm is small enough for a code to be unambiguous in"
) {
    TrackerSignaler bare(no_trackers(), String(), String());
    TrackerSignalerProbe::arm(bare, String(), 1);

    TrackerSignaler coded(no_trackers(), "my_game", "ABC");
    coded.open(String(), 1);
    const String room = coded.room_id();
    NETW_CHECK_EQ(int(room.length()), 5);
    for (int64_t at = 0; at < room.length(); at++) {
        const String glyph = room.substr(at, 1);
        CHECK((glyph == "A" || glyph == "B" || glyph == "C"));
    }
    NETW_CHECK_EQ(int(coded.info_hash().length()), 20);
    CHECK(
        coded.info_hash()
        == (String("my_game") + String(":") + room).sha1_text().substr(0, 20)
    );
    coded.close();
}

TEST_CASE(
    "[Networked][Connect][Hosted] an offer rides the directed answer slot "
    "with its whole candidate bundle, because a public tracker normalizes "
    "the matchmaking array and would strip the candidates out of it"
) {
    TrackerSignaler client(no_trackers(), String(), String());
    TrackerSignalerProbe::arm(client, "a1b2c3d4e5f60718293a", 7);

    client.send(1, String(), "offer", bundle_of("offer", "v=0 fake", 2));

    NETW_CHECK_EQ(int(TrackerSignalerProbe::parked(client)), 1);
    const Dictionary sent = TrackerSignalerProbe::parked_at(client, 0);
    CHECK(String(sent["to_peer_id"]) == String("a1b2c3d4e50000000001"));
    CHECK(!sent.has("offers"));
    const Dictionary slot = sent["answer"];
    CHECK(String(slot["type"]) == String("offer"));
    const Array carried = slot["candidates"];
    NETW_CHECK_EQ(int(carried.size()), 2);
}

TEST_CASE(
    "[Networked][Connect][Hosted] a host answers the address it learned from "
    "the offer, because a client's address is random and nothing but the "
    "offer itself can tell the host what it is"
) {
    TrackerSignaler host(no_trackers(), String(), String());
    TrackerSignalerProbe::arm(host, "a1b2c3d4e5f60718293a", 1);

    const String client_address = "9876543210"
                                  "0000000007";
    host.send(7, client_address, "answer", bundle_of("answer", "v=0 back", 1));

    NETW_CHECK_EQ(int(TrackerSignalerProbe::parked(host)), 1);
    const Dictionary sent = TrackerSignalerProbe::parked_at(host, 0);
    CHECK(String(sent["to_peer_id"]) == client_address);
    const Dictionary slot = sent["answer"];
    CHECK(String(slot["type"]) == String("answer"));
}

TEST_CASE(
    "[Networked][Connect][Hosted] the same bundle arriving twice reports "
    "once while a bundle carrying new candidates reports again, because a "
    "tracker re-sends for reliability and a top-up is not a re-send"
) {
    TrackerSignaler host(no_trackers(), String(), String());
    const String hash = "a1b2c3d4e5f60718293a";
    TrackerSignalerProbe::arm(host, hash, 1);
    const String from = "9876543210"
                        "0000000007";

    TrackerSignalerProbe::feed(
        host,
        packet_from(hash, from, bundle_of("offer", "v=0 same", 1))
    );
    TrackerSignalerProbe::feed(
        host,
        packet_from(hash, from, bundle_of("offer", "v=0 same", 1))
    );

    LocalVector<SignalerEvent> once;
    host.drain(once);
    NETW_CHECK_EQ(int(once.size()), 1);
    CHECK(once[0].kind_text == String("offer"));
    NETW_CHECK_EQ(int(once[0].from_peer), 7);

    TrackerSignalerProbe::feed(
        host,
        packet_from(hash, from, bundle_of("offer", "v=0 same", 2))
    );
    LocalVector<SignalerEvent> topup;
    host.drain(topup);
    NETW_CHECK_EQ(int(topup.size()), 1);
}

TEST_CASE(
    "[Networked][Connect][Hosted] a packet for another swarm or from this "
    "peer itself is dropped, because one tracker socket carries every room "
    "the process is in and a peer hears its own announces back"
) {
    TrackerSignaler host(no_trackers(), String(), String());
    const String hash = "a1b2c3d4e5f60718293a";
    TrackerSignalerProbe::arm(host, hash, 1);

    TrackerSignalerProbe::feed(
        host,
        packet_from(
            "ffffffffffffffffffff",
            "9876543210"
            "0000000007",
            bundle_of("offer", "v=0 elsewhere", 1)
        )
    );
    TrackerSignalerProbe::feed(
        host,
        packet_from(
            hash,
            host.local_address(),
            bundle_of("offer", "v=0 mine", 1)
        )
    );

    LocalVector<SignalerEvent> heard;
    host.drain(heard);
    NETW_CHECK_EQ(int(heard.size()), 0);
}

TEST_CASE(
    "[Networked][Connect][Hosted] what a script signaler reports reaches the "
    "session through the adapter, because a signaler reports by calling and "
    "the session reads on its own poll rather than being re-entered"
) {
    Ref<netw::NetwWebRTCSignaler> seam;
    seam.instantiate();
    netw::connect::ScriptSignaler adapter(seam);

    seam->report_ready();
    seam->receive(
        7,
        "9876543210"
        "0000000007",
        "offer",
        bundle_of("offer", "v=0 scripted", 1)
    );
    seam->report_lost();

    adapter.poll(0.016, 0, 0);
    LocalVector<SignalerEvent> heard;
    adapter.drain(heard);

    NETW_CHECK_EQ(int(heard.size()), 3);
    CHECK(heard[0].kind == SignalerEvent::READY);
    CHECK(heard[1].kind == SignalerEvent::RECEIVED);
    NETW_CHECK_EQ(int(heard[1].from_peer), 7);
    CHECK(heard[1].kind_text == String("offer"));
    CHECK(heard[2].kind == SignalerEvent::LOST);

    LocalVector<SignalerEvent> again;
    adapter.poll(0.016, 0, 0);
    adapter.drain(again);
    NETW_CHECK_EQ(int(again.size()), 0);
}

TEST_CASE(
    "[Networked][Connect][Hosted] a signaler that implements no open refuses "
    "the bring-up rather than reporting a room it never opened, because a "
    "seam with nothing behind it must fail loudly at the one call that can"
) {
    Ref<netw::NetwWebRTCSignaler> bare;
    bare.instantiate();
    netw::connect::ScriptSignaler adapter(bare);

    CHECK(adapter.open("room", 1) == ERR_UNCONFIGURED);
    CHECK(adapter.room_id().is_empty());

    Ref<netw::NetwWebRTCSignaler> absent;
    netw::connect::ScriptSignaler headless(absent);
    CHECK(headless.open("room", 1) == ERR_UNCONFIGURED);
    CHECK(headless.room_id().is_empty());
    CHECK(headless.local_address().is_empty());
}

} // namespace TestNetwConnectSignaler
