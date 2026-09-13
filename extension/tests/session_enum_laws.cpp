#include "support/netw_test.h"

#include "netw/api/member_config.hpp"
#include "netw/api/netw_multiplayer.hpp"
#include "netw/api/property_set.hpp"
#include "netw/api/schema_core.hpp"
#include "netw/display/decl.hpp"
#include "netw/liveness_core.hpp"

namespace TestNetwSessionEnums {

using netw::NetwLivenessCore;
using netw::NetwMemberConfig;
using netw::NetwMultiplayer;
using netw::NetwPropertySet;
using netw::SchemaCore;

TEST_CASE(
    "[Networked][Session][Hosted] L1 every session enum that mirrors another "
    "class carries that class's value rather than a copy of it"
) {
    SUBCASE("the record kinds are the property set's") {
        NETW_CHECK_EQ(
            NetwMultiplayer::RECORD_KIND_STATE,
            NetwPropertySet::RECORD_STATE
        );
        NETW_CHECK_EQ(
            NetwMultiplayer::RECORD_KIND_BROADCAST,
            NetwPropertySet::RECORD_BROADCAST
        );
    }

    SUBCASE("the entity states are the liveness core's") {
        NETW_CHECK_EQ(
            NetwMultiplayer::ENTITY_STATE_LIVE,
            NetwLivenessCore::STATE_LIVE
        );
        NETW_CHECK_EQ(
            NetwMultiplayer::ENTITY_STATE_DEAD,
            NetwLivenessCore::STATE_DEAD
        );
        NETW_CHECK_EQ(
            NetwMultiplayer::ENTITY_STATE_ABSENT,
            NetwLivenessCore::STATE_ABSENT
        );
    }

    SUBCASE("the layer policies are the interest engine's") {
        NETW_CHECK_EQ(
            NetwMultiplayer::LAYER_POLICY_HIDE_FROM_OUTSIDERS,
            netw::interest::Engine::HIDE_FROM_OUTSIDERS
        );
        NETW_CHECK_EQ(
            NetwMultiplayer::LAYER_POLICY_HIDE_FROM_INSIDERS,
            netw::interest::Engine::HIDE_FROM_INSIDERS
        );
    }

    SUBCASE("the display roles are the display declaration's") {
        NETW_CHECK_EQ(
            NetwMultiplayer::DISPLAY_ROLE_AUTHORITY,
            netw::display::ROLE_AUTHORITY
        );
        NETW_CHECK_EQ(
            NetwMultiplayer::PREDICTED_MODE_BRACKETED,
            netw::display::PREDICTED_BRACKETED
        );
        NETW_CHECK_EQ(
            NetwMultiplayer::TIMELINE_MODE_FORECAST,
            netw::display::TIMELINE_FORECAST
        );
    }

    SUBCASE("the write policies are the member config's") {
        NETW_CHECK_EQ(
            NetwMultiplayer::WRITE_POLICY_ANY_PEER,
            NetwMemberConfig::POLICY_ANY_PEER
        );
    }

    SUBCASE("the column types are the schema core's") {
        NETW_CHECK_EQ(NetwMultiplayer::COLUMN_F32, SchemaCore::F32);
        NETW_CHECK_EQ(NetwMultiplayer::COLUMN_VARIANT, SchemaCore::VARIANT);
        NETW_CHECK_EQ(
            NetwMultiplayer::COLUMN_VARIANT + 1,
            SchemaCore::COLUMN_TYPE_COUNT
        );
    }
}

TEST_CASE(
    "[Networked][Session][Hosted] L2 the stat enum is exactly the stat table, "
    "so a counter cannot be named without an index or indexed without a name"
) {
    int counted = 0;
#define NETW_SESSION_STAT_COUNT(m_name, m_key) counted++;
    NETW_SESSION_STAT_TABLE(NETW_SESSION_STAT_COUNT)
#undef NETW_SESSION_STAT_COUNT

    NETW_CHECK_EQ(counted, NetwMultiplayer::STAT_COUNT);
    NETW_CHECK_EQ(NetwMultiplayer::STAT_COUNT, 97);
    NETW_CHECK_EQ(NetwMultiplayer::STAT_DROPS_UNKNOWN_ROUTE, 0);
    NETW_CHECK_EQ(NetwMultiplayer::STAT_JOINT_LINGER_HELD, 88);
    NETW_CHECK_EQ(NetwMultiplayer::STAT_DROPS_DEAD_ROUTE, 90);
    NETW_CHECK_EQ(NetwMultiplayer::STAT_ATTRIBUTION_DROPPED_OUT, 96);
}

} // namespace TestNetwSessionEnums
