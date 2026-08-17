#include "support/netw_test.h"

#include "netw/display_decl.hpp"

namespace TestNetwDisplayDecl {

using namespace godot;
using netw::NetwDisplayDecl;

Ref<NetwDisplayDecl> fresh() {
    Ref<NetwDisplayDecl> decl;
    decl.instantiate();
    return decl;
}

TEST_CASE(
    "[Networked][Display][Hosted] a write answers how much of the runtime it "
    "invalidated"
) {
    const Ref<NetwDisplayDecl> decl = fresh();

    NETW_CHECK_EQ(
        decl->set_param(
            NetwDisplayDecl::PARAM_ROLE,
            NetwDisplayDecl::ROLE_REMOTE
        ),
        NetwDisplayDecl::DIRT_ROLE
    );
    NETW_CHECK_EQ(
        decl->set_param(
            NetwDisplayDecl::PARAM_PREDICTED_MODE,
            NetwDisplayDecl::PREDICTED_BRACKETED
        ),
        NetwDisplayDecl::DIRT_ROLE
    );
    NETW_CHECK_EQ(
        decl->set_param(NetwDisplayDecl::PARAM_VISUAL_ROOT, NodePath("Visual")),
        NetwDisplayDecl::DIRT_RUNTIME
    );
    NETW_CHECK_EQ(
        decl->set_param(NetwDisplayDecl::PARAM_PREDICTED_SMOOTH_TIME, 0.25),
        NetwDisplayDecl::DIRT_RUNTIME
    );
    NETW_CHECK_EQ(
        decl->set_param(NetwDisplayDecl::PARAM_TRACE_INTERVAL, 4),
        NetwDisplayDecl::DIRT_NONE
    );

    NETW_CHECK_EQ(decl->get_display_role(), NetwDisplayDecl::ROLE_REMOTE);
    NETW_CHECK_EQ(
        decl->get_predicted_mode(),
        NetwDisplayDecl::PREDICTED_BRACKETED
    );
    CHECK(decl->get_visual_root() == NodePath("Visual"));
    NETW_CHECK_CLOSE(decl->get_predicted_smooth_time(), 0.25, 0.0);
    NETW_CHECK_EQ(decl->get_trace_interval(), 4);
}

TEST_CASE(
    "[Networked][Display][Hosted] every param reads back the value it was "
    "written"
) {
    const Ref<NetwDisplayDecl> decl = fresh();

    decl->set_param(
        NetwDisplayDecl::PARAM_ROLE,
        NetwDisplayDecl::ROLE_AUTHORITY
    );
    decl->set_param(
        NetwDisplayDecl::PARAM_PREDICTED_MODE,
        NetwDisplayDecl::PREDICTED_BRACKETED
    );
    decl->set_param(NetwDisplayDecl::PARAM_PREDICTED_SMOOTH_TIME, 0.5);
    decl->set_param(NetwDisplayDecl::PARAM_CHASE_GLIDE_TIME, 0.75);
    decl->set_param(
        NetwDisplayDecl::PARAM_TIMELINE_MODE,
        NetwDisplayDecl::TIMELINE_FORECAST
    );
    decl->set_param(NetwDisplayDecl::PARAM_MAX_FORECAST_TICKS, 9);
    decl->set_param(NetwDisplayDecl::PARAM_SMART_DILATION, false);
    decl->set_param(NetwDisplayDecl::PARAM_MAX_EXTRA_DILATION, 0.2);
    decl->set_param(NetwDisplayDecl::PARAM_LAG_ADAPT_RATE, 0.3);
    decl->set_param(NetwDisplayDecl::PARAM_STARVATION_GROWTH, 0.4);
    decl->set_param(NetwDisplayDecl::PARAM_FLOOR_SMOOTHING, 0.6);
    decl->set_param(NetwDisplayDecl::PARAM_STARVATION_GRACE_FRAMES, 7);
    decl->set_param(NetwDisplayDecl::PARAM_TRACE_INTERVAL, 8);
    decl->set_param(NetwDisplayDecl::PARAM_VISUAL_ROOT, NodePath("Body/Art"));

    NETW_CHECK_EQ(
        int(decl->get_param(NetwDisplayDecl::PARAM_ROLE)),
        NetwDisplayDecl::ROLE_AUTHORITY
    );
    NETW_CHECK_EQ(
        int(decl->get_param(NetwDisplayDecl::PARAM_PREDICTED_MODE)),
        NetwDisplayDecl::PREDICTED_BRACKETED
    );
    NETW_CHECK_CLOSE(
        double(decl->get_param(NetwDisplayDecl::PARAM_PREDICTED_SMOOTH_TIME)),
        0.5,
        0.0
    );
    NETW_CHECK_CLOSE(
        double(decl->get_param(NetwDisplayDecl::PARAM_CHASE_GLIDE_TIME)),
        0.75,
        0.0
    );
    NETW_CHECK_EQ(
        int(decl->get_param(NetwDisplayDecl::PARAM_TIMELINE_MODE)),
        NetwDisplayDecl::TIMELINE_FORECAST
    );
    NETW_CHECK_EQ(
        int(decl->get_param(NetwDisplayDecl::PARAM_MAX_FORECAST_TICKS)),
        9
    );
    CHECK_FALSE(bool(decl->get_param(NetwDisplayDecl::PARAM_SMART_DILATION)));
    NETW_CHECK_CLOSE(
        double(decl->get_param(NetwDisplayDecl::PARAM_MAX_EXTRA_DILATION)),
        0.2,
        0.0
    );
    NETW_CHECK_CLOSE(
        double(decl->get_param(NetwDisplayDecl::PARAM_LAG_ADAPT_RATE)),
        0.3,
        0.0
    );
    NETW_CHECK_CLOSE(
        double(decl->get_param(NetwDisplayDecl::PARAM_STARVATION_GROWTH)),
        0.4,
        0.0
    );
    NETW_CHECK_CLOSE(
        double(decl->get_param(NetwDisplayDecl::PARAM_FLOOR_SMOOTHING)),
        0.6,
        0.0
    );
    NETW_CHECK_EQ(
        int(decl->get_param(NetwDisplayDecl::PARAM_STARVATION_GRACE_FRAMES)),
        7
    );
    NETW_CHECK_EQ(
        int(decl->get_param(NetwDisplayDecl::PARAM_TRACE_INTERVAL)),
        8
    );
    CHECK(
        NodePath(decl->get_param(NetwDisplayDecl::PARAM_VISUAL_ROOT))
        == NodePath("Body/Art")
    );
}

TEST_CASE(
    "[Networked][Display][Hosted] an ordinal outside its enum is refused and "
    "leaves the setting alone"
) {
    const Ref<NetwDisplayDecl> decl = fresh();
    decl->set_param(NetwDisplayDecl::PARAM_ROLE, NetwDisplayDecl::ROLE_REMOTE);
    decl->set_param(
        NetwDisplayDecl::PARAM_PREDICTED_MODE,
        NetwDisplayDecl::PREDICTED_BRACKETED
    );
    decl->set_param(
        NetwDisplayDecl::PARAM_TIMELINE_MODE,
        NetwDisplayDecl::TIMELINE_FORECAST
    );

    ERR_PRINT_OFF;
    NETW_CHECK_EQ(
        decl->set_param(NetwDisplayDecl::PARAM_ROLE, NetwDisplayDecl::ROLE_MAX),
        NetwDisplayDecl::DIRT_NONE
    );
    NETW_CHECK_EQ(
        decl->set_param(NetwDisplayDecl::PARAM_PREDICTED_MODE, -1),
        NetwDisplayDecl::DIRT_NONE
    );
    NETW_CHECK_EQ(
        decl->set_param(
            NetwDisplayDecl::PARAM_TIMELINE_MODE,
            NetwDisplayDecl::TIMELINE_MODE_MAX
        ),
        NetwDisplayDecl::DIRT_NONE
    );
    ERR_PRINT_ON;

    NETW_CHECK_EQ(decl->get_display_role(), NetwDisplayDecl::ROLE_REMOTE);
    NETW_CHECK_EQ(
        decl->get_predicted_mode(),
        NetwDisplayDecl::PREDICTED_BRACKETED
    );
    NETW_CHECK_EQ(
        decl->get_timeline_mode(),
        NetwDisplayDecl::TIMELINE_FORECAST
    );
}

TEST_CASE(
    "[Networked][Display][Hosted] a visual root is a path, and a component id "
    "never reaches the record"
) {
    const Ref<NetwDisplayDecl> decl = fresh();
    decl->set_param(NetwDisplayDecl::PARAM_VISUAL_ROOT, String("Visual"));
    CHECK(decl->get_visual_root() == NodePath("Visual"));

    ERR_PRINT_OFF;
    NETW_CHECK_EQ(
        decl->set_param(NetwDisplayDecl::PARAM_VISUAL_ROOT, 3),
        NetwDisplayDecl::DIRT_NONE
    );
    ERR_PRINT_ON;
    CHECK(decl->get_visual_root() == NodePath("Visual"));
}

TEST_CASE(
    "[Networked][Display][Hosted] a param naming no setting is refused at both "
    "doors"
) {
    const Ref<NetwDisplayDecl> decl = fresh();

    ERR_PRINT_OFF;
    NETW_CHECK_EQ(
        decl->set_param(NetwDisplayDecl::PARAM_MAX, 1),
        NetwDisplayDecl::DIRT_NONE
    );
    NETW_CHECK_EQ(int(decl->get_param(-1).get_type()), int(Variant::NIL));
    ERR_PRINT_ON;
}

TEST_CASE(
    "[Networked][Display][Hosted] T1 crossing the predicted boundary clears "
    "the history, and the chase demote is the one warm handoff"
) {
    CHECK(NetwDisplayDecl::pump_clears_history(
        NetwDisplayDecl::PUMP_REMOTE,
        NetwDisplayDecl::PUMP_CHASE
    ));
    CHECK(NetwDisplayDecl::pump_clears_history(
        NetwDisplayDecl::PUMP_BRACKETED,
        NetwDisplayDecl::PUMP_DISABLED
    ));
    CHECK(NetwDisplayDecl::pump_clears_history(
        NetwDisplayDecl::PUMP_UNRESOLVED,
        NetwDisplayDecl::PUMP_BRACKETED
    ));

    CHECK_FALSE(NetwDisplayDecl::pump_clears_history(
        NetwDisplayDecl::PUMP_CHASE,
        NetwDisplayDecl::PUMP_REMOTE
    ));
    CHECK_FALSE(NetwDisplayDecl::pump_clears_history(
        NetwDisplayDecl::PUMP_UNRESOLVED,
        NetwDisplayDecl::PUMP_REMOTE
    ));
    CHECK_FALSE(NetwDisplayDecl::pump_clears_history(
        NetwDisplayDecl::PUMP_REMOTE,
        NetwDisplayDecl::PUMP_DISABLED
    ));

    CHECK(NetwDisplayDecl::pump_is_predicted(NetwDisplayDecl::PUMP_CHASE));
    CHECK(NetwDisplayDecl::pump_is_predicted(NetwDisplayDecl::PUMP_BRACKETED));
    CHECK_FALSE(NetwDisplayDecl::pump_is_predicted(NetwDisplayDecl::PUMP_REMOTE));
}

TEST_CASE(
    "[Networked][Display][Hosted] T2 only a transition between two displays "
    "that were showing something arms the render offsets"
) {
    CHECK(NetwDisplayDecl::pump_arms_offsets(
        NetwDisplayDecl::PUMP_CHASE,
        NetwDisplayDecl::PUMP_REMOTE
    ));
    CHECK(NetwDisplayDecl::pump_arms_offsets(
        NetwDisplayDecl::PUMP_REMOTE,
        NetwDisplayDecl::PUMP_BRACKETED
    ));

    CHECK_FALSE(NetwDisplayDecl::pump_arms_offsets(
        NetwDisplayDecl::PUMP_UNRESOLVED,
        NetwDisplayDecl::PUMP_REMOTE
    ));
    CHECK_FALSE(NetwDisplayDecl::pump_arms_offsets(
        NetwDisplayDecl::PUMP_DISABLED,
        NetwDisplayDecl::PUMP_REMOTE
    ));
    CHECK_FALSE(NetwDisplayDecl::pump_arms_offsets(
        NetwDisplayDecl::PUMP_REMOTE,
        NetwDisplayDecl::PUMP_DISABLED
    ));
}

} // namespace TestNetwDisplayDecl
