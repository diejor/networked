#include "support/netw_test.h"

#include <cstdint>

#include "godot/variant.hpp"
#include "netw/api/quantize.hpp"
#include "netw/predict/engine.hpp"
#include "netw/predict/tick_work.hpp"
#include "netw/predict/transition_book.hpp"
#include "netw/predict/wiring.hpp"
#include "netw/store/validate.hpp"

#if defined(NETW_TIER_HOSTED)

#include "support/alloc_probe.h"
#include "support/carrier.h"
#include "support/loopback_rig.h"

using namespace godot;

namespace TestNetwPredictStateBook {

using netw::predict::FieldDecl;
using netw::predict::StateRow;
using netw::predict::TransitionBook;
using netw::predict::Wiring;

constexpr int64_t WARM = 8;
constexpr int64_t STEADY = 300;

Wiring pose_wiring() {
    Ref<netw::NetwQuantizeScalar> grid;
    grid.instantiate();
    grid->set_bit_count(12);
    grid->set_min_limit(-8.0);
    grid->set_max_limit(8.0);
    LocalVector<FieldDecl> declared;
    FieldDecl where;
    where.key = StringName("where");
    where.type = int(Variant::VECTOR3);
    where.quantizer = grid;
    declared.push_back(where);
    FieldDecl speed;
    speed.key = StringName("speed");
    speed.type = int(Variant::FLOAT);
    declared.push_back(speed);
    FieldDecl grounded;
    grounded.key = StringName("grounded");
    grounded.type = int(Variant::BOOL);
    declared.push_back(grounded);
    return netw::predict::compile(declared);
}

TEST_CASE(
    "[Networked][Predict] PB1 a steady predicted tick allocates nothing in "
    "the predicted book's record and read"
) {
    const Wiring wiring = pose_wiring();
    TransitionBook book;
    book.reset(wiring.codec);
    Dictionary payload;
    payload[StringName("where")] = Vector3(1.5, -2.25, 0.5);
    payload[StringName("speed")] = 0.1;
    payload[StringName("grounded")] = true;
    StateRow written;
    netw::predict::read_payload(wiring, payload, written);
    StateRow row;
    for (int64_t key = 1; key <= WARM; ++key) {
        REQUIRE(book.record_row(wiring.codec, key, key, true, written));
        REQUIRE(book.read_row(wiring.codec, key, row));
    }

    int64_t recorded = 0;
    int64_t read = 0;
    const int64_t opened = netw::store::allocations_seen();
    netw_test::AllocProbe probe;
    probe.arm();
    for (int64_t key = WARM + 1; key <= STEADY; ++key) {
        recorded
            += book.record_row(wiring.codec, key, key, true, written) ? 1 : 0;
        read += book.read_row(wiring.codec, key, row) ? 1 : 0;
    }
    probe.disarm();

    NETW_CHECK_EQ(recorded, STEADY - WARM);
    NETW_CHECK_EQ(read, STEADY - WARM);
    NETW_CHECK_EQ(netw::store::allocations_seen() - opened, int64_t(0));
    REQUIRE(row.has(1));
    NETW_CHECK_CLOSE(double(row.values[1]), 0.1, 0.0);
    CHECK(bool(row.values[2]));
}

LocalVector<FieldDecl> float_fields(const char *p_first, const char *p_second) {
    LocalVector<FieldDecl> out;
    const char *keys[] = { p_first, p_second };
    for (const char *key : keys) {
        if (key == nullptr) {
            continue;
        }
        FieldDecl field;
        field.key = StringName(key);
        field.type = int(Variant::FLOAT);
        out.push_back(field);
    }
    return out;
}

TEST_CASE(
    "[Networked][Predict] PB3 a steady predicted tick allocates nothing in "
    "capture, restore, the input book, fingerprints and the comparison"
) {
    netw::NetwPredictionEngine held;
    netw::NetwPredictionEngine *const engine = &held;
    netw_test::Carrier *owner = memnew(netw_test::Carrier);
    owner->define("speed", 1.5);
    owner->define("latch", 7.0);
    owner->define("throttle", 0.25);
    const LocalVector<FieldDecl> state = float_fields("speed", "latch");
    const LocalVector<FieldDecl> input = float_fields("throttle", nullptr);
    const Wiring states = netw::predict::compile(state);
    const Wiring commands = netw::predict::compile(input);
    const int64_t slot = engine->open(state);
    engine->rewire(slot, state, input);
    REQUIRE(engine->bind_owner(slot, owner));
    engine->open_lane(slot);
    StateRow command;
    command.resize(1);
    command.set(0, 0.25);
    StateRow read;
    StateRow predicted;

    int64_t moved = 0;
    int64_t agreed = 0;
    const auto tick = [&](int64_t p_key) {
        moved += engine->capture_predicted(slot, p_key) ? 1 : 0;
        moved += engine->restore_predicted(slot, p_key) ? 1 : 0;
        const bool authored = engine->record_authored_command(
            slot,
            p_key,
            p_key,
            true,
            command
        );
        moved += authored ? 1 : 0;
        const TransitionBook *book = engine->input_book(slot);
        moved += book->read_row(commands.codec, p_key, read) ? 1 : 0;

        const TransitionBook *sealed = engine->predicted_book(slot);
        moved += sealed->read_row(states.codec, p_key, predicted) ? 1 : 0;
        netw::predict::FamilyFingerprints live;
        const int32_t pre = engine->live_fingerprint(slot, &live);
        const int32_t post = engine->book_fingerprint(slot, p_key);
        const netw::predict::FamilyFingerprints families
            = engine->book_families(slot, p_key);
        agreed += pre == post && live.controller == families.controller ? 1
                                                                         : 0;
        const netw::predict::Judged judged = engine->judge_state(
            slot,
            p_key - 1,
            int64_t(netw::predict::Domain::IN_DOMAIN),
            0,
            predicted,
            predicted,
            Callable()
        );
        agreed += judged.corrected ? 0 : 1;
    };
    for (int64_t key = 1; key <= WARM; ++key) {
        tick(key);
    }

    moved = 0;
    agreed = 0;
    const int64_t built = netw::predict::script_dictionaries_built();
    const int64_t opened = netw::store::allocations_seen();
    netw_test::AllocProbe probe;
    probe.arm();
    for (int64_t key = WARM + 1; key <= STEADY; ++key) {
        tick(key);
    }
    probe.disarm();

    NETW_CHECK_EQ(moved, 5 * (STEADY - WARM));
    NETW_CHECK_EQ(agreed, 2 * (STEADY - WARM));
    NETW_CHECK_EQ(netw::predict::script_dictionaries_built() - built, 0);
    NETW_CHECK_EQ(netw::store::allocations_seen() - opened, int64_t(0));
    REQUIRE(read.has(0));
    NETW_CHECK_CLOSE(double(read.values[0]), 0.25, 0.0);
    memdelete(owner);
}

struct SteadyTick {
    int64_t scopes = 0;
    int64_t allocations = 0;
    int64_t dictionary_writes = 0;
    int64_t script_dictionaries = 0;
};

SteadyTick steady_session(netw::Schedule p_schedule) {
    netw_test::LoopbackRig rig(1);
    rig.declare_world(
        netw_test::WorldDecl().clocked(30, 3).lag_compensated().player(
            netw_test::EntityDecl()
                .named("P")
                .on_schema("SteadyPose")
                .synced("position")
                .placed_at(Vector2())
                .predicted(0.01)
                .scheduled(p_schedule),
            0
        )
    );
    Node *owner = rig.node_of(rig.entity_of("P", 0), 0);
    REQUIRE(owner != nullptr);
    owner->set("motion", Vector2(1.0, 0.0));
    const auto step = [&]() {
        if (p_schedule == netw::Schedule::FRAME) {
            rig.step_frame(1);
        } else {
            rig.step_ticks(1);
        }
    };
    for (int64_t at = 0; at < 40; ++at) {
        step();
    }
    const netw::predict::TickWork opened = netw::predict::tick_work();
    const int64_t built = netw::predict::script_dictionaries_built();
    netw_test::AllocProbe probe;
    probe.arm();
    for (int64_t at = 0; at < 60; ++at) {
        step();
    }
    probe.disarm();
    const netw::predict::TickWork &closed = netw::predict::tick_work();
    SteadyTick out;
    out.scopes = closed.scopes - opened.scopes;
    out.allocations = closed.allocations - opened.allocations;
    out.dictionary_writes = closed.dictionary_writes
        - opened.dictionary_writes;
    out.script_dictionaries = netw::predict::script_dictionaries_built()
        - built;
    return out;
}

TEST_CASE(
    "[Networked][Predict] PB4 a steady predicted session tick, arrival "
    "included, allocates nothing and builds no Dictionary"
) {
    const netw::Schedule schedules[]
        = { netw::Schedule::TICK, netw::Schedule::FRAME };
    for (const netw::Schedule schedule : schedules) {
        const SteadyTick steady = steady_session(schedule);
        NETW_CHECK_GT(steady.scopes, int64_t(60));
        NETW_CHECK_EQ(steady.allocations, int64_t(0));
        NETW_CHECK_EQ(steady.dictionary_writes, int64_t(0));
        NETW_CHECK_EQ(steady.script_dictionaries, int64_t(0));
    }
}

netw_test::EntityDecl recovering_body(const char *p_name) {
    return netw_test::EntityDecl()
        .named(p_name)
        .on_schema("RecoveringPose")
        .synced("position")
        .synced("lean")
        .projected_by("lean")
        .placed_at(Vector2())
        .predicted(0.01)
        .scheduled(netw::Schedule::FRAME)
        .recovered_by(netw::RecoveryPolicy::REBASE_RECOVER);
}

void nudge_authority(netw_test::LoopbackRig &p_rig, const StringName &p_name) {
    Node2D *authority = Object::cast_to<Node2D>(
        p_rig.node_of(p_rig.entity_of(p_name))
    );
    REQUIRE(authority != nullptr);
    authority->set_position(authority->get_position() + Vector2(0.0, 0.05));
    authority->set(
        "lean",
        Vector2(authority->get("lean")) + Vector2(0.02, -0.01)
    );
}

constexpr int64_t CORRECTION_WARM = 40;

struct SteadyCorrection {
    SteadyTick tick;
    int64_t corrections = 0;
};

SteadyCorrection steady_correction() {
    netw_test::LoopbackRig rig(1);
    Vector<StringName> members;
    members.push_back(StringName("B"));
    rig.declare_world(
        netw_test::WorldDecl()
            .clocked(30, 3)
            .lag_compensated()
            .player(recovering_body("P"), 0)
            .hosted(recovering_body("B"))
            .island(
                StringName("P"),
                members,
                netw_test::WorldDecl::INDEPENDENT
            )
            .simulating(StringName("B"))
    );
    Node *owner = rig.node_of(rig.entity_of("P", 0), 0);
    REQUIRE(owner != nullptr);
    owner->set("motion", Vector2(1.0, 0.0));
    netw::NetwPrediction *handle = rig.prediction_handle("P", 0);
    REQUIRE(handle != nullptr);
    const auto step = [&]() {
        nudge_authority(rig, "P");
        nudge_authority(rig, "B");
        rig.step_frame(1);
    };
    for (int64_t at = 0; at < CORRECTION_WARM; ++at) {
        step();
    }
    const int64_t corrected = handle->get_stats()->get_int_fact(
        netw::NetwPredictStats::FACT_CORRECTIONS
    );
    const netw::predict::TickWork opened = netw::predict::tick_work();
    const int64_t built = netw::predict::script_dictionaries_built();
    netw_test::AllocProbe probe;
    probe.arm();
    for (int64_t at = 0; at < 60; ++at) {
        step();
    }
    probe.disarm();
    const netw::predict::TickWork &closed = netw::predict::tick_work();
    SteadyCorrection out;
    out.tick.scopes = closed.scopes - opened.scopes;
    out.tick.allocations = closed.allocations - opened.allocations;
    out.tick.dictionary_writes = closed.dictionary_writes
        - opened.dictionary_writes;
    out.tick.script_dictionaries = netw::predict::script_dictionaries_built()
        - built;
    out.corrections = handle->get_stats()->get_int_fact(
                          netw::NetwPredictStats::FACT_CORRECTIONS
                      )
        - corrected;
    return out;
}

TEST_CASE(
    "[Networked][Predict] PB5 a steady frame correction every arrival, a "
    "recover and a simulated body included, allocates nothing and builds no "
    "Dictionary"
) {
    const SteadyCorrection steady = steady_correction();
    NETW_CHECK_GE(steady.corrections, int64_t(50));
    NETW_CHECK_GT(steady.tick.scopes, int64_t(60));
    NETW_CHECK_EQ(steady.tick.allocations, int64_t(0));
    NETW_CHECK_EQ(steady.tick.dictionary_writes, int64_t(0));
    NETW_CHECK_EQ(steady.tick.script_dictionaries, int64_t(0));
}

TEST_CASE(
    "[Networked][Predict] PB6 a basis restore pushes the whole book entry "
    "the correction merged into, so a field the correction did not carry "
    "returns to its recorded value and the live value is not kept"
) {
    netw::NetwPredictionEngine held;
    netw::NetwPredictionEngine *const engine = &held;
    netw_test::Carrier *owner = memnew(netw_test::Carrier);
    owner->define("speed", 1.5);
    owner->define("latch", 7.0);
    const LocalVector<FieldDecl> state = float_fields("speed", "latch");
    const int64_t slot = engine->open(state);
    engine->rewire(slot, state, LocalVector<FieldDecl>());
    REQUIRE(engine->bind_owner(slot, owner));
    engine->open_lane(slot);
    REQUIRE(engine->capture_predicted(slot, 5));

    owner->set(StringName("speed"), 9.0);
    owner->set(StringName("latch"), 9.0);
    Dictionary correction;
    correction[StringName("speed")] = 3.0;
    REQUIRE(engine->seat_restore(slot, 5, correction));
    CHECK(engine->apply_restore(
        slot,
        correction,
        int(netw::predict::Operator::REBASE_EXACT),
        4,
        -1,
        StringName(),
        1,
        0.01,
        false,
        false,
        5
    ));

    NETW_CHECK_CLOSE(double(owner->get(StringName("speed"))), 3.0, 0.0);
    NETW_CHECK_CLOSE(double(owner->get(StringName("latch"))), 7.0, 0.0);
    memdelete(owner);
}

} // namespace TestNetwPredictStateBook

#endif
