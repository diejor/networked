#include "support/netw_test.h"

#include "support/declared_nodes.h"

#include "support/minted_script.h"

#include "support/loopback_rig.h"

#if defined(NETW_TIER_HOSTED)

#include <godot_cpp/classes/resource_loader.hpp>
#include <godot_cpp/classes/script.hpp>

#include "godot/node.hpp"
#include "netw/api/entity.hpp"
#include "netw/api/group_promise.hpp"
#include "netw/api/interest_layer.hpp"
#include "netw/api/netw_multiplayer.hpp"
#include "netw/api/tests.hpp"
#include "netw/call_args.hpp"
#include "netw/script/model.hpp"
#include "netw/wire/registry.hpp"

namespace TestNetwRpcGroupSettleLaws {

using namespace godot;
using netw::NetwEntity;
using netw::NetwGroupPromise;
using netw::NetwMultiplayer;
using netw::NetwNativeTests;
namespace script_model = netw::script::model;
using netw_test::LoopbackRig;

const char *PROBE_SCRIPT = netw_test::gdsrc::AUTHORITY_RPC_SINK;
const char *PROBE_METHOD = "apply_value";

Node *a_probe() {
    const Ref<Script> script = netw_test::script_from(PROBE_SCRIPT);
    if (script.is_null()) {
        return nullptr;
    }
    Node *made = Object::cast_to<Node>(script->call("new"));
    if (made != nullptr) {
        made->set_name("RequestTarget");
    }
    return made;
}

TEST_CASE(
    "[Networked][Rpc] a group nobody is live for is already satisfied and "
    "still settles at the pump, because the caller has not been handed the "
    "promise yet and nothing has chained onto it"
) {
    LoopbackRig rig(0);
    rig.mount();
    NetwMultiplayer *core = rig.server();
    CHECK(core != nullptr);
    if (core == nullptr) {
        return;
    }
    Node *probe = a_probe();
    CHECK(probe != nullptr);
    if (probe == nullptr) {
        return;
    }
    rig.branch(-1)->add_child(probe);

    const RID entity = core->entity_create();
    const int64_t route = core->entity_admit(entity);
    NETW_CHECK_GT(route, 0);
    NETW_CHECK_EQ(int(core->entity_bind_node(entity, probe)), int(OK));

    const Ref<NetwEntity> wrapper = core->entity_get_view(entity);
    NETW_CHECK_EQ(core->rpc_get_recipients(wrapper).size(), 0);

    Array args;
    args.push_back(1);
    const Ref<NetwGroupPromise> group = core->rpc_request_call_group(
        Callable(probe, StringName(PROBE_METHOD)),
        args,
        NetwMultiplayer::rpc_request_timeout_default()
    );
    CHECK(group.is_valid());
    if (group.is_valid()) {
        CHECK_FALSE(group->get_is_completed());
        core->settle_drain();
        CHECK(group->get_is_completed());
    }

    probe->get_parent()->remove_child(probe);
    memdelete(probe);
}

TEST_CASE(
    "[Networked][Rpc] an authority-mode method admits the node's own "
    "multiplayer authority and the server, and no other sender, because "
    "authority names one peer rather than the peer set"
) {
    Ref<NetwMultiplayer> core;
    core.instantiate();
    Node *probe = a_probe();
    CHECK(probe != nullptr);
    if (probe == nullptr) {
        return;
    }
    probe->set_multiplayer_authority(5, false);

    CHECK(
        NetwNativeTests::rpc_sender_admits(
            core.ptr(),
            probe,
            StringName(PROBE_METHOD),
            1
        )
    );
    CHECK(
        NetwNativeTests::rpc_sender_admits(
            core.ptr(),
            probe,
            StringName(PROBE_METHOD),
            5
        )
    );
    CHECK_FALSE(
        NetwNativeTests::rpc_sender_admits(
            core.ptr(),
            probe,
            StringName(PROBE_METHOD),
            6
        )
    );

    memdelete(probe);
}

TEST_CASE(
    "[Networked][Rpc] a peer that leaves is dropped from every group it was "
    "owed by, so a request waiting only on the departed settles instead of "
    "waiting out its deadline"
) {
    LoopbackRig rig(1);
    rig.mount();
    NetwMultiplayer *core = rig.server();
    CHECK(core != nullptr);
    if (core == nullptr) {
        return;
    }
    Node *probe = a_probe();
    CHECK(probe != nullptr);
    if (probe == nullptr) {
        return;
    }
    rig.branch(-1)->add_child(probe);

    const RID entity = core->entity_create();
    const int64_t route = core->entity_admit(entity);
    NETW_CHECK_GT(route, 0);
    NETW_CHECK_EQ(int(core->entity_bind_node(entity, probe)), int(OK));
    rig.pump(2);

    const Ref<NetwEntity> wrapper = core->entity_get_view(entity);
    const PackedInt32Array owed = core->rpc_get_recipients(wrapper);
    NETW_CHECK_EQ(owed.size(), 1);
    if (owed.size() != 1) {
        probe->get_parent()->remove_child(probe);
        memdelete(probe);
        return;
    }

    Array args;
    args.push_back(1);
    const Ref<NetwGroupPromise> group = core->rpc_request_call_group(
        Callable(probe, StringName(PROBE_METHOD)),
        args,
        NetwMultiplayer::rpc_request_timeout_default()
    );
    CHECK(group.is_valid());
    if (group.is_valid()) {
        CHECK_FALSE(group->get_is_completed());
        core->rpc_handle_disconnect(owed[0]);
        CHECK(group->get_is_completed());
    }

    probe->get_parent()->remove_child(probe);
    memdelete(probe);
}

const char *ARG_PROBE_SCRIPT = netw_test::gdsrc::NODE_ARG_RPC_SINK;

Node *an_arg_probe() {
    const Ref<Script> script = netw_test::script_from(ARG_PROBE_SCRIPT);
    if (script.is_null()) {
        return nullptr;
    }
    Node *made = Object::cast_to<Node>(script->call("new"));
    if (made != nullptr) {
        made->set_name("ArgTarget");
    }
    return made;
}

TEST_CASE(
    "[Networked][Rpc] a node argument naming a route the sender's own "
    "interest view does not admit is refused whole, because an argument is a "
    "disclosure and a peer may not name what it cannot see"
) {
    LoopbackRig rig(0);
    rig.mount();
    NetwMultiplayer *core = rig.server();
    CHECK(core != nullptr);
    if (core == nullptr) {
        return;
    }

    Node *target = an_arg_probe();
    Node *passenger = memnew(Node);
    passenger->set_name("Passenger");
    CHECK(target != nullptr);
    if (target == nullptr) {
        memdelete(passenger);
        return;
    }
    rig.branch(-1)->add_child(target);
    rig.branch(-1)->add_child(passenger);

    const RID seat = core->entity_create();
    const int64_t open_route = core->entity_admit(seat);
    NETW_CHECK_EQ(int(core->entity_bind_node(seat, target)), int(OK));

    const RID carried = core->entity_create();
    const int64_t hidden_route = core->entity_admit(carried);
    NETW_CHECK_EQ(int(core->entity_bind_node(carried, passenger)), int(OK));
    const Ref<NetwEntity> hidden = core->entity_get_view(carried);
    const Ref<netw::NetwInterestLayer> curtain
        = core->interest_layer(StringName("hidden"));
    CHECK(curtain.is_valid());
    CHECK(hidden.is_valid());
    if (!curtain.is_valid() || !hidden.is_valid()) {
        target->get_parent()->remove_child(target);
        passenger->get_parent()->remove_child(passenger);
        memdelete(target);
        memdelete(passenger);
        return;
    }
    curtain->add_entity(hidden);
    rig.pump(2);

    const Ref<NetwEntity> host = core->entity_get_view(seat);
    LocalVector<netw::call_args::Slot> encoded;
    encoded.push_back(netw::call_args::of_node(hidden_route, 0, String()));
    encoded.push_back(netw::call_args::of_value(3));
    netw::wire::WriteStream writer;
    uint64_t flags = 0;
    REQUIRE(writer.bits(flags, 8));
    REQUIRE(
        script_model::write_call_body(
            writer,
            core->rpc_method_token(host, target, StringName("receive_node")),
            encoded,
            Array(),
            Array()
        )
    );
    REQUIRE(writer.align_verify());

    core->receive_carrier(
        netw::NetwMultiplayer::frame_pack(
            open_route,
            0,
            netw::wire::builtin_channel("CALL"),
            writer.to_bytes(),
            String()
        ),
        2,
        true,
        -1,
        -1
    );

    CHECK(Object::cast_to<Node>(target->get("last_node")) == nullptr);
    NETW_CHECK_EQ(int64_t(target->get("last_value")), -1);

    target->get_parent()->remove_child(target);
    passenger->get_parent()->remove_child(passenger);
    memdelete(target);
    memdelete(passenger);
}

TEST_CASE(
    "[Networked][Rpc] a call whose node argument names a route this peer "
    "does not know yet is PARKED until that route arrives, because a spawn "
    "and the call that names it can cross in either order"
) {
    LoopbackRig rig(0);
    rig.mount();
    NetwMultiplayer *core = rig.server();
    REQUIRE(core != nullptr);

    Node *target = an_arg_probe();
    REQUIRE(target != nullptr);
    rig.branch(-1)->add_child(target);

    const RID seat = core->entity_create();
    const int64_t open_route = core->entity_admit(seat);
    NETW_CHECK_EQ(int(core->entity_bind_node(seat, target)), int(OK));
    const Ref<NetwEntity> host = core->entity_get_view(seat);

    const int64_t unknown_route = open_route + 4096;
    NETW_CHECK_EQ(
        int(core->liveness_route_state(unknown_route)),
        int(NetwMultiplayer::ENTITY_STATE_UNKNOWN)
    );
    const int64_t parked_before = core->liveness_pending_live_count();

    LocalVector<netw::call_args::Slot> encoded;
    encoded.push_back(netw::call_args::of_node(unknown_route, 0, String()));
    encoded.push_back(netw::call_args::of_value(3));
    netw::wire::WriteStream writer;
    uint64_t flags = 0;
    REQUIRE(writer.bits(flags, 8));
    REQUIRE(
        script_model::write_call_body(
            writer,
            core->rpc_method_token(host, target, StringName("receive_node")),
            encoded,
            Array(),
            Array()
        )
    );
    REQUIRE(writer.align_verify());

    core->receive_carrier(
        netw::NetwMultiplayer::frame_pack(
            open_route,
            0,
            netw::wire::builtin_channel("CALL"),
            writer.to_bytes(),
            String()
        ),
        2,
        true,
        -1,
        -1
    );

    NETW_CHECK_EQ(int64_t(target->get("last_value")), int64_t(-1));
    NETW_CHECK_EQ(
        core->liveness_pending_live_count() - parked_before,
        int64_t(1)
    );

    target->get_parent()->remove_child(target);
    memdelete(target);
}

} // namespace TestNetwRpcGroupSettleLaws

#endif
