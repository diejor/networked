#include "support/netw_test.h"

#include "netw/display/decl.hpp"

namespace TestNetwDisplayDeclValue {

using namespace godot;
using netw::display::Decl;

TEST_CASE(
    "[Networked][Display][Hosted] a write answers how much of the runtime it "
    "invalidated"
) {
    Decl decl;

    NETW_CHECK_EQ(
        decl.set_param(netw::display::PARAM_ROLE, netw::display::ROLE_REMOTE),
        netw::display::DIRT_ROLE
    );
    NETW_CHECK_EQ(
        decl.set_param(
            netw::display::PARAM_PREDICTED_MODE,
            netw::display::PREDICTED_BRACKETED
        ),
        netw::display::DIRT_ROLE
    );
    NETW_CHECK_EQ(
        decl.set_param(netw::display::PARAM_VISUAL_ROOT, NodePath("Visual")),
        netw::display::DIRT_RUNTIME
    );
    NETW_CHECK_EQ(
        decl.set_param(netw::display::PARAM_PREDICTED_SMOOTH_TIME, 0.25),
        netw::display::DIRT_RUNTIME
    );
    NETW_CHECK_EQ(
        decl.set_param(netw::display::PARAM_TRACE_INTERVAL, 4),
        netw::display::DIRT_NONE
    );

    NETW_CHECK_EQ(decl.display_role, netw::display::ROLE_REMOTE);
    NETW_CHECK_EQ(decl.predicted_mode, netw::display::PREDICTED_BRACKETED);
    CHECK(decl.visual_root == NodePath("Visual"));
    NETW_CHECK_CLOSE(decl.predicted_smooth_time, 0.25, 0.0);
    NETW_CHECK_EQ(decl.trace_interval, 4);
}

TEST_CASE(
    "[Networked][Display][Hosted] every param reads back the value it was "
    "written"
) {
    Decl decl;

    decl.set_param(netw::display::PARAM_ROLE, netw::display::ROLE_AUTHORITY);
    decl.set_param(
        netw::display::PARAM_PREDICTED_MODE,
        netw::display::PREDICTED_BRACKETED
    );
    decl.set_param(netw::display::PARAM_PREDICTED_SMOOTH_TIME, 0.5);
    decl.set_param(netw::display::PARAM_CHASE_GLIDE_TIME, 0.75);
    decl.set_param(
        netw::display::PARAM_TIMELINE_MODE,
        netw::display::TIMELINE_FORECAST
    );
    decl.set_param(netw::display::PARAM_MAX_FORECAST_TICKS, 9);
    decl.set_param(netw::display::PARAM_SMART_DILATION, false);
    decl.set_param(netw::display::PARAM_MAX_EXTRA_DILATION, 0.2);
    decl.set_param(netw::display::PARAM_LAG_ADAPT_RATE, 0.3);
    decl.set_param(netw::display::PARAM_STARVATION_GROWTH, 0.4);
    decl.set_param(netw::display::PARAM_FLOOR_SMOOTHING, 0.6);
    decl.set_param(netw::display::PARAM_STARVATION_GRACE_FRAMES, 7);
    decl.set_param(netw::display::PARAM_TRACE_INTERVAL, 8);
    decl.set_param(netw::display::PARAM_VISUAL_ROOT, NodePath("Body/Art"));

    NETW_CHECK_EQ(
        int(decl.get_param(netw::display::PARAM_ROLE)),
        netw::display::ROLE_AUTHORITY
    );
    NETW_CHECK_EQ(
        int(decl.get_param(netw::display::PARAM_PREDICTED_MODE)),
        netw::display::PREDICTED_BRACKETED
    );
    NETW_CHECK_CLOSE(
        double(decl.get_param(netw::display::PARAM_PREDICTED_SMOOTH_TIME)),
        0.5,
        0.0
    );
    NETW_CHECK_CLOSE(
        double(decl.get_param(netw::display::PARAM_CHASE_GLIDE_TIME)),
        0.75,
        0.0
    );
    NETW_CHECK_EQ(
        int(decl.get_param(netw::display::PARAM_TIMELINE_MODE)),
        netw::display::TIMELINE_FORECAST
    );
    NETW_CHECK_EQ(
        int(decl.get_param(netw::display::PARAM_MAX_FORECAST_TICKS)),
        9
    );
    CHECK_FALSE(bool(decl.get_param(netw::display::PARAM_SMART_DILATION)));
    NETW_CHECK_CLOSE(
        double(decl.get_param(netw::display::PARAM_MAX_EXTRA_DILATION)),
        0.2,
        0.0
    );
    NETW_CHECK_CLOSE(
        double(decl.get_param(netw::display::PARAM_LAG_ADAPT_RATE)),
        0.3,
        0.0
    );
    NETW_CHECK_CLOSE(
        double(decl.get_param(netw::display::PARAM_STARVATION_GROWTH)),
        0.4,
        0.0
    );
    NETW_CHECK_CLOSE(
        double(decl.get_param(netw::display::PARAM_FLOOR_SMOOTHING)),
        0.6,
        0.0
    );
    NETW_CHECK_EQ(
        int(decl.get_param(netw::display::PARAM_STARVATION_GRACE_FRAMES)),
        7
    );
    NETW_CHECK_EQ(int(decl.get_param(netw::display::PARAM_TRACE_INTERVAL)), 8);
    CHECK(
        NodePath(decl.get_param(netw::display::PARAM_VISUAL_ROOT))
        == NodePath("Body/Art")
    );
}

TEST_CASE(
    "[Networked][Display][Hosted] an ordinal outside its enum is refused and "
    "leaves the setting alone"
) {
    Decl decl;
    decl.set_param(netw::display::PARAM_ROLE, netw::display::ROLE_REMOTE);
    decl.set_param(
        netw::display::PARAM_PREDICTED_MODE,
        netw::display::PREDICTED_BRACKETED
    );
    decl.set_param(
        netw::display::PARAM_TIMELINE_MODE,
        netw::display::TIMELINE_FORECAST
    );

    ERR_PRINT_OFF;
    NETW_CHECK_EQ(
        decl.set_param(netw::display::PARAM_ROLE, netw::display::ROLE_MAX),
        netw::display::DIRT_NONE
    );
    NETW_CHECK_EQ(
        decl.set_param(netw::display::PARAM_PREDICTED_MODE, -1),
        netw::display::DIRT_NONE
    );
    NETW_CHECK_EQ(
        decl.set_param(
            netw::display::PARAM_TIMELINE_MODE,
            netw::display::TIMELINE_MODE_MAX
        ),
        netw::display::DIRT_NONE
    );
    ERR_PRINT_ON;

    NETW_CHECK_EQ(decl.display_role, netw::display::ROLE_REMOTE);
    NETW_CHECK_EQ(decl.predicted_mode, netw::display::PREDICTED_BRACKETED);
    NETW_CHECK_EQ(decl.timeline_mode, netw::display::TIMELINE_FORECAST);
}

TEST_CASE(
    "[Networked][Display][Hosted] a visual root is a path, and a component id "
    "never reaches the record"
) {
    Decl decl;
    decl.set_param(netw::display::PARAM_VISUAL_ROOT, String("Visual"));
    CHECK(decl.visual_root == NodePath("Visual"));

    ERR_PRINT_OFF;
    NETW_CHECK_EQ(
        decl.set_param(netw::display::PARAM_VISUAL_ROOT, 3),
        netw::display::DIRT_NONE
    );
    ERR_PRINT_ON;
    CHECK(decl.visual_root == NodePath("Visual"));
}

TEST_CASE(
    "[Networked][Display][Hosted] a param naming no setting is refused at both "
    "doors"
) {
    Decl decl;

    ERR_PRINT_OFF;
    NETW_CHECK_EQ(
        decl.set_param(netw::display::PARAM_MAX, 1),
        netw::display::DIRT_NONE
    );
    NETW_CHECK_EQ(int(decl.get_param(-1).get_type()), int(Variant::NIL));
    ERR_PRINT_ON;
}

TEST_CASE(
    "[Networked][Display][Hosted] T1 crossing the predicted boundary clears "
    "the history, and the chase demote is the one warm handoff"
) {
    CHECK(
        netw::display::pump_clears_history(
            netw::display::PUMP_REMOTE,
            netw::display::PUMP_CHASE
        )
    );
    CHECK(
        netw::display::pump_clears_history(
            netw::display::PUMP_BRACKETED,
            netw::display::PUMP_DISABLED
        )
    );
    CHECK(
        netw::display::pump_clears_history(
            netw::display::PUMP_UNRESOLVED,
            netw::display::PUMP_BRACKETED
        )
    );

    CHECK_FALSE(
        netw::display::pump_clears_history(
            netw::display::PUMP_CHASE,
            netw::display::PUMP_REMOTE
        )
    );
    CHECK_FALSE(
        netw::display::pump_clears_history(
            netw::display::PUMP_UNRESOLVED,
            netw::display::PUMP_REMOTE
        )
    );
    CHECK_FALSE(
        netw::display::pump_clears_history(
            netw::display::PUMP_REMOTE,
            netw::display::PUMP_DISABLED
        )
    );

    CHECK(netw::display::pump_is_predicted(netw::display::PUMP_CHASE));
    CHECK(netw::display::pump_is_predicted(netw::display::PUMP_BRACKETED));
    CHECK_FALSE(netw::display::pump_is_predicted(netw::display::PUMP_REMOTE));
}

TEST_CASE(
    "[Networked][Display][Hosted] T2 only a transition between two displays "
    "that were showing something arms the render offsets"
) {
    CHECK(
        netw::display::pump_arms_offsets(
            netw::display::PUMP_CHASE,
            netw::display::PUMP_REMOTE
        )
    );
    CHECK(
        netw::display::pump_arms_offsets(
            netw::display::PUMP_REMOTE,
            netw::display::PUMP_BRACKETED
        )
    );

    CHECK_FALSE(
        netw::display::pump_arms_offsets(
            netw::display::PUMP_UNRESOLVED,
            netw::display::PUMP_REMOTE
        )
    );
    CHECK_FALSE(
        netw::display::pump_arms_offsets(
            netw::display::PUMP_DISABLED,
            netw::display::PUMP_REMOTE
        )
    );
    CHECK_FALSE(
        netw::display::pump_arms_offsets(
            netw::display::PUMP_REMOTE,
            netw::display::PUMP_DISABLED
        )
    );
}

} // namespace TestNetwDisplayDeclValue
