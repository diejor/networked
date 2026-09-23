#include "support/netw_test.h"

#include "netw/display/decl.hpp"
#include "netw/display/role_facts.hpp"
#include "netw/sim/resolve.hpp"

namespace TestNetwDisplayRole {

using namespace godot;
using netw::display::Decl;
using netw::display::resolve_role_facts;
using netw::display::role_for_mode;
using netw::display::RoleFacts;
using netw::sim::Mode;

struct ModeRow {
    const char *label;
    Mode mode;
    bool predicted;
    bool controlled_locally;
    netw::display::Role role;
};

const ModeRow MODE_ROWS[] = {
    {"a controller that predicts on the authority draws its prediction",
     Mode::AUTHORITY,
     true,
     true,
     netw::display::ROLE_PREDICTED},
    {"an authority running a predicted entity it does not control draws "
     "its own authority",
     Mode::AUTHORITY,
     true,
     false,
     netw::display::ROLE_AUTHORITY},
    {"an authority over an unpredicted entity asks the ladder",
     Mode::AUTHORITY,
     false,
     true,
     netw::display::ROLE_AUTO},
    {"a predicting controller draws its prediction",
     Mode::PREDICT,
     true,
     true,
     netw::display::ROLE_PREDICTED},
    {"an active copy of a predicted entity runs live",
     Mode::ACTIVE,
     true,
     false,
     netw::display::ROLE_PREDICTED},
    {"an active copy of an unpredicted entity runs live",
     Mode::ACTIVE,
     false,
     false,
     netw::display::ROLE_PREDICTED},
    {"a proxy of a predicted entity is remote even on its controller",
     Mode::PROXY,
     true,
     true,
     netw::display::ROLE_REMOTE},
    {"a proxy of an unpredicted entity asks the ladder",
     Mode::PROXY,
     false,
     true,
     netw::display::ROLE_AUTO},
    {"an entity with no row asks the ladder",
     Mode::NONE,
     true,
     true,
     netw::display::ROLE_AUTO},
};

TEST_CASE(
    "[Networked][Display][Hosted] the auto role derives from the resolved "
    "mode, and only an unpredicted authority or proxy falls to the ladder"
) {
    for (const ModeRow &row : MODE_ROWS) {
        NETW_FORMAT_TEXT(label_text, row.label);
        CAPTURE(label_text);
        NETW_CHECK_EQ(
            role_for_mode(row.mode, row.predicted, row.controlled_locally),
            int(row.role)
        );
    }
}

TEST_CASE(
    "[Networked][Display][Hosted] L1 a peer that simulates what it controls "
    "displays its prediction"
) {
    RoleFacts f;
    f.simulates_locally = true;
    f.controlled_locally = true;
    f.owner_is_authority = true;
    f.authors_streams = true;

    NETW_CHECK_EQ(resolve_role_facts(f), int(netw::display::ROLE_PREDICTED));

    SUBCASE("simulating without control is not a prediction") {
        f.controlled_locally = false;
        NETW_CHECK_EQ(
            resolve_role_facts(f),
            int(netw::display::ROLE_AUTHORITY)
        );
    }

    SUBCASE("control without local simulation is not a prediction either") {
        f.simulates_locally = false;
        f.authors_streams = false;
        NETW_CHECK_EQ(resolve_role_facts(f), int(netw::display::ROLE_DISABLED));
    }
}

TEST_CASE(
    "[Networked][Display][Hosted] L4 holding authority over what you control "
    "outranks authoring the stream"
) {
    RoleFacts f;
    f.owner_is_authority = true;
    f.controlled_locally = true;
    f.authors_streams = true;

    NETW_CHECK_EQ(resolve_role_facts(f), int(netw::display::ROLE_DISABLED));

    SUBCASE("without local control the authored stream wins") {
        f.controlled_locally = false;
        NETW_CHECK_EQ(
            resolve_role_facts(f),
            int(netw::display::ROLE_AUTHORITY)
        );
    }

    SUBCASE("with neither, authority alone disables") {
        f.controlled_locally = false;
        f.authors_streams = false;
        NETW_CHECK_EQ(resolve_role_facts(f), int(netw::display::ROLE_DISABLED));
    }

    SUBCASE("a peer that neither authors nor holds authority is remote") {
        f.owner_is_authority = false;
        f.controlled_locally = false;
        f.authors_streams = false;
        NETW_CHECK_EQ(resolve_role_facts(f), int(netw::display::ROLE_REMOTE));
    }
}

TEST_CASE(
    "[Networked][Display][Hosted] the ladder answers a real role for every "
    "state its facts can be in"
) {
    RoleFacts f;
    int seen[netw::display::ROLE_MAX] = {0, 0, 0, 0, 0};

    for (int state = 0; state < 16; ++state) {
        f.simulates_locally = (state & 1) != 0;
        f.controlled_locally = (state & 2) != 0;
        f.authors_streams = (state & 4) != 0;
        f.owner_is_authority = (state & 8) != 0;

        const int role = resolve_role_facts(f);
        const bool names_a_role
            = role > netw::display::ROLE_AUTO && role < netw::display::ROLE_MAX;
        CHECK(names_a_role);
        if (!names_a_role) {
            break;
        }
        seen[role] += 1;
        NETW_CHECK_EQ(resolve_role_facts(f), role);
    }

    NETW_CHECK_GT(seen[netw::display::ROLE_REMOTE], 0);
    NETW_CHECK_GT(seen[netw::display::ROLE_PREDICTED], 0);
    NETW_CHECK_GT(seen[netw::display::ROLE_DISABLED], 0);
    NETW_CHECK_GT(seen[netw::display::ROLE_AUTHORITY], 0);
}

TEST_CASE(
    "[Networked][Display][Hosted] L5 the pump a role is driven by, and the two "
    "roles that share one"
) {
    Decl decl;

    NETW_CHECK_EQ(
        decl.pump_for(netw::display::ROLE_AUTHORITY),
        int(netw::display::PUMP_BRACKETED)
    );
    decl.set_param(
        netw::display::PARAM_LIVE_MODE,
        netw::display::LIVE_BRACKETED
    );
    NETW_CHECK_EQ(
        decl.pump_for(netw::display::ROLE_PREDICTED),
        int(netw::display::PUMP_BRACKETED)
    );

    SUBCASE("a chasing live mode is the other pump") {
        decl.set_param(
            netw::display::PARAM_LIVE_MODE,
            netw::display::LIVE_CHASE
        );
        NETW_CHECK_EQ(
            decl.pump_for(netw::display::ROLE_PREDICTED),
            int(netw::display::PUMP_CHASE)
        );
        NETW_CHECK_EQ(
            decl.pump_for(netw::display::ROLE_AUTHORITY),
            int(netw::display::PUMP_BRACKETED)
        );
    }

    SUBCASE("remote has its own, and everything else runs nothing") {
        NETW_CHECK_EQ(
            decl.pump_for(netw::display::ROLE_REMOTE),
            int(netw::display::PUMP_REMOTE)
        );
        NETW_CHECK_EQ(
            decl.pump_for(netw::display::ROLE_DISABLED),
            int(netw::display::PUMP_DISABLED)
        );
        NETW_CHECK_EQ(
            decl.pump_for(netw::display::ROLE_AUTO),
            int(netw::display::PUMP_DISABLED)
        );
    }
}

} // namespace TestNetwDisplayRole
