#include "support/auth_stand.h"
#include "support/mesh_stand.h"

#if defined(NETW_TIER_HOSTED)

#include "netw/api/auth_result.hpp"
#include "netw/api/netw_identity.hpp"
#include "netw/api/netw_multiplayer.hpp"
#include "netw/api/participant.hpp"
#include "netw/api/promise.hpp"

namespace TestAuthMeshJoin {

using namespace godot;
using namespace netw_test;
using netw::AuthResult;
using netw::NetwIdentity;
using netw::NetwMultiplayer;
using netw::NetwParticipant;
using netw::NetwPromise;

constexpr int COORDINATOR = 7;
constexpr int TRANSPORT_SERVER = 1;
constexpr int MEMBER = 9;

Ref<AuthResult> accepting_as(const char *p_username) {
    Ref<NetwIdentity> identity;
    identity.instantiate();
    identity->set_username(StringName(p_username));
    identity->set_service(StringName("mesh_stand"));
    Ref<AuthResult> verdict;
    verdict.instantiate();
    verdict->set_accepted(true);
    verdict->set_identity(identity);
    return verdict;
}

Ref<AuthResult> rejecting(const char *p_reason) {
    Ref<AuthResult> verdict;
    verdict.instantiate();
    verdict->set_accepted(false);
    verdict->set_rejection_reason(String(p_reason));
    return verdict;
}

PackedByteArray proof(uint8_t p_mark) {
    PackedByteArray bytes;
    bytes.push_back(p_mark);
    bytes.push_back(0x5a);
    return bytes;
}

Ref<NetwTestAuthFlow> flow_offering(const PackedByteArray &p_proof) {
    Ref<NetwTestAuthFlow> flow;
    flow.instantiate();
    flow->set_answer(p_proof);
    return flow;
}

Ref<NetwPromise> unsettled() {
    Ref<NetwPromise> pending;
    pending.instantiate();
    return pending;
}

void check_text(const String &p_seen, const String &p_want) {
    NETW_FORMAT_TEXT(seen_text, p_seen.utf8().get_data());
    NETW_FORMAT_TEXT(want_text, p_want.utf8().get_data());
    CAPTURE(seen_text);
    CAPTURE(want_text);
    const bool same = p_seen == p_want;
    CHECK(same);
}

String name_of(const Ref<NetwParticipant> &p_seated) {
    return p_seated.is_valid() ? String(p_seated->get_username()) : String();
}

String identity_at(NetwMultiplayer *p_session, int p_peer) {
    const Ref<NetwIdentity> held = p_session->peer_get_identity(p_peer);
    return held.is_valid() ? String(held->get_username()) : String();
}

TEST_CASE(
    "[Networked][Session][SceneTree] AJ1 three instances with a configured "
    "flow hold their hellos until each preparation settles, so every "
    "credential rides the link to coordinator 7 and nothing is sent early"
) {
    MeshStand stand;
    stand.seat_coordinator(COORDINATOR);
    stand.seat_member(TRANSPORT_SERVER);
    stand.seat_member(MEMBER);

    const Ref<NetwTestAuthFlow> host_flow = flow_offering(proof(7));
    const Ref<NetwTestAuthFlow> one_flow = flow_offering(proof(1));
    const Ref<NetwTestAuthFlow> nine_flow = flow_offering(proof(9));
    host_flow->set_verdict_for(TRANSPORT_SERVER, accepting_as("verified_one"));
    host_flow->set_verdict_for(MEMBER, accepting_as("verified_nine"));

    NetwMultiplayer *host = stand.session_of(COORDINATOR);
    NetwMultiplayer *one = stand.session_of(TRANSPORT_SERVER);
    NetwMultiplayer *nine = stand.session_of(MEMBER);
    REQUIRE(host != nullptr);
    REQUIRE(one != nullptr);
    REQUIRE(nine != nullptr);
    host->auth_set_flow(host_flow);
    one->auth_set_flow(one_flow);
    nine->auth_set_flow(nine_flow);

    const Ref<NetwPromise> one_ready = unsettled();
    const Ref<NetwPromise> nine_ready = unsettled();
    one_flow->set_prepared(one_ready);
    nine_flow->set_prepared(nine_ready);

    const Ref<NetwPromise> one_prepared
        = stand.prepare_join(TRANSPORT_SERVER, StringName("claimed_one"));
    const Ref<NetwPromise> nine_prepared
        = stand.prepare_join(MEMBER, StringName("claimed_nine"));
    NETW_CHECK_EQ(int(one_prepared->get_is_settled()), 0);
    NETW_CHECK_EQ(int(nine_prepared->get_is_settled()), 0);

    stand.wire(COORDINATOR, TRANSPORT_SERVER);
    stand.wire(COORDINATOR, MEMBER);
    stand.pump(4);

    NETW_CHECK_EQ(
        int(one->auth_link_waits_on_preparation(COORDINATOR)),
        1
    );
    NETW_CHECK_EQ(
        int(nine->auth_link_waits_on_preparation(COORDINATOR)),
        1
    );
    NETW_CHECK_EQ(host_flow->verify_count(), 0);
    NETW_CHECK_EQ(one_flow->credential_count(), 0);

    one_ready->resolve(int64_t(OK));
    nine_ready->resolve(int64_t(OK));
    stand.pump(6);

    NETW_CHECK_EQ(int(one_prepared->get_code()), int(OK));
    NETW_CHECK_EQ(int(nine_prepared->get_code()), int(OK));
    NETW_CHECK_EQ(host_flow->verify_count(), 2);
    NETW_CHECK_EQ(int(host_flow->payload_of(TRANSPORT_SERVER) == proof(1)), 1);
    NETW_CHECK_EQ(int(host_flow->payload_of(MEMBER) == proof(9)), 1);

    check_text(identity_at(host, TRANSPORT_SERVER), String("verified_one"));
    check_text(identity_at(host, MEMBER), String("verified_nine"));

    check_text(identity_at(one, COORDINATOR), String());
    check_text(identity_at(nine, COORDINATOR), String());
    check_text(identity_at(nine, TRANSPORT_SERVER), String());

    check_text(
        name_of(stand.submit_join(TRANSPORT_SERVER)),
        String("verified_one")
    );
    check_text(name_of(stand.submit_join(MEMBER)), String("verified_nine"));
    check_text(
        name_of(host->participant_of(TRANSPORT_SERVER)),
        String("verified_one")
    );
    check_text(name_of(host->participant_of(MEMBER)), String("verified_nine"));
    NETW_CHECK_EQ(int(host->get_connected_participants().size()), 2);
}

TEST_CASE(
    "[Networked][Session][SceneTree] AJ2 coordinator 7 refuses the proof it "
    "rejects and seats no identity for that peer, while the peer it accepts "
    "joins on the same stand, so transport peer 1 is admitted by its "
    "credential alone"
) {
    MeshStand stand;
    stand.seat_coordinator(COORDINATOR);
    stand.seat_member(TRANSPORT_SERVER);
    stand.seat_member(MEMBER);

    const Ref<NetwTestAuthFlow> host_flow = flow_offering(proof(7));
    const Ref<NetwTestAuthFlow> one_flow = flow_offering(proof(1));
    const Ref<NetwTestAuthFlow> nine_flow = flow_offering(proof(9));
    host_flow->set_verdict_for(TRANSPORT_SERVER, rejecting("Bad proof"));
    host_flow->set_verdict_for(MEMBER, accepting_as("verified_nine"));

    NetwMultiplayer *host = stand.session_of(COORDINATOR);
    NetwMultiplayer *one = stand.session_of(TRANSPORT_SERVER);
    NetwMultiplayer *nine = stand.session_of(MEMBER);
    REQUIRE(host != nullptr);
    REQUIRE(one != nullptr);
    REQUIRE(nine != nullptr);
    host->auth_set_flow(host_flow);
    one->auth_set_flow(one_flow);
    nine->auth_set_flow(nine_flow);

    const Ref<NetwPromise> one_ready = unsettled();
    const Ref<NetwPromise> nine_ready = unsettled();
    one_flow->set_prepared(one_ready);
    nine_flow->set_prepared(nine_ready);

    stand.prepare_join(TRANSPORT_SERVER, StringName("claimed_one"));
    stand.prepare_join(MEMBER, StringName("claimed_nine"));
    stand.wire(COORDINATOR, TRANSPORT_SERVER);
    stand.wire(COORDINATOR, MEMBER);
    stand.pump(4);

    one_ready->resolve(int64_t(OK));
    nine_ready->resolve(int64_t(OK));
    stand.pump(6);

    check_text(
        String(host->session_refusal(TRANSPORT_SERVER)),
        String("Bad proof")
    );
    check_text(identity_at(host, TRANSPORT_SERVER), String());
    check_text(String(host->session_refusal(MEMBER)), String());
    check_text(identity_at(host, MEMBER), String("verified_nine"));

    check_text(name_of(stand.submit_join(MEMBER)), String("verified_nine"));
    NETW_CHECK_EQ(int(host->participant_of(MEMBER).is_valid()), 1);
    NETW_CHECK_EQ(int(host->participant_of(TRANSPORT_SERVER).is_valid()), 0);
}

} // namespace TestAuthMeshJoin

#endif
