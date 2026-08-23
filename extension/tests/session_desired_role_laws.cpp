#include "support/netw_test.h"

#include "netw/api/netw_multiplayer.hpp"
#include "netw/session_core.hpp"
#include "support/netw_call_log.h"

namespace TestNetwSessionDesiredRoleLaws {

using namespace godot;
using netw::NetwMultiplayerCore;
using netw::SessionCore;
using netw_test::CallLog;

TEST_CASE(
    "[Networked][Session][Hosted] AD1 the authored role is read through its "
    "installed reader on every ask, so a configuration re-authored between "
    "two role edges is followed rather than answered from the last one"
) {
    Ref<NetwMultiplayerCore> core;
    core.instantiate();
    const CallLog read;
    core->set_desired_role_reader(
        read.answering("authored", int(SessionCore::ROLE_LISTEN_SERVER))
    );

    NETW_CHECK_EQ(
        int(core->authored_desired_role()),
        int(SessionCore::ROLE_LISTEN_SERVER)
    );
    NETW_CHECK_EQ(
        int(core->authored_desired_role()),
        int(SessionCore::ROLE_LISTEN_SERVER)
    );
    NETW_CHECK_EQ(read.count("authored"), 2);

    core->set_desired_role_reader(
        read.answering("authored", int(SessionCore::ROLE_CLIENT))
    );

    NETW_CHECK_EQ(
        int(core->authored_desired_role()),
        int(SessionCore::ROLE_CLIENT)
    );
    NETW_CHECK_EQ(read.count("authored"), 3);
}

TEST_CASE(
    "[Networked][Session][Hosted] AD2 the authored role and the session "
    "machine's own desired role are separate facts, so neither one moves when "
    "the other is written"
) {
    Ref<NetwMultiplayerCore> core;
    core.instantiate();
    SessionCore &session = core->session_plane();
    const CallLog read;
    core->set_desired_role_reader(
        read.answering("authored", int(SessionCore::ROLE_CLIENT))
    );

    NETW_CHECK_EQ(
        int(core->authored_desired_role()),
        int(SessionCore::ROLE_CLIENT)
    );
    NETW_CHECK_EQ(
        int(session.get_desired_role()),
        int(SessionCore::ROLE_LISTEN_SERVER)
    );

    session.set_desired_role(SessionCore::ROLE_DEDICATED_SERVER);

    NETW_CHECK_EQ(
        int(core->authored_desired_role()),
        int(SessionCore::ROLE_CLIENT)
    );

    core->set_desired_role_reader(
        read.answering("authored", int(SessionCore::ROLE_LISTEN_SERVER))
    );

    NETW_CHECK_EQ(
        int(session.get_desired_role()),
        int(SessionCore::ROLE_DEDICATED_SERVER)
    );
    NETW_CHECK_EQ(
        int(core->authored_desired_role()),
        int(SessionCore::ROLE_LISTEN_SERVER)
    );
}

TEST_CASE(
    "[Networked][Session][Hosted] AD3 a session with no authored reader "
    "installed intends to listen-server and asks nobody, which is the role an "
    "unconfigured session already carries"
) {
    Ref<NetwMultiplayerCore> core;
    core.instantiate();

    NETW_CHECK_EQ(
        int(core->authored_desired_role()),
        int(SessionCore::ROLE_LISTEN_SERVER)
    );

    const CallLog read;
    core->set_desired_role_reader(
        read.answering("authored", int(SessionCore::ROLE_DEDICATED_SERVER))
    );

    NETW_CHECK_EQ(
        int(core->authored_desired_role()),
        int(SessionCore::ROLE_DEDICATED_SERVER)
    );
    NETW_CHECK_EQ(read.count("authored"), 1);
}

TEST_CASE(
    "[Networked][Session][Hosted] AD4 a reader that answers something outside "
    "the role enum answers listen-server, so nothing downstream is ever handed "
    "a role it cannot switch on"
) {
    Ref<NetwMultiplayerCore> core;
    core.instantiate();
    const CallLog read;

    core->set_desired_role_reader(read.answering("authored", Variant()));

    NETW_CHECK_EQ(
        int(core->authored_desired_role()),
        int(SessionCore::ROLE_LISTEN_SERVER)
    );

    core->set_desired_role_reader(read.answering("authored", String("client")));

    NETW_CHECK_EQ(
        int(core->authored_desired_role()),
        int(SessionCore::ROLE_LISTEN_SERVER)
    );

    core->set_desired_role_reader(read.answering("authored", 9));

    NETW_CHECK_EQ(
        int(core->authored_desired_role()),
        int(SessionCore::ROLE_LISTEN_SERVER)
    );

    core->set_desired_role_reader(read.answering("authored", -1));

    NETW_CHECK_EQ(
        int(core->authored_desired_role()),
        int(SessionCore::ROLE_LISTEN_SERVER)
    );
    NETW_CHECK_EQ(read.count("authored"), 4);
}

} // namespace TestNetwSessionDesiredRoleLaws
