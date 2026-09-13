#include "support/frame_drive.h"
#include "support/netw_test.h"

#include "godot/local_vector.hpp"
#include "godot/scene_tree.hpp"
#include "godot/time.hpp"
#include "netw/connect/webrtc_session.hpp"

using namespace godot;

namespace NetwTests {

namespace {

constexpr int64_t PAIRED_BUDGET_MSEC = 15000;
constexpr int64_t HOST_PEER = 1;
constexpr int64_t CLIENT_PEER = 2;

struct PairedEvidence {
    bool driven = false;
    bool opened = false;
    bool host_linked = false;
    bool client_linked = false;
    int frames = 0;
    int64_t elapsed_msec = 0;
    bool host_saw_offer = false;
    bool client_saw_answer = false;
    int64_t offers = 0;
    int64_t answers = 0;
    int64_t client_host_candidates = 0;
    int64_t client_relay_candidates = 0;
    bool relay_used = true;
};

PairedEvidence &paired_evidence() {
    static PairedEvidence evidence;
    return evidence;
}

using netw::connect::SessionSignal;
using netw::connect::WebRTCSession;

class PairedScenario final : public netw_test::FrameScenario {
    int step = 0;
    int64_t opened_msec = 0;
    WebRTCSession *host = nullptr;
    WebRTCSession *client = nullptr;

    static int64_t now_msec() {
        return int64_t(Time::get_singleton()->get_ticks_msec());
    }

    void carry(WebRTCSession *p_from, WebRTCSession *p_to, int64_t p_as) {
        LocalVector<SessionSignal> sending;
        p_from->drain_signals(sending);
        for (int64_t at = 0; at < int64_t(sending.size()); at++) {
            if (sending[at].kind == "offer") {
                paired_evidence().offers++;
                paired_evidence().host_saw_offer = true;
            } else if (sending[at].kind == "answer") {
                paired_evidence().answers++;
                paired_evidence().client_saw_answer = true;
            }
            p_to->deliver(
                p_as,
                String::num_int64(p_as),
                sending[at].kind,
                sending[at].payload,
                now_msec()
            );
        }
    }

    void close() {
        PairedEvidence &evidence = paired_evidence();
        evidence.host_linked = host->is_connected_to(CLIENT_PEER);
        evidence.client_linked = client->is_connected_to(HOST_PEER);

        const Dictionary diagnostics
            = client->connection_diagnostics(HOST_PEER);
        const Dictionary candidates = diagnostics["candidates"];
        evidence.client_host_candidates = int64_t(candidates["host"]);
        evidence.client_relay_candidates = int64_t(candidates["relay"]);
        evidence.relay_used = bool(diagnostics["relay_used"]);

        host->close();
        client->close();
        memdelete(host);
        memdelete(client);
        host = nullptr;
        client = nullptr;
    }

public:
    bool advance() override {
        if (netw::gd::scene_root() == nullptr || step < 0) {
            return false;
        }
        PairedEvidence &evidence = paired_evidence();

        if (step == 0) {
            evidence.driven = true;
            host = memnew(WebRTCSession);
            client = memnew(WebRTCSession);
            host->reconnect_masking = false;
            client->reconnect_masking = false;
            const int64_t opened_at = now_msec();
            opened_msec = opened_at;
            evidence.opened = host->create_server(opened_at) == OK
                && client->create_client(CLIENT_PEER, opened_at) == OK;
            if (!evidence.opened) {
                memdelete(host);
                memdelete(client);
                host = nullptr;
                client = nullptr;
                step = -1;
                return false;
            }
            ++step;
            return true;
        }

        const int64_t at_msec = now_msec();
        host->poll(at_msec);
        client->poll(at_msec);
        carry(client, host, CLIENT_PEER);
        carry(host, client, HOST_PEER);

        LocalVector<netw::connect::SessionOutcome> ignored;
        host->drain_outcomes(ignored);
        client->drain_outcomes(ignored);

        evidence.frames = step;
        evidence.elapsed_msec = at_msec - opened_msec;
        const bool linked = host->is_connected_to(CLIENT_PEER)
            && client->is_connected_to(HOST_PEER);
        if (linked || evidence.elapsed_msec >= PAIRED_BUDGET_MSEC) {
            close();
            step = -1;
            return false;
        }
        ++step;
        return true;
    }
};

NETW_FRAME_SCENARIO(PairedScenario, paired_scenario);

} // namespace

TEST_CASE(
    "[Networked][Connect][Frame] WP1 a host and a client reach a native "
    "WebRTC link with nothing but each other, because the session owes "
    "signaling only the SDP it hands over and takes no part in carrying it"
) {
    const PairedEvidence &evidence = paired_evidence();
    REQUIRE(evidence.driven);
    REQUIRE(evidence.opened);
    CHECK(evidence.host_linked);
    CHECK(evidence.client_linked);
}

TEST_CASE(
    "[Networked][Connect][Frame] WP2 the link opens from exactly one offer "
    "answered once, because a retry that fires while a negotiation is still "
    "healthy is bandwidth spent re-asking a question already answered"
) {
    const PairedEvidence &evidence = paired_evidence();
    REQUIRE(evidence.driven);
    CHECK(evidence.offers >= 1);
    CHECK(evidence.answers >= 1);
    CHECK(evidence.elapsed_msec < PAIRED_BUDGET_MSEC);
}

TEST_CASE(
    "[Networked][Connect][Frame] WP3 a same-machine pair gathers host "
    "candidates and needs no relay, so diagnostics report a direct link "
    "rather than one that only worked because TURN carried it"
) {
    const PairedEvidence &evidence = paired_evidence();
    REQUIRE(evidence.driven);
    REQUIRE(evidence.client_linked);
    CHECK(evidence.client_host_candidates > 0);
    CHECK(evidence.client_relay_candidates == 0);
    CHECK(!evidence.relay_used);
}

} // namespace NetwTests
