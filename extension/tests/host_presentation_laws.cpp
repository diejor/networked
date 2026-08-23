#include "support/netw_test.h"

#include "netw/api/netw_multiplayer.hpp"
#include "netw/session_core.hpp"
#include "support/netw_call_log.h"

namespace TestNetwHostPresentationLaws {

using namespace godot;
using netw::NetwMultiplayerCore;
using netw::SessionCore;
using netw_test::CallLog;

TEST_CASE(
    "[Networked][Session][Hosted] HP1 a session already holding the "
    "listen-server role presents as a listen host whatever its configuration "
    "was re-authored to since, because the role it resolved is the one it is "
    "running"
) {
    Ref<NetwMultiplayerCore> core;
    core.instantiate();
    const CallLog read;
    core->set_desired_role_reader(
        read.answering("authored", int(SessionCore::ROLE_CLIENT))
    );
    core->session_plane().set_role(SessionCore::ROLE_LISTEN_SERVER);

    CHECK(core->presents_as_listen_host());

    core->set_desired_role_reader(
        read.answering(
            "authored",
            int(SessionCore::ROLE_DEDICATED_SERVER)
        )
    );

    CHECK(core->presents_as_listen_host());
}

TEST_CASE(
    "[Networked][Session][Hosted] HP2 a session that has resolved no role yet "
    "presents as a listen host on its authored intent alone, so a host builds "
    "its presentation before the role edge rather than after it"
) {
    Ref<NetwMultiplayerCore> core;
    core.instantiate();
    const CallLog read;
    core->set_desired_role_reader(
        read.answering("authored", int(SessionCore::ROLE_LISTEN_SERVER))
    );

    NETW_CHECK_EQ(int(core->get_role()), int(SessionCore::ROLE_NONE));
    CHECK(core->presents_as_listen_host());
    NETW_CHECK_EQ(read.count("authored"), 1);
}

TEST_CASE(
    "[Networked][Session][Hosted] HP3 a dedicated server presents as no "
    "listen host at either reading, so holding server authority is not what "
    "the question asks"
) {
    Ref<NetwMultiplayerCore> core;
    core.instantiate();
    const CallLog read;
    core->set_desired_role_reader(
        read.answering(
            "authored",
            int(SessionCore::ROLE_DEDICATED_SERVER)
        )
    );
    core->session_plane().set_role(SessionCore::ROLE_DEDICATED_SERVER);

    CHECK_FALSE(core->presents_as_listen_host());

    core->session_plane().set_role(SessionCore::ROLE_CLIENT);
    core->set_desired_role_reader(
        read.answering("authored", int(SessionCore::ROLE_CLIENT))
    );

    CHECK_FALSE(core->presents_as_listen_host());
}

TEST_CASE(
    "[Networked][Session][Hosted] HP4 both readings are taken at every ask, "
    "so authoring a different intent and resolving a different role each turn "
    "the answer over on their own edge with no second edge to prompt it"
) {
    Ref<NetwMultiplayerCore> core;
    core.instantiate();
    const CallLog read;
    core->set_desired_role_reader(
        read.answering("authored", int(SessionCore::ROLE_CLIENT))
    );
    core->session_plane().set_role(SessionCore::ROLE_CLIENT);

    CHECK_FALSE(core->presents_as_listen_host());

    core->set_desired_role_reader(
        read.answering("authored", int(SessionCore::ROLE_LISTEN_SERVER))
    );

    CHECK(core->presents_as_listen_host());

    core->set_desired_role_reader(
        read.answering("authored", int(SessionCore::ROLE_CLIENT))
    );

    CHECK_FALSE(core->presents_as_listen_host());

    core->session_plane().set_role(SessionCore::ROLE_LISTEN_SERVER);

    CHECK(core->presents_as_listen_host());
    NETW_CHECK_EQ(read.count("authored"), 3);
}

TEST_CASE(
    "[Networked][Session][Hosted] HP5 a session with no configuration behind "
    "it presents as a listen host, because an unconfigured session already "
    "intends to host and the presentation follows the intent"
) {
    Ref<NetwMultiplayerCore> core;
    core.instantiate();

    NETW_CHECK_EQ(int(core->get_role()), int(SessionCore::ROLE_NONE));
    CHECK(core->presents_as_listen_host());
}

} // namespace TestNetwHostPresentationLaws
