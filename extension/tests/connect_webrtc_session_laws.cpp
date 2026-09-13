#include "support/netw_test.h"

#include "godot/local_vector.hpp"
#include "godot/variant.hpp"
#include "netw/connect/webrtc_session.hpp"

namespace netw::connect {

struct WebRTCSessionProbe {
    static void open(WebRTCSession &p_session, int64_t p_peer, bool p_local) {
        p_session.open_row(p_peer, p_local, 0);
    }

    static void set_remote_landed(WebRTCSession &p_session, int64_t p_peer) {
        WebRTCSession::Row *row = p_session.row_of(p_peer);
        if (row != nullptr) {
            row->remote_desc_set = true;
        }
    }

    static void set_counts(
        WebRTCSession &p_session,
        int64_t p_peer,
        int64_t p_host,
        int64_t p_srflx,
        int64_t p_relay
    ) {
        WebRTCSession::Row *row = p_session.row_of(p_peer);
        if (row != nullptr) {
            row->counts.host = p_host;
            row->counts.srflx = p_srflx;
            row->counts.relay = p_relay;
        }
    }

    static int64_t attempts(const WebRTCSession &p_session, int64_t p_peer) {
        const WebRTCSession::Row *row = p_session.row_of(p_peer);
        return row != nullptr ? row->attempts : 0;
    }

    static void drive(WebRTCSession &p_session, int64_t p_now_msec) {
        p_session.maybe_retry(p_now_msec);
    }
};

} // namespace netw::connect

namespace TestNetwConnectWebRTCSession {

using namespace godot;
using netw::connect::candidate_key;
using netw::connect::SessionOutcome;
using netw::connect::SessionSignal;
using netw::connect::WebRTCSession;
using netw::connect::WebRTCSessionProbe;

Dictionary ice_at(const char *p_name, const char *p_media, int64_t p_index) {
    Dictionary one;
    one["candidate"] = String(p_name);
    one["sdpMid"] = String(p_media);
    one["sdpMLineIndex"] = p_index;
    return one;
}

TEST_CASE(
    "[Networked][Connect][Hosted] a description carries every candidate "
    "gathered so far in one bundle, because a public tracker relays one "
    "directed message and strips anything it does not recognise"
) {
    WebRTCSession session;
    WebRTCSessionProbe::open(session, 1, false);
    session
        .report_candidate(1, "0", 0, "candidate:a 1 udp 1 10.0.0.1 1 typ host");
    session
        .report_candidate(1, "0", 0, "candidate:b 1 udp 1 1.2.3.4 1 typ srflx");
    session.report_description(1, "offer", "v=0 fake", 100);

    LocalVector<SessionSignal> sent;
    session.drain_signals(sent);

    NETW_CHECK_EQ(int(sent.size()), 1);
    CHECK(sent[0].kind == String("offer"));
    CHECK(String(sent[0].payload["sdp"]) == String("v=0 fake"));
    const Array bundled = sent[0].payload["candidates"];
    NETW_CHECK_EQ(int(bundled.size()), 2);
}

TEST_CASE(
    "[Networked][Connect][Hosted] a candidate key folds mid, index and text "
    "together, because the same candidate re-sent on a top-up bundle must be "
    "applied to the connection exactly once"
) {
    const String one = candidate_key(ice_at("candidate:a", "0", 0));
    const String same = candidate_key(ice_at("candidate:a", "0", 0));
    const String other_index = candidate_key(ice_at("candidate:a", "0", 1));
    const String other_text = candidate_key(ice_at("candidate:b", "0", 0));

    CHECK(one == same);
    CHECK(one != other_index);
    CHECK(one != other_text);
}

TEST_CASE(
    "[Networked][Connect][Hosted] the retry budget re-sends the bundle up to "
    "its attempt cap and then reports one failure, because a client that "
    "keeps dialling forever never lets the connect deadline speak"
) {
    WebRTCSession session;
    session.connect_retry = 1.0;
    session.max_connect_attempts = 3;
    WebRTCSessionProbe::open(session, 1, false);
    WebRTCSessionProbe::set_remote_landed(session, 1);
    WebRTCSessionProbe::set_counts(session, 1, 1, 0, 1);
    session.report_description(1, "offer", "v=0 fake", 0);

    LocalVector<SessionSignal> flushed;
    session.drain_signals(flushed);

    WebRTCSessionProbe::drive(session, 2000);
    NETW_CHECK_EQ(int(WebRTCSessionProbe::attempts(session, 1)), 2);
    WebRTCSessionProbe::drive(session, 4000);
    NETW_CHECK_EQ(int(WebRTCSessionProbe::attempts(session, 1)), 3);

    WebRTCSessionProbe::drive(session, 6000);
    NETW_CHECK_EQ(int(WebRTCSessionProbe::attempts(session, 1)), 3);

    LocalVector<SessionOutcome> reported;
    session.drain_outcomes(reported);
    NETW_CHECK_EQ(int(reported.size()), 1);
    CHECK(reported[0].kind == SessionOutcome::FAILED);
    CHECK(reported[0].reason == String("NAT_TRAVERSAL_FAILED"));

    WebRTCSessionProbe::drive(session, 8000);
    LocalVector<SessionOutcome> again;
    session.drain_outcomes(again);
    NETW_CHECK_EQ(int(again.size()), 0);
}

TEST_CASE(
    "[Networked][Connect][Hosted] a give-up names which leg failed, because "
    "a host that never answered, an unreachable relay and a traversal that "
    "lost are three different things for whoever reads the report"
) {
    WebRTCSession session;
    session.connect_retry = 1.0;
    session.max_connect_attempts = 1;
    WebRTCSessionProbe::open(session, 1, false);
    session.report_description(1, "offer", "v=0 fake", 0);
    LocalVector<SessionSignal> flushed;
    session.drain_signals(flushed);
    WebRTCSessionProbe::drive(session, 5000);

    LocalVector<SessionOutcome> silent;
    session.drain_outcomes(silent);
    NETW_CHECK_EQ(int(silent.size()), 1);
    CHECK(silent[0].reason == String("HOST_UNRESPONSIVE"));

    WebRTCSession relayless;
    relayless.connect_retry = 1.0;
    relayless.max_connect_attempts = 1;
    WebRTCSessionProbe::open(relayless, 1, false);
    WebRTCSessionProbe::set_remote_landed(relayless, 1);
    WebRTCSessionProbe::set_counts(relayless, 1, 2, 1, 0);
    relayless.report_description(1, "offer", "v=0 fake", 0);
    LocalVector<SessionSignal> ignored;
    relayless.drain_signals(ignored);
    WebRTCSessionProbe::drive(relayless, 5000);

    LocalVector<SessionOutcome> turned;
    relayless.drain_outcomes(turned);
    NETW_CHECK_EQ(int(turned.size()), 1);
    CHECK(turned[0].reason == String("TURN_UNREACHABLE"));
}

TEST_CASE(
    "[Networked][Connect][Hosted] diagnostics call a relay used only when no "
    "direct candidate was gathered, so a run that merely reached TURN is not "
    "read as a run that needed it"
) {
    WebRTCSession session;
    WebRTCSessionProbe::open(session, 1, false);
    WebRTCSessionProbe::set_counts(session, 1, 0, 0, 2);

    const Dictionary needed = session.connection_diagnostics(1);
    CHECK(bool(needed["relay_used"]));

    WebRTCSessionProbe::set_counts(session, 1, 1, 0, 2);
    const Dictionary spare = session.connection_diagnostics(1);
    CHECK(!bool(spare["relay_used"]));

    const Dictionary counts = spare["candidates"];
    NETW_CHECK_EQ(int(int64_t(counts["relay"])), 2);
}

TEST_CASE(
    "[Networked][Connect][Hosted] a bundle marks itself local when either the "
    "session or the peer is, because a same-machine pair must not be handed "
    "TURN configuration it would only warn about"
) {
    WebRTCSession session;
    WebRTCSessionProbe::open(session, 2, true);
    session.report_description(2, "answer", "v=0 fake", 0);

    LocalVector<SessionSignal> sent;
    session.drain_signals(sent);
    NETW_CHECK_EQ(int(sent.size()), 1);
    CHECK(bool(sent[0].payload["is_local"]));

    WebRTCSession wide;
    wide.is_local_session = true;
    WebRTCSessionProbe::open(wide, 3, false);
    wide.report_description(3, "answer", "v=0 fake", 0);

    LocalVector<SessionSignal> marked;
    wide.drain_signals(marked);
    NETW_CHECK_EQ(int(marked.size()), 1);
    CHECK(bool(marked[0].payload["is_local"]));
}

} // namespace TestNetwConnectWebRTCSession
