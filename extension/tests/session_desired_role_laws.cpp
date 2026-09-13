#include "support/netw_test.h"

#include "netw/api/netw_multiplayer.hpp"
#include "netw/api/session_config.hpp"
#include "netw/session_core.hpp"

namespace TestNetwSessionDesiredRoleLaws {

using namespace godot;
using netw::NetwMultiplayer;
using netw::NetwSessionConfig;
using netw::SessionCore;

static Ref<NetwSessionConfig> authoring(int64_t p_role) {
    Ref<NetwSessionConfig> config;
    config.instantiate();
    config->set_desired_role(p_role);
    return config;
}

TEST_CASE(
    "[Networked][Session][Hosted] AD1 the authored role is owned by the "
    "session rather than read off the Resource it came from, so re-authoring "
    "that Resource afterwards moves nothing and the getter answers a detached "
    "copy nobody can configure the session through"
) {
    Ref<NetwMultiplayer> core;
    core.instantiate();
    const Ref<NetwSessionConfig> config
        = authoring(SessionCore::ROLE_LISTEN_SERVER);
    core->session_initialize(config);

    NETW_CHECK_EQ(
        int(core->session_get_authored_role()),
        int(SessionCore::ROLE_LISTEN_SERVER)
    );

    config->set_desired_role(SessionCore::ROLE_CLIENT);

    NETW_CHECK_EQ(
        int(core->session_get_authored_role()),
        int(SessionCore::ROLE_LISTEN_SERVER)
    );
    const bool answers_a_detached_copy
        = core->session_get_config().ptr() != config.ptr();
    CHECK(answers_a_detached_copy);

    core->session_get_config()->set_desired_role(SessionCore::ROLE_CLIENT);

    NETW_CHECK_EQ(
        int(core->session_get_authored_role()),
        int(SessionCore::ROLE_LISTEN_SERVER)
    );
}

TEST_CASE(
    "[Networked][Session][Hosted] AD2 the authored role and the session "
    "machine's own desired role are separate facts, so neither one moves when "
    "the other is written"
) {
    Ref<NetwMultiplayer> core;
    core.instantiate();
    SessionCore &session = core->session_plane();
    const Ref<NetwSessionConfig> config = authoring(SessionCore::ROLE_CLIENT);
    core->session_initialize(config);

    NETW_CHECK_EQ(
        int(core->session_get_authored_role()),
        int(SessionCore::ROLE_CLIENT)
    );

    session.set_desired_role(SessionCore::ROLE_DEDICATED_SERVER);

    NETW_CHECK_EQ(
        int(core->session_get_authored_role()),
        int(SessionCore::ROLE_CLIENT)
    );
    NETW_CHECK_EQ(
        int(session.get_desired_role()),
        int(SessionCore::ROLE_DEDICATED_SERVER)
    );
}

TEST_CASE(
    "[Networked][Session][Hosted] AD3 a session with no config registered "
    "intends to listen-server off a default config it already holds, which is "
    "the role an unconfigured session already carries"
) {
    Ref<NetwMultiplayer> core;
    core.instantiate();

    CHECK(core->session_get_config().is_valid());
    NETW_CHECK_EQ(
        int(core->session_get_authored_role()),
        int(SessionCore::ROLE_LISTEN_SERVER)
    );

    core->session_initialize(authoring(SessionCore::ROLE_DEDICATED_SERVER));

    NETW_CHECK_EQ(
        int(core->session_get_authored_role()),
        int(SessionCore::ROLE_DEDICATED_SERVER)
    );
}

TEST_CASE(
    "[Networked][Session][Hosted] AD4 a role outside the enum is refused by "
    "the draft and leaves the previous role standing, so nothing downstream "
    "is ever handed a role it cannot switch on"
) {
    Ref<NetwMultiplayer> core;
    core.instantiate();
    const Ref<NetwSessionConfig> config = authoring(SessionCore::ROLE_CLIENT);
    core->session_initialize(config);

    config->set_desired_role(9);

    NETW_CHECK_EQ(
        int(config->get_desired_role()),
        int(SessionCore::ROLE_CLIENT)
    );
    NETW_CHECK_EQ(
        int(core->session_get_authored_role()),
        int(SessionCore::ROLE_CLIENT)
    );

    config->set_desired_role(-1);

    NETW_CHECK_EQ(
        int(config->get_desired_role()),
        int(SessionCore::ROLE_CLIENT)
    );
    NETW_CHECK_EQ(
        int(core->session_get_authored_role()),
        int(SessionCore::ROLE_CLIENT)
    );
}

TEST_CASE(
    "[Networked][Session][Hosted] AD5 a session initializes once, so a second "
    "configuration is refused as late and the running role stands rather than "
    "a reconnect quietly reopening initialization"
) {
    Ref<NetwMultiplayer> core;
    core.instantiate();
    NETW_CHECK_EQ(
        int(core->session_initialize(authoring(SessionCore::ROLE_CLIENT))),
        int(OK)
    );

    NETW_CHECK_EQ(
        int(core->session_get_authored_role()),
        int(SessionCore::ROLE_CLIENT)
    );

    NETW_CHECK_EQ(
        int(core->session_initialize(
            authoring(SessionCore::ROLE_DEDICATED_SERVER)
        )),
        int(ERR_ALREADY_IN_USE)
    );

    NETW_CHECK_EQ(
        int(core->session_get_authored_role()),
        int(SessionCore::ROLE_CLIENT)
    );
}

} // namespace TestNetwSessionDesiredRoleLaws
