#include "support/netw_test.h"

#include <cmath>
#include <cstddef>
#include <cstdint>

#include "godot/variant.hpp"
#include "netw/api/prediction.hpp"

#if defined(NETW_TIER_HOSTED)

#include "support/carrier.h"
#include "support/loopback_rig.h"

using namespace godot;

namespace TestNetwPredictCorrectionOutcome {

using netw_test::EntityDecl;
using netw_test::LoopbackRig;
using netw_test::WorldDecl;

constexpr int WARM = 12;
constexpr int RECORDED = 24;

EntityDecl leaning_player(
    const char *p_name,
    netw::Schedule p_schedule,
    netw::RecoveryPolicy p_recovery
) {
    return EntityDecl()
        .named(p_name)
        .on_schema("LeaningPose")
        .synced("position")
        .synced("lean")
        .placed_at(Vector2())
        .predicted(0.01)
        .scheduled(p_schedule)
        .recovered_by(p_recovery);
}

struct Outcome {
    LocalVector<Vector2> written;
    int64_t corrections = 0;
};

void nudge(LoopbackRig &p_rig, const StringName &p_name) {
    Node2D *authority
        = Object::cast_to<Node2D>(p_rig.node_of(p_rig.entity_of(p_name)));
    REQUIRE(authority != nullptr);
    authority->set_position(authority->get_position() + Vector2(0.0, 0.05));
    authority->set(
        "lean",
        Vector2(authority->get("lean")) + Vector2(0.02, -0.01)
    );
}

void note(
    LoopbackRig &p_rig,
    const StringName &p_name,
    int p_client,
    Outcome &r_out
) {
    Node2D *owner = Object::cast_to<Node2D>(
        p_rig.node_of(p_rig.entity_of(p_name, p_client), p_client)
    );
    REQUIRE(owner != nullptr);
    r_out.written.push_back(owner->get_position());
    r_out.written.push_back(Vector2(owner->get("lean")));
}

int64_t corrections_of(LoopbackRig &p_rig, const StringName &p_name, int p_at) {
    netw::NetwPrediction *handle = p_rig.prediction_handle(p_name, p_at);
    REQUIRE(handle != nullptr);
    return handle->get_stats()->get_int_fact(
        netw::NetwPredictStats::FACT_CORRECTIONS
    );
}

Outcome single_outcome(
    netw::Schedule p_schedule,
    netw::RecoveryPolicy p_by,
    bool p_projected = false
) {
    LoopbackRig rig(1);
    EntityDecl decl = leaning_player("P", p_schedule, p_by);
    if (p_projected) {
        decl.projected_by("lean");
    }
    rig.declare_world(
        WorldDecl().clocked(30, 3).lag_compensated().player(decl, 0)
    );
    Node *owner = rig.node_of(rig.entity_of("P", 0), 0);
    REQUIRE(owner != nullptr);
    owner->set("motion", Vector2(1.0, 0.0));
    Outcome out;
    for (int at = 0; at < WARM + RECORDED; ++at) {
        nudge(rig, "P");
        if (p_schedule == netw::Schedule::FRAME) {
            rig.step_frame(1);
        } else {
            rig.step_ticks(1);
        }
        if (at >= WARM) {
            note(rig, "P", 0, out);
        }
    }
    out.corrections = corrections_of(rig, "P", 0);
    return out;
}

Outcome joint_outcome() {
    LoopbackRig rig(2);
    Vector<StringName> members;
    members.push_back(StringName("Q"));
    rig.declare_world(
        WorldDecl()
            .clocked(30, 3)
            .lag_compensated()
            .player(
                leaning_player(
                    "P",
                    netw::Schedule::TICK,
                    netw::RecoveryPolicy::AUTO
                ),
                0
            )
            .player(
                leaning_player(
                    "Q",
                    netw::Schedule::TICK,
                    netw::RecoveryPolicy::AUTO
                ),
                1
            )
            .island(StringName("P"), members)
            .simulating(StringName("Q"))
    );
    rig.node_of(rig.entity_of("P", 0), 0)->set("motion", Vector2(1.0, 0.0));
    rig.node_of(rig.entity_of("Q", 1), 1)->set("motion", Vector2(0.0, 1.0));
    Outcome out;
    for (int at = 0; at < WARM + RECORDED; ++at) {
        nudge(rig, "P");
        nudge(rig, "Q");
        rig.step_ticks(1);
        if (at >= WARM) {
            note(rig, "P", 0, out);
            note(rig, "Q", 0, out);
        }
    }
    out.corrections = corrections_of(rig, "P", 0);
    return out;
}

const double TICK_REPLAY[] = {
    26,           0.650000095,  0.259999961,  -0.12999998,  28,
    0.700000107,  0.279999971,  -0.139999986, 30,           0.750000119,
    0.299999982,  -0.149999991, 32,           0.800000131,  0.319999993,
    -0.159999996, 34,           0.850000143,  0.340000004,  -0.170000002,
    36,           0.900000155,  0.360000014,  -0.180000007, 38,
    0.950000167,  0.380000025,  -0.190000013, 40,           1.00000012,
    0.400000036,  -0.200000018, 42,           1.05000007,   0.420000046,
    -0.210000023, 44,           1.10000002,   0.440000057,  -0.220000029,
    46,           1.14999998,   0.460000068,  -0.230000034, 48,
    1.19999993,   0.480000079,  -0.240000039, 50,           1.24999988,
    0.50000006,   -0.25000003,  52,           1.29999983,   0.520000041,
    -0.26000002,  54,           1.34999979,   0.540000021,  -0.270000011,
    56,           1.39999974,   0.560000002,  -0.280000001, 58,
    1.44999969,   0.579999983,  -0.289999992, 60,           1.49999964,
    0.599999964,  -0.299999982, 62,           1.5499996,    0.619999945,
    -0.309999973, 64,           1.59999955,   0.639999926,  -0.319999963,
    66,           1.6499995,    0.659999907,  -0.329999954, 68,
    1.69999945,   0.679999888,  -0.339999944, 70,           1.7499994,
    0.699999869,  -0.349999934, 72,           1.79999936,   0.71999985,
    -0.359999925,
};

const double FRAME_RECOVER[] = {
    24,           0.650000095,  0.259999961,  -0.12999998,  26,
    0.700000107,  0.279999971,  -0.139999986, 28,           0.750000119,
    0.299999982,  -0.149999991, 30,           0.800000131,  0.319999993,
    -0.159999996, 32,           0.850000143,  0.340000004,  -0.170000002,
    34,           0.900000155,  0.360000014,  -0.180000007, 36,
    0.950000167,  0.380000025,  -0.190000013, 38,           1.00000012,
    0.400000036,  -0.200000018, 40,           1.05000007,   0.420000046,
    -0.210000023, 42,           1.10000002,   0.440000057,  -0.220000029,
    44,           1.14999998,   0.460000068,  -0.230000034, 46,
    1.19999993,   0.480000079,  -0.240000039, 48,           1.24999988,
    0.50000006,   -0.25000003,  50,           1.29999983,   0.520000041,
    -0.26000002,  52,           1.34999979,   0.540000021,  -0.270000011,
    54,           1.39999974,   0.560000002,  -0.280000001, 56,
    1.44999969,   0.579999983,  -0.289999992, 58,           1.49999964,
    0.599999964,  -0.299999982, 60,           1.5499996,    0.619999945,
    -0.309999973, 62,           1.59999955,   0.639999926,  -0.319999963,
    64,           1.6499995,    0.659999907,  -0.329999954, 66,
    1.69999945,   0.679999888,  -0.339999944, 68,           1.7499994,
    0.699999869,  -0.349999934, 70,           1.79999936,   0.71999985,
    -0.359999925,
};

const double JOINT_PASS[] = {
    26, 0.600000083, 0.239999965, -0.119999982,
    0,  22.5999985,  0.239999965, -0.119999982,
    28, 0.650000095, 0.259999961, -0.12999998,
    0,  24.6499977,  0.259999961, -0.12999998,
    30, 0.700000107, 0.279999971, -0.139999986,
    0,  26.6999969,  0.279999971, -0.139999986,
    32, 0.750000119, 0.299999982, -0.149999991,
    0,  28.7499962,  0.299999982, -0.149999991,
    34, 0.800000131, 0.319999993, -0.159999996,
    0,  30.7999954,  0.319999993, -0.159999996,
    36, 0.850000143, 0.340000004, -0.170000002,
    0,  32.8499947,  0.340000004, -0.170000002,
    38, 0.900000155, 0.360000014, -0.180000007,
    0,  34.8999939,  0.360000014, -0.180000007,
    40, 0.950000167, 0.380000025, -0.190000013,
    0,  36.9499931,  0.380000025, -0.190000013,
    42, 1.00000012,  0.400000036, -0.200000018,
    0,  38.9999924,  0.400000036, -0.200000018,
    44, 1.05000007,  0.420000046, -0.210000023,
    0,  41.0499916,  0.420000046, -0.210000023,
    46, 1.10000002,  0.440000057, -0.220000029,
    0,  43.0999908,  0.440000057, -0.220000029,
    48, 1.14999998,  0.460000068, -0.230000034,
    0,  45.1499901,  0.460000068, -0.230000034,
    50, 1.19999993,  0.480000079, -0.240000039,
    0,  47.1999893,  0.480000079, -0.240000039,
    52, 1.24999988,  0.50000006,  -0.25000003,
    0,  49.2499886,  0.50000006,  -0.25000003,
    54, 1.29999983,  0.520000041, -0.26000002,
    0,  51.2999878,  0.520000041, -0.26000002,
    56, 1.34999979,  0.540000021, -0.270000011,
    0,  53.349987,   0.540000021, -0.270000011,
    58, 1.39999974,  0.560000002, -0.280000001,
    0,  55.3999863,  0.560000002, -0.280000001,
    60, 1.44999969,  0.579999983, -0.289999992,
    0,  57.4499855,  0.579999983, -0.289999992,
    62, 1.49999964,  0.599999964, -0.299999982,
    0,  59.4999847,  0.599999964, -0.299999982,
    64, 1.5499996,   0.619999945, -0.309999973,
    0,  61.549984,   0.619999945, -0.309999973,
    66, 1.59999955,  0.639999926, -0.319999963,
    0,  63.5999832,  0.639999926, -0.319999963,
    68, 1.6499995,   0.659999907, -0.329999954,
    0,  65.6499786,  0.659999907, -0.329999954,
    70, 1.69999945,  0.679999888, -0.339999944,
    0,  67.6999817,  0.679999888, -0.339999944,
    72, 1.7499994,   0.699999869, -0.349999934,
    0,  69.7499847,  0.699999869, -0.349999934,
};

constexpr double CLOSE = 1.0e-5;

template <size_t N>
void check_golden(const Outcome &p_out, const double (&p_golden)[N]) {
    NETW_CHECK_EQ(int64_t(p_out.written.size() * 2), int64_t(N));
    int64_t first_off = -1;
    double worst = 0.0;
    for (uint32_t at = 0; at < p_out.written.size() && at * 2 + 1 < N; ++at) {
        const double dx = std::fabs(p_out.written[at].x - p_golden[at * 2]);
        const double dy = std::fabs(p_out.written[at].y - p_golden[at * 2 + 1]);
        const double off = dx > dy ? dx : dy;
        if (off > CLOSE && first_off < 0) {
            first_off = int64_t(at);
        }
        worst = off > worst ? off : worst;
    }
    NETW_CHECK_EQ(first_off, int64_t(-1));
    NETW_CHECK_CLOSE(worst, 0.0, CLOSE);
}

TEST_CASE(
    "[Networked][Predict] OC1 a tick replay correction writes the node the "
    "values it wrote before, at the same ticks"
) {
    const Outcome out = single_outcome(
        netw::Schedule::TICK,
        netw::RecoveryPolicy::REBASE_REPLAY
    );
    NETW_CHECK_EQ(out.corrections, int64_t(34));
    check_golden(out, TICK_REPLAY);
}

TEST_CASE(
    "[Networked][Predict] OC2 a frame recover correction writes the node the "
    "values it wrote before, at the same ticks"
) {
    const Outcome out = single_outcome(
        netw::Schedule::FRAME,
        netw::RecoveryPolicy::REBASE_RECOVER
    );
    NETW_CHECK_EQ(out.corrections, int64_t(34));
    check_golden(out, FRAME_RECOVER);
}

TEST_CASE(
    "[Networked][Predict] OC4 a frame recover correction over a projected "
    "pose writes the node the values it wrote before, at the same ticks"
) {
    const Outcome out = single_outcome(
        netw::Schedule::FRAME,
        netw::RecoveryPolicy::REBASE_RECOVER,
        true
    );
    NETW_CHECK_EQ(out.corrections, int64_t(34));
    check_golden(out, FRAME_RECOVER);
}

TEST_CASE(
    "[Networked][Predict] OC3 a joint pass writes the island's nodes the "
    "values it wrote before, at the same ticks"
) {
    const Outcome out = joint_outcome();
    NETW_CHECK_EQ(out.corrections, int64_t(0));
    check_golden(out, JOINT_PASS);
}

} // namespace TestNetwPredictCorrectionOutcome

#endif
