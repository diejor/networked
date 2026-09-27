#include "support/netw_test.h"

#include "godot/class_db.hpp"
#include "godot/multiplayer.hpp"
#include "godot/scene_tree.hpp"
#include "godot/script.hpp"
#include "netw/api/context.hpp"
#include "netw/api/join_request.hpp"
#include "netw/api/netw_multiplayer.hpp"
#include "netw/session_core.hpp"

#if defined(NETW_TIER_HOSTED)

namespace TestNetwSessionAdmissionLaws {

using namespace godot;
using netw::NetwMultiplayer;

constexpr const char *GATE = R"(extends Node

var calls := 0
var seen_peer := 0
var seen_name := StringName()
var seen_team := StringName()
var verdict := OK

func admit(
	peer_id: int,
	username: StringName,
	team: StringName = StringName()
) -> Error:
	calls += 1
	seen_peer = peer_id
	seen_name = username
	seen_team = team
	return verdict

func seat(_who, _team: StringName) -> void:
	pass
)";

constexpr const char *GATE_ANSWERING_NO_ERROR = R"(extends Node

var calls := 0

func admit(_peer_id: int, _username: StringName):
	calls += 1
	return "admitted"
)";

Ref<Script> shape_of(const char *p_source) {
    const Ref<Script> shape
        = netw::gd::instantiate_class(StringName("GDScript"));
    shape->set_source_code(String(p_source));
    shape->reload();
    return shape;
}

Node *mounted_branch(const char *p_name) {
    Node *branch = memnew(Node);
    branch->set_name(StringName(p_name));
    netw::gd::scene_root()->add_child(branch);
    return branch;
}

Ref<NetwMultiplayer> branch_session(Node *p_branch) {
    Ref<SceneMultiplayer> inner;
    inner.instantiate();
    inner->set_root_path(p_branch->get_path());
    Ref<NetwMultiplayer> made;
    made.instantiate();
    made->session_set_inner(inner);
    made->session_set_role(NetwMultiplayer::ROLE_LISTEN_SERVER);
    netw::gd::scene_tree()->set_multiplayer(made, p_branch->get_path());
    return made;
}

void release_branch(const Ref<NetwMultiplayer> &p_api, Node *p_branch) {
    p_api->embed_dispose();
    netw::gd::scene_tree()->set_multiplayer(
        Ref<MultiplayerAPI>(),
        p_branch->get_path()
    );
    netw::gd::scene_root()->remove_child(p_branch);
    memdelete(p_branch);
}

Node *gate_on(Node *p_branch, const char *p_source) {
    const Ref<Script> shape = shape_of(p_source);
    Node *written = Object::cast_to<Node>(shape->call("new"));
    p_branch->add_child(written);
    return written;
}

void join_as(
    const Ref<NetwMultiplayer> &p_api,
    const char *p_username,
    int64_t p_peer
) {
    netw::JoinRequest claim;
    claim.username = StringName(p_username);
    claim.app_tag = p_api->auth_app_tag_of();
    claim.wire_identity = netw::SessionCore::compute_wire_identity();
    claim.schema_identity = p_api->session_schema_identity();
    p_api->session_receive_join(claim.serialize(), p_peer);
}

TEST_CASE(
    "[Networked][Session][SceneTree] G1 a join an admission handler answers "
    "OK for is admitted, and the handler saw it before the membership existed"
) {
    Node *branch = mounted_branch("AdmitG1");
    const Ref<NetwMultiplayer> host = branch_session(branch);
    Node *gate = gate_on(branch, GATE);
    REQUIRE(gate != nullptr);
    REQUIRE(
        int(
            netw::Netw::configure_admission(Callable(gate, StringName("admit")))
        )
        == int(OK)
    );

    join_as(host, "ana", 9);

    NETW_CHECK_EQ(int(gate->get(StringName("calls"))), 1);
    CHECK(host->peer_get_player(9).is_valid());
    CHECK(String(host->session_refusal(9)).is_empty());

    release_branch(host, branch);
}

TEST_CASE(
    "[Networked][Session][SceneTree] G2 a join an admission handler answers "
    "an Error for is turned down, and the refusal names that Error"
) {
    Node *branch = mounted_branch("AdmitG2");
    const Ref<NetwMultiplayer> host = branch_session(branch);
    Node *gate = gate_on(branch, GATE);
    gate->set(StringName("verdict"), int(ERR_UNAUTHORIZED));
    REQUIRE(
        int(
            netw::Netw::configure_admission(Callable(gate, StringName("admit")))
        )
        == int(OK)
    );

    join_as(host, "ana", 9);

    NETW_CHECK_EQ(int(gate->get(StringName("calls"))), 1);
    CHECK(host->peer_get_player(9).is_null());
    CHECK(String(host->session_refusal(9))
              .contains(String(netw::gd::error_name(ERR_UNAUTHORIZED))));

    release_branch(host, branch);
}

TEST_CASE(
    "[Networked][Session][SceneTree] G3 a refused join spends no membership, "
    "so the peer admitted after it holds the one the refused peer would have "
    "had. The handler runs before the mint rather than after it, which is "
    "what a kick written inside the join handler cannot undo"
) {
    Node *branch = mounted_branch("AdmitG3");
    const Ref<NetwMultiplayer> host = branch_session(branch);
    Node *gate = gate_on(branch, GATE);
    gate->set(StringName("verdict"), int(ERR_UNAUTHORIZED));
    REQUIRE(
        int(
            netw::Netw::configure_admission(Callable(gate, StringName("admit")))
        )
        == int(OK)
    );

    join_as(host, "ana", 9);
    CHECK(host->peer_get_player(9).is_null());

    gate->set(StringName("verdict"), int(OK));
    join_as(host, "bo", 11);

    const Ref<netw::NetwPlayer> seated = host->peer_get_player(11);
    REQUIRE(seated.is_valid());
    NETW_CHECK_EQ(seated->player_id(), int64_t(1));

    release_branch(host, branch);
}

TEST_CASE(
    "[Networked][Session][SceneTree] G4 the handler is handed the joining "
    "peer, the username it claimed and the join's own arguments unflattened"
) {
    Node *branch = mounted_branch("AdmitG4");
    const Ref<NetwMultiplayer> host = branch_session(branch);
    Node *gate = gate_on(branch, GATE);
    REQUIRE(
        netw::Netw::configure_join(Callable(gate, StringName("seat")))
            .is_valid()
    );
    REQUIRE(
        int(
            netw::Netw::configure_admission(Callable(gate, StringName("admit")))
        )
        == int(OK)
    );

    Array carried;
    carried.push_back(StringName("red"));
    host->session_submit_join(StringName("ana"), carried);

    NETW_CHECK_EQ(int(gate->get(StringName("calls"))), 1);
    NETW_CHECK_EQ(
        int64_t(gate->get(StringName("seen_peer"))),
        int64_t(host->NETW_API_VIRTUAL(get_unique_id)())
    );
    CHECK(StringName(gate->get(StringName("seen_name"))) == StringName("ana"));
    CHECK(StringName(gate->get(StringName("seen_team"))) == StringName("red"));

    release_branch(host, branch);
}

TEST_CASE(
    "[Networked][Session][SceneTree] G5 the host's own join is judged by the "
    "same handler, so a gate that refuses everyone locks its own host out "
    "loudly rather than seating one peer under a rule it refused every other "
    "peer by"
) {
    Node *branch = mounted_branch("AdmitG5");
    const Ref<NetwMultiplayer> host = branch_session(branch);
    Node *gate = gate_on(branch, GATE);
    gate->set(StringName("verdict"), int(ERR_UNAUTHORIZED));
    REQUIRE(
        int(
            netw::Netw::configure_admission(Callable(gate, StringName("admit")))
        )
        == int(OK)
    );

    host->session_submit_join(StringName("host"), Array());

    NETW_CHECK_EQ(int(gate->get(StringName("calls"))), 1);
    CHECK(host->player_local().is_null());

    release_branch(host, branch);
}

TEST_CASE(
    "[Networked][Session][SceneTree] G6 a handler that answers no Error at "
    "all refuses the join, because a gate that decides nothing has admitted "
    "nobody"
) {
    Node *branch = mounted_branch("AdmitG6");
    const Ref<NetwMultiplayer> host = branch_session(branch);
    Node *gate = gate_on(branch, GATE_ANSWERING_NO_ERROR);
    REQUIRE(
        int(
            netw::Netw::configure_admission(Callable(gate, StringName("admit")))
        )
        == int(OK)
    );

    join_as(host, "ana", 9);

    NETW_CHECK_EQ(int(gate->get(StringName("calls"))), 1);
    CHECK(host->peer_get_player(9).is_null());
    CHECK(String(host->session_refusal(9))
              .contains(String(netw::gd::error_name(ERR_INVALID_DATA))));

    release_branch(host, branch);
}

} // namespace TestNetwSessionAdmissionLaws

#endif
