#include "support/netw_test.h"

#if defined(NETW_TIER_HOSTED)

#include "support/loopback_rig.h"
#include "support/minted_script.h"
#include "support/netw_call_log.h"

#include "godot/node.hpp"
#include "godot/object.hpp"
#include "godot/script.hpp"
#include "netw/api/context.hpp"
#include "netw/api/netw_multiplayer.hpp"
#include "netw/api/participant.hpp"
#include "netw/api/session.hpp"

namespace TestPlayerLeaveReasonLaws {

using namespace godot;
using netw_test::CallLog;
using netw_test::LoopbackRig;

constexpr const char *ACCOUNT_GATE = R"(extends Node

func admit(request: NetwJoinRequest) -> Error:
	request.account = "test:" + request.username
	return OK
)";

TEST_CASE(
    "[Networked][Session][SceneTree] a player replaced by a second login of "
    "its account is told why before its connection closes"
) {
    const Ref<Script> shape = netw_test::minted_script(ACCOUNT_GATE);
    REQUIRE(shape.is_valid());
    LoopbackRig rig(2);
    rig.mount();
    Node *gate = Object::cast_to<Node>(shape->call("new"));
    rig.branch(-1)->add_child(gate);
    REQUIRE(
        netw::Netw::configure_admission(Callable(gate, StringName("admit")))
            .is_valid()
    );
    rig.join(-1, StringName("host"));
    rig.join(0, StringName("ana"));
    rig.join(1, StringName("bo"));
    const CallLog heard;
    rig.client(0)->get_session()->connect(
        StringName("exiting"),
        heard.callable("exiting")
    );
    rig.client(1)->get_session()->connect(
        StringName("player_left"),
        heard.callable("player_left")
    );

    const int second = rig.add_client();
    rig.mount_late(second);
    rig.join(second, StringName("ana"));
    rig.pump(24);

    NETW_REQUIRE_EQ(heard.count(StringName("player_left")), 1);
    NETW_CHECK_EQ(
        int(heard.args(StringName("player_left"))[1]),
        int(netw::NetwPlayer::LEAVE_DROPPED)
    );

    CHECK(rig.player(second).is_valid());
    CHECK_FALSE(rig.client(0)->is_online());
    NETW_REQUIRE_EQ(heard.count(StringName("exiting")), 1);
    const Array exiting = heard.args(StringName("exiting"));
    NETW_CHECK_EQ(int(exiting[0]), int(netw::NetwSession::KICKED));
    CHECK(bool(String(exiting[1]) == String("Signed in again.")));
    gate->get_parent()->remove_child(gate);
    memdelete(gate);
}

TEST_CASE(
    "[Networked][Session][SceneTree] a host that quits shuts the session "
    "down, unless its transport moves the server on host loss"
) {
    for (const bool migrates : {false, true}) {
        NETW_FORMAT_INT(migrates_text, int64_t(migrates));
        CAPTURE(migrates_text);
        LoopbackRig rig(1, migrates);
        rig.mount();
        rig.join(-1, StringName("host"));
        rig.join(0, StringName("ana"));
        const CallLog heard;
        rig.client(0)->get_session()->connect(
            StringName("exiting"),
            heard.callable("exiting")
        );
        rig.client(0)->get_session()->connect(
            StringName("player_left"),
            heard.callable("player_left")
        );

        rig.server()->session_leave_on_quit();
        rig.pump(8);

        if (migrates) {
            CHECK(rig.client(0)->is_online());
            NETW_CHECK_EQ(heard.count(StringName("exiting")), 0);
            NETW_CHECK_EQ(heard.count(StringName("player_left")), 0);
            continue;
        }
        CHECK_FALSE(rig.client(0)->is_online());
        NETW_REQUIRE_EQ(heard.count(StringName("exiting")), 1);
        NETW_CHECK_EQ(
            int(heard.args(StringName("exiting"))[0]),
            int(netw::NetwSession::SERVER_SHUTDOWN)
        );
        NETW_REQUIRE_EQ(heard.count(StringName("player_left")), 1);
        const Array left = heard.args(StringName("player_left"));
        NETW_CHECK_EQ(
            Ref<netw::NetwPlayer>(left[0])->get_peer_id(),
            int64_t(1)
        );
        NETW_CHECK_EQ(int(left[1]), int(netw::NetwPlayer::LEAVE_QUIT));
    }
}

} // namespace TestPlayerLeaveReasonLaws

#endif
