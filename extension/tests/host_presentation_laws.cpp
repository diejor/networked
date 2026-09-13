#include "support/netw_test.h"

#include "netw/api/netw_multiplayer.hpp"
#include "netw/api/session_config.hpp"
#include "netw/session_core.hpp"

namespace TestNetwHostPresentationLaws {

using namespace godot;
using netw::NetwMultiplayer;
using netw::NetwSessionConfig;
using netw::SessionCore;

static Ref<NetwSessionConfig> author(
    const Ref<NetwMultiplayer> &p_core,
    int64_t p_role
) {
    Ref<NetwSessionConfig> config;
    config.instantiate();
    config->set_desired_role(p_role);
    p_core->session_initialize(config);
    return config;
}

TEST_CASE(
    "[Networked][Session][Hosted] HP1 a session already holding the "
    "listen-server role presents as a listen host whatever its configuration "
    "was re-authored to since, because the role it resolved is the one it is "
    "running"
) {
    Ref<NetwMultiplayer> core;
    core.instantiate();
    const Ref<NetwSessionConfig> config
        = author(core, SessionCore::ROLE_CLIENT);
    core->session_plane().set_role(SessionCore::ROLE_LISTEN_SERVER);

    CHECK(core->presents_as_listen_host());

    config->set_desired_role(SessionCore::ROLE_DEDICATED_SERVER);

    CHECK(core->presents_as_listen_host());
}

TEST_CASE(
    "[Networked][Session][Hosted] HP2 a session that has resolved no role yet "
    "presents as a listen host on its authored intent alone, so a host builds "
    "its presentation before the role edge rather than after it"
) {
    Ref<NetwMultiplayer> core;
    core.instantiate();
    author(core, SessionCore::ROLE_LISTEN_SERVER);

    NETW_CHECK_EQ(int(core->session_get_role()), int(SessionCore::ROLE_NONE));
    CHECK(core->presents_as_listen_host());
}

TEST_CASE(
    "[Networked][Session][Hosted] HP3 a dedicated server presents as no "
    "listen host at either reading, so holding server authority is not what "
    "the question asks"
) {
    Ref<NetwMultiplayer> core;
    core.instantiate();
    author(core, SessionCore::ROLE_DEDICATED_SERVER);
    core->session_plane().set_role(SessionCore::ROLE_DEDICATED_SERVER);

    CHECK_FALSE(core->presents_as_listen_host());

    core->session_plane().set_role(SessionCore::ROLE_CLIENT);

    CHECK_FALSE(core->presents_as_listen_host());
}

TEST_CASE(
    "[Networked][Session][Hosted] HP4 both readings are taken at every ask, "
    "so the resolved role turns the answer over on its own edge, while the "
    "Resource the intent came from can no longer turn anything over at all"
) {
    Ref<NetwMultiplayer> core;
    core.instantiate();
    const Ref<NetwSessionConfig> config
        = author(core, SessionCore::ROLE_CLIENT);
    core->session_plane().set_role(SessionCore::ROLE_CLIENT);

    CHECK_FALSE(core->presents_as_listen_host());

    config->set_desired_role(SessionCore::ROLE_LISTEN_SERVER);

    CHECK_FALSE(core->presents_as_listen_host());

    core->session_plane().set_role(SessionCore::ROLE_LISTEN_SERVER);

    CHECK(core->presents_as_listen_host());

    core->session_plane().set_role(SessionCore::ROLE_CLIENT);

    CHECK_FALSE(core->presents_as_listen_host());
}

TEST_CASE(
    "[Networked][Session][Hosted] HP5 a session with no configuration behind "
    "it presents as a listen host, because an unconfigured session already "
    "intends to host and the presentation follows the intent"
) {
    Ref<NetwMultiplayer> core;
    core.instantiate();

    NETW_CHECK_EQ(int(core->session_get_role()), int(SessionCore::ROLE_NONE));
    CHECK(core->presents_as_listen_host());
}

} // namespace TestNetwHostPresentationLaws
