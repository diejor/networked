#include "support/netw_test.h"

#include "godot/node.hpp"
#include "godot/script.hpp"
#include "netw/api/entity.hpp"
#include "netw/api/netw_multiplayer.hpp"
#include "netw/entity/control.hpp"
#include "support/declared_nodes.h"
#include "support/minted_script.h"

namespace TestEntityControlLaws {

using namespace godot;
using netw::NetwEntity;
using netw::NetwMultiplayer;

struct ArmedNode {
    Node *owner = nullptr;
    Ref<NetwEntity> entity;

    ArmedNode(const char *p_id, int64_t p_peer, int p_initial) {
        owner = memnew(Node);
        NetwEntity::bind(owner, p_id, p_peer);
        entity = NetwEntity::of(owner);
        REQUIRE(entity.is_valid());
        entity->set_initial_controller(p_initial);
        entity->arm(godot::Ref<netw::NetwMultiplayer>());
    }

    int64_t authority() const {
        return owner->get_multiplayer_authority();
    }

    ~ArmedNode() {
        memdelete(owner);
    }
};

TEST_CASE(
    "[Networked][Entity][Hosted] EC1 arming a represented-peer entity makes "
    "the peer its name spells the controller and the node authority at once"
) {
    ArmedNode armed(
        "valeria",
        42,
        int(netw::entity::Control::InitialController::REPRESENTED_PEER)
    );

    NETW_CHECK_EQ(armed.entity->get_controller(), 42);
    NETW_CHECK_EQ(armed.authority(), 42);
    NETW_CHECK_EQ(
        armed.entity->get_control_kind(),
        int64_t(NetwEntity::CONTROL_PEER_CONTROLLED)
    );
}

TEST_CASE(
    "[Networked][Entity][Hosted] EC2 arming a server-controlled entity keeps "
    "authority at the server however its name reads"
) {
    ArmedNode armed(
        "valeria",
        42,
        int(netw::entity::Control::InitialController::SERVER)
    );

    NETW_CHECK_EQ(armed.entity->get_controller(), 0);
    NETW_CHECK_EQ(armed.authority(), 1);
    NETW_CHECK_EQ(
        armed.entity->get_control_kind(),
        int64_t(NetwEntity::CONTROL_SERVER_CONTROLLED)
    );
}

TEST_CASE(
    "[Networked][Entity][Hosted] EC3 a represented-peer entity naming no peer "
    "arms to the server"
) {
    ArmedNode armed(
        "valeria",
        0,
        int(netw::entity::Control::InitialController::REPRESENTED_PEER)
    );

    NETW_CHECK_EQ(armed.entity->get_controller(), 0);
    NETW_CHECK_EQ(armed.authority(), 1);
}

TEST_CASE(
    "[Networked][Entity][Hosted] EC4 a grant moves the controller and the "
    "node authority as one act, and a revoke returns both to the server"
) {
    ArmedNode armed(
        "valeria",
        0,
        int(netw::entity::Control::InitialController::REPRESENTED_PEER)
    );

    armed.entity->grant_control(42);

    NETW_CHECK_EQ(armed.entity->get_controller(), 42);
    NETW_CHECK_EQ(armed.authority(), 42);
    NETW_CHECK_EQ(
        armed.entity->get_control_kind(),
        int64_t(NetwEntity::CONTROL_PEER_CONTROLLED)
    );

    armed.entity->revoke_control();

    NETW_CHECK_EQ(armed.entity->get_controller(), 0);
    NETW_CHECK_EQ(armed.authority(), 1);
    NETW_CHECK_EQ(
        armed.entity->get_control_kind(),
        int64_t(NetwEntity::CONTROL_SERVER_CONTROLLED)
    );
}

Ref<NetwMultiplayer> a_coordinated_session(int64_t p_coordinator) {
    Ref<NetwMultiplayer> session;
    session.instantiate();
    session->session_set_authority_peer(p_coordinator);
    session->session_peer_assigned(true, false, p_coordinator);
    return session;
}

TEST_CASE(
    "[Networked][Entity][Hosted] EC5 existence authority asks is_host, not "
    "the literal peer 1, so a session with coordinator 7 answers its own "
    "entity's is_authority true and falls a server-controlled node's "
    "authority to peer 7 rather than 1"
) {
    Ref<NetwMultiplayer> session = a_coordinated_session(7);
    REQUIRE(session->is_host());

    Node *owner = memnew(Node);
    NetwEntity::bind(owner, "valeria", 0);
    Ref<NetwEntity> entity = NetwEntity::of(owner);
    REQUIRE(entity.is_valid());
    entity->set_initial_controller(
        int(netw::entity::Control::InitialController::SERVER)
    );
    entity->arm(session);

    CHECK(entity->get_is_authority());
    NETW_CHECK_EQ(entity->get_controller(), 0);
    NETW_CHECK_EQ(owner->get_multiplayer_authority(), 7);

    memdelete(owner);
}

TEST_CASE(
    "[Networked][Entity][Hosted] EC6 an explicit remote controller survives "
    "a coordinator that is not peer 1, and the represented peer it names "
    "stays its own id rather than being rewritten to the coordinator"
) {
    Ref<NetwMultiplayer> session = a_coordinated_session(7);

    Node *owner = memnew(Node);
    NetwEntity::bind(owner, "driftwood", 42);
    Ref<NetwEntity> entity = NetwEntity::of(owner);
    REQUIRE(entity.is_valid());
    entity->set_initial_controller(
        int(netw::entity::Control::InitialController::REPRESENTED_PEER)
    );
    entity->arm(session);

    NETW_CHECK_EQ(entity->get_controller(), 42);
    NETW_CHECK_EQ(owner->get_multiplayer_authority(), 42);
    NETW_CHECK_EQ(entity->get_peer_id(), 42);

    memdelete(owner);
}

#if defined(NETW_TIER_HOSTED)

const char *POLICY_SCRIPT = netw_test::gdsrc::A_PLAIN_SCRIPT;

Ref<Script> a_script() {
    const Ref<Script> script = netw_test::script_from(POLICY_SCRIPT);
    REQUIRE(script.is_valid());
    return script;
}

TEST_CASE(
    "[Networked][Entity] the receive gate trusts the coordinator before it "
    "looks at anything, whatever peer that coordinator names, and refuses a "
    "scriptless node to everyone else, transport peer 1 included"
) {
    Node *node = memnew(Node);
    node->set_multiplayer_authority(9, false);

    CHECK(
        netw::entity::Control::script_admits(
            node,
            StringName("hp"),
            false,
            7,
            0,
            7
        )
    );
    CHECK(
        netw::entity::Control::script_admits(
            nullptr,
            StringName("hp"),
            false,
            7,
            0,
            7
        )
    );

    CHECK_FALSE(
        netw::entity::Control::script_admits(
            node,
            StringName("hp"),
            false,
            1,
            0,
            7
        )
    );
    CHECK_FALSE(
        netw::entity::Control::script_admits(
            nullptr,
            StringName("hp"),
            false,
            1,
            0,
            7
        )
    );
    memdelete(node);
}

TEST_CASE(
    "[Networked][Entity] a name the script never declared falls to "
    "the node's own authority, which is the undeclared default"
) {
    const Ref<Script> script = a_script();
    Node *node = memnew(Node);
    node->set_script(script);
    node->set_multiplayer_authority(7, false);

    CHECK(
        netw::entity::Control::script_admits(
            node,
            StringName("netw_undeclared_name"),
            false,
            7,
            0,
            0
        )
    );
    CHECK_FALSE(
        netw::entity::Control::script_admits(
            node,
            StringName("netw_undeclared_name"),
            false,
            9,
            0,
            0
        )
    );
    memdelete(node);
}

TEST_CASE(
    "[Networked][Entity] a declared policy is read off the script's "
    "own book, and the write and emit books never answer for each other"
) {
    const Ref<Script> script = a_script();
    Node *node = memnew(Node);
    node->set_script(script);
    node->set_multiplayer_authority(7, false);

    netw::entity::Control::declare_policy(
        script.ptr(),
        StringName("netw_open_field"),
        int64_t(netw::entity::Control::WritePolicy::ANY_PEER),
        false
    );

    CHECK(
        netw::entity::Control::script_admits(
            node,
            StringName("netw_open_field"),
            false,
            9,
            0,
            0
        )
    );

    CHECK_FALSE(
        netw::entity::Control::script_admits(
            node,
            StringName("netw_open_field"),
            true,
            9,
            0,
            0
        )
    );

    CHECK_FALSE(
        netw::entity::Control::script_admits(
            node,
            StringName("netw_sibling_field"),
            false,
            9,
            0,
            0
        )
    );
    CHECK(
        netw::entity::Control::script_admits(
            node,
            StringName("netw_sibling_field"),
            false,
            7,
            0,
            0
        )
    );
    memdelete(node);
}

TEST_CASE(
    "[Networked][Entity] a controller-policed name admits the "
    "controller and nobody else, and re-declaring replaces rather than adds"
) {
    const Ref<Script> script = a_script();
    Node *node = memnew(Node);
    node->set_script(script);
    node->set_multiplayer_authority(7, false);

    netw::entity::Control::declare_policy(
        script.ptr(),
        StringName("netw_steer"),
        int64_t(netw::entity::Control::WritePolicy::CONTROLLER),
        false
    );

    CHECK(
        netw::entity::Control::script_admits(
            node,
            StringName("netw_steer"),
            false,
            9,
            9,
            0
        )
    );
    CHECK_FALSE(
        netw::entity::Control::script_admits(
            node,
            StringName("netw_steer"),
            false,
            9,
            4,
            0
        )
    );

    netw::entity::Control::declare_policy(
        script.ptr(),
        StringName("netw_steer"),
        int64_t(netw::entity::Control::WritePolicy::AUTHORITY),
        false
    );

    CHECK_FALSE(
        netw::entity::Control::script_admits(
            node,
            StringName("netw_steer"),
            false,
            9,
            9,
            0
        )
    );
    CHECK(
        netw::entity::Control::script_admits(
            node,
            StringName("netw_steer"),
            false,
            7,
            9,
            0
        )
    );
    memdelete(node);
}

#endif

} // namespace TestEntityControlLaws
