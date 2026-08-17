#include "support/netw_test.h"

#include "netw/predict/engine.hpp"
#include "netw/predict/sensors.hpp"
#include "netw/prediction_core.hpp"

#include "godot/physics_body.hpp"

namespace TestNetwPredictSensorLaws {

using namespace godot;
using namespace netw;
using namespace netw::predict;

WitnessContact contact(
    const char *p_identity,
    int p_class,
    int p_realization,
    bool p_outside = false
) {
    WitnessContact out;
    out.identity = p_identity;
    out.witness_class = p_class;
    out.realization = p_realization;
    out.outside_boundary = p_outside;
    return out;
}

TEST_CASE(
    "[Networked][Predict][Hosted] witness identity and class are comparable "
    "while realization and boundary stay local"
) {
    LocalVector<WitnessContact> forward;
    forward.push_back(
        contact("entity:7", SENSOR_WITNESS_DYNAMIC_ENTITY, 3)
    );
    forward.push_back(contact("path:/ground", SENSOR_WITNESS_SUPPORT, 1));
    LocalVector<WitnessContact> reversed;
    reversed.push_back(
        contact("path:/ground", SENSOR_WITNESS_SUPPORT, 4, true)
    );
    reversed.push_back(
        contact("entity:7", SENSOR_WITNESS_DYNAMIC_ENTITY, 5)
    );

    const WitnessSummary first = summarize_witness(forward, false);
    const WitnessSummary second = summarize_witness(reversed, false);
    REQUIRE(first.valid);
    REQUIRE(second.valid);
    NETW_CHECK_EQ(first.fingerprint, second.fingerprint);
    NETW_CHECK_EQ(
        first.class_bits,
        SENSOR_WITNESS_SUPPORT | SENSOR_WITNESS_DYNAMIC_ENTITY
    );
    CHECK(first.realization_bits != second.realization_bits);
    CHECK(!first.breach);
    CHECK(second.breach);
}

TEST_CASE(
    "[Networked][Predict][Hosted] environment and topology folds ignore "
    "declaration order and expose the quantum"
) {
    Dictionary forward;
    forward[StringName("wind")] = 2.0;
    forward[StringName("ground")] = 1;
    Dictionary reversed;
    reversed[StringName("ground")] = 1;
    reversed[StringName("wind")] = 2.0;
    NETW_CHECK_EQ(
        environment_digest(4, forward),
        environment_digest(4, reversed)
    );
    CHECK(topology_fingerprint(forward, 1) != topology_fingerprint(forward, 2));
}

TEST_CASE(
    "[Networked][Predict][Hosted] carry retires only on a conclusive contract "
    "failure or the bounded infidelity run"
) {
    CarryTrack carry;
    carry.resize(2);
    CarryProbe unfaithful;
    unfaithful.faithful = false;
    for (int at = 0; at < 7; ++at) {
        NETW_CHECK_EQ(
            int(carry.judge(0, int(Schedule::FRAME), unfaithful)),
            int(CarryVerdict::UNFAITHFUL)
        );
        CHECK(!carry.field(0)->retired);
    }
    NETW_CHECK_EQ(
        int(carry.judge(0, int(Schedule::FRAME), unfaithful)),
        int(CarryVerdict::RETIRED_INFIDELITY)
    );
    CHECK(carry.field(0)->retired);
    NETW_CHECK_EQ(carry.field(0)->infidelity, 8);
    NETW_CHECK_EQ(
        int(carry.judge(0, int(Schedule::FRAME), unfaithful)),
        int(CarryVerdict::RETIRED_ALREADY)
    );

    CarryProbe faithful;
    NETW_CHECK_EQ(
        int(carry.judge(1, int(Schedule::FRAME), faithful)),
        int(CarryVerdict::CARRIED)
    );
    NETW_CHECK_EQ(carry.field(1)->carried, 1);
    NETW_CHECK_EQ(
        int(carry.judge(1, int(Schedule::TICK), faithful)),
        int(CarryVerdict::RETIRED_SCHEDULE)
    );
    CHECK(carry.field(1)->retired);
}

TEST_CASE(
    "[Networked][Predict][Hosted] a carry with no recorded transition declines "
    "without convicting the rule"
) {
    CarryTrack carry;
    carry.resize(1);
    for (int at = 0; at < CARRY_INFIDELITY_LIMIT * 2; ++at) {
        NETW_CHECK_EQ(
            int(carry.decline(0, int(Schedule::FRAME))),
            int(CarryVerdict::DECLINED)
        );
    }
    CHECK(!carry.field(0)->retired);
    NETW_CHECK_EQ(carry.field(0)->infidelity, 0);
    NETW_CHECK_EQ(carry.field(0)->declined, CARRY_INFIDELITY_LIMIT * 2);

    NETW_CHECK_EQ(
        int(carry.decline(0, int(Schedule::TICK))),
        int(CarryVerdict::RETIRED_SCHEDULE)
    );
    CHECK(carry.field(0)->retired);
    NETW_CHECK_EQ(carry.field(0)->infidelity, 0);
}

TEST_CASE(
    "[Networked][Predict][Hosted][SceneTree] static geometry is what every "
    "peer holds identically, which a body that moves is not"
) {
    StaticBody2D *wall = memnew(StaticBody2D);
    StaticBody3D *floor_3d = memnew(StaticBody3D);
    TileMapLayer *tiles = memnew(TileMapLayer);
    AnimatableBody2D *platform = memnew(AnimatableBody2D);
    CharacterBody2D *walker = memnew(CharacterBody2D);
    RigidBody2D *crate = memnew(RigidBody2D);

    CHECK(static_geometry(wall));
    CHECK(static_geometry(floor_3d));
    CHECK(static_geometry(tiles));
    // A moving platform IS a StaticBody2D subclass, so the exclusion has to
    // run before the class list is asked.
    CHECK(!static_geometry(platform));
    CHECK(!static_geometry(walker));
    CHECK(!static_geometry(crate));
    CHECK(!static_geometry(nullptr));

    memdelete(wall);
    memdelete(floor_3d);
    memdelete(tiles);
    memdelete(platform);
    memdelete(walker);
    memdelete(crate);
}

TEST_CASE(
    "[Networked][Predict][Hosted][SceneTree] the declared support outranks "
    "what the collider is, and everything unshared is a dynamic entity"
) {
    Ref<NetwPredictionEngine> pool;
    pool.instantiate();
    StaticBody2D *wall = memnew(StaticBody2D);
    AnimatableBody2D *platform = memnew(AnimatableBody2D);

    NETW_CHECK_EQ(pool->witness_class(wall, false), int(SENSOR_WITNESS_STATIC));
    NETW_CHECK_EQ(
        pool->witness_class(wall, true),
        int(SENSOR_WITNESS_SUPPORT)
    );
    NETW_CHECK_EQ(
        pool->witness_class(platform, false),
        int(SENSOR_WITNESS_DYNAMIC_ENTITY)
    );
    NETW_CHECK_EQ(
        pool->witness_class(nullptr, false),
        int(SENSOR_WITNESS_DYNAMIC_ENTITY)
    );

    memdelete(wall);
    memdelete(platform);
}

Ref<NetwPredictDeclaration> carry_field(const char *p_key) {
    Ref<NetwPredictDeclaration> out;
    out.instantiate();
    out->append_field(
        StringName(p_key),
        int(PropertyClass::CAUSAL),
        StringName(),
        0.0,
        false,
        false,
        -1.0,
        -1.0,
        false
    );
    return out;
}

int64_t carrying_slot(const Ref<NetwPredictionEngine> &p_pool, int p_schedule) {
    const int64_t slot = p_pool->open(carry_field("spin"));
    p_pool->configure(
        slot,
        p_schedule,
        int(Role::PREDICT),
        int(CorrectionMode::SNAP),
        int(RestoreMode::EXACT),
        6,
        NetwPredictionEngine::ISLAND_NONE,
        true,
        false
    );
    return slot;
}

TEST_CASE(
    "[Networked][Predict][Hosted] the pool judges a carry by declared field "
    "name and reports its retirement once"
) {
    Ref<NetwPredictionEngine> pool;
    pool.instantiate();
    const int64_t slot = carrying_slot(pool, int(Schedule::FRAME));
    const StringName spin("spin");

    for (int at = 0; at < CARRY_INFIDELITY_LIMIT - 1; ++at) {
        NETW_CHECK_EQ(
            pool->judge_carry(slot, spin, true, true, true, true, false),
            int(NetwPredictionEngine::CARRY_UNFAITHFUL)
        );
        CHECK(!pool->carry_retired(slot, spin));
    }
    NETW_CHECK_EQ(
        pool->judge_carry(slot, spin, true, true, true, true, false),
        int(NetwPredictionEngine::CARRY_RETIRED_INFIDELITY)
    );
    CHECK(pool->carry_retired(slot, spin));
    NETW_CHECK_EQ(
        pool->judge_carry(slot, spin, true, true, true, true, true),
        int(NetwPredictionEngine::CARRY_RETIRED_ALREADY)
    );
    NETW_CHECK_EQ(
        int64_t(pool->carry_stats(slot, spin)[2]),
        int64_t(CARRY_INFIDELITY_LIMIT)
    );
}

TEST_CASE(
    "[Networked][Predict][Hosted] a refusal with no invocation convicts a "
    "carry only of the schedule it was declared on"
) {
    Ref<NetwPredictionEngine> pool;
    pool.instantiate();
    const StringName spin("spin");

    const int64_t framed = carrying_slot(pool, int(Schedule::FRAME));
    NETW_CHECK_EQ(
        pool->decline_carry(framed, spin),
        int(NetwPredictionEngine::CARRY_DECLINED)
    );
    CHECK(!pool->carry_retired(framed, spin));

    const int64_t ticked = carrying_slot(pool, int(Schedule::TICK));
    NETW_CHECK_EQ(
        pool->decline_carry(ticked, spin),
        int(NetwPredictionEngine::CARRY_RETIRED_SCHEDULE)
    );
    CHECK(pool->carry_retired(ticked, spin));
    NETW_CHECK_EQ(int64_t(pool->carry_stats(ticked, spin)[2]), int64_t(0));
}

TEST_CASE(
    "[Networked][Predict][Hosted] a rule the pool has convicted is never "
    "eligible to run again"
) {
    Ref<NetwPredictionEngine> pool;
    pool.instantiate();
    const StringName spin("spin");

    const int64_t framed = carrying_slot(pool, int(Schedule::FRAME));
    CHECK(pool->carry_eligible(framed, spin));
    CHECK(!pool->carry_eligible(framed, StringName("undeclared")));

    for (int at = 0; at < CARRY_INFIDELITY_LIMIT; ++at) {
        pool->judge_carry(framed, spin, true, true, true, true, false);
    }
    CHECK(!pool->carry_eligible(framed, spin));

    CHECK(!pool->carry_eligible(
        carrying_slot(pool, int(Schedule::TICK)),
        spin
    ));
}

TEST_CASE(
    "[Networked][Predict][Hosted] a rewire discards the rules declared "
    "against the field table it replaced"
) {
    Ref<NetwPredictionEngine> pool;
    pool.instantiate();
    const int64_t slot = carrying_slot(pool, int(Schedule::FRAME));
    const StringName spin("spin");
    const StringName charge("charge");

    NETW_CHECK_EQ(
        pool->judge_carry(slot, spin, true, true, true, true, false),
        int(NetwPredictionEngine::CARRY_UNFAITHFUL)
    );

    pool->rewire(slot, carry_field("charge"), Ref<NetwPredictDeclaration>());

    NETW_CHECK_EQ(int64_t(pool->carry_stats(slot, spin)[2]), int64_t(0));
    NETW_CHECK_EQ(
        pool->judge_carry(slot, charge, true, true, true, true, false),
        int(NetwPredictionEngine::CARRY_UNFAITHFUL)
    );
}

TEST_CASE(
    "[Networked][Predict][Hosted] measured quantum and solve evidence share "
    "one journal row"
) {
    Slot slot;
    Wiring wiring;
    wiring.fields.append(StringName("position"));
    wiring.resize(1);
    slot.rewire(wiring);
    slot.config.schedule = int(Schedule::FRAME);
    slot.config.witness = true;

    Timing first;
    first.tick = 1;
    first.frame = 10;
    first.quantum = 1;
    slot.record_input(1, 11);
    const DriveRecord opened = slot.open_drive(first, Dictionary(), StateStamp());
    REQUIRE(opened.ran);
    slot.close_drive(opened.transition, StateStamp());
    slot.acknowledge(opened.transition, true);

    Timing second = first;
    second.tick = 2;
    second.frame = 12;
    slot.record_input(2, 12);
    StateStamp pre;
    pre.evidence_mask = EVIDENCE_RAW;
    const DriveRecord measured = slot.open_drive(second, Dictionary(), pre);
    REQUIRE(measured.ran);
    Dictionary environment;
    environment[StringName("wind")] = 3;
    Dictionary topology;
    topology[StringName("schedule")] = int(Schedule::FRAME);
    LocalVector<WitnessContact> contacts;
    contacts.push_back(
        contact("path:/ground", SENSOR_WITNESS_SUPPORT, 1)
    );
    REQUIRE(slot.record_evidence(
        measured.transition,
        9,
        environment,
        topology,
        contacts,
        false
    ));
    slot.close_drive(measured.transition, StateStamp());

    NETW_CHECK_EQ(slot.stats.quantum_steps, 2);
    NETW_CHECK_EQ(slot.stats.quantum_declared, 1);
    NETW_CHECK_EQ(slot.stats.quantum_faults, 1);
    const JournalEvidence evidence
        = slot.journal.evidence_of(measured.transition);
    NETW_CHECK_EQ(evidence.e_digest, environment_digest(9, environment));
    NETW_CHECK_EQ(evidence.topo_fp, topology_fingerprint(topology, 2));
    NETW_CHECK_EQ(
        evidence.witness_fp,
        summarize_witness(contacts, false).fingerprint
    );
    NETW_CHECK_EQ(
        evidence.evidence_mask,
        EVIDENCE_RAW | EVIDENCE_WITNESS
    );
}

int64_t witnessing_slot(const Ref<NetwPredictionEngine> &p_pool) {
    const int64_t slot = p_pool->open(carry_field("position"));
    p_pool->configure(
        slot,
        int(Schedule::FRAME),
        int(Role::PREDICT),
        int(CorrectionMode::SNAP),
        int(RestoreMode::EXACT),
        6,
        NetwPredictionEngine::ISLAND_NONE,
        false,
        true
    );
    return slot;
}

void record_one(
    const Ref<NetwPredictionEngine> &p_pool,
    int64_t p_slot,
    int64_t p_transition,
    int p_realization,
    bool p_sleeping
) {
    PackedStringArray ids;
    ids.push_back("path:/ground");
    PackedInt32Array classes;
    classes.push_back(SENSOR_WITNESS_SUPPORT);
    PackedInt32Array realizations;
    realizations.push_back(p_realization);
    PackedByteArray outside;
    outside.push_back(0);
    p_pool->record_evidence(
        p_slot,
        p_transition,
        1,
        Dictionary(),
        Dictionary(),
        ids,
        classes,
        realizations,
        outside,
        p_sleeping
    );
    p_pool->mark_witness_match(p_slot, p_transition, true);
}

TEST_CASE(
    "[Networked][Predict][Hosted] a clean witness is awake through its solve "
    "and touched only what the closure reproduces"
) {
    struct Row {
        const char *name;
        int realization;
        bool sleeping;
        bool woke;
        bool clean;
    };
    const Row rows[] = {
        {"the declared support", int(ContactClass::DECLARED_SUPPORT), false,
         false, true},
        {"other static geometry", int(ContactClass::OTHER_STATIC), false,
         false, true},
        {"no contact at all", int(ContactClass::NONE), false, false, true},
        {"a predicted body", int(ContactClass::PREDICTED_DYNAMIC), false,
         false, false},
        {"an unpredicted body", int(ContactClass::UNPREDICTED_DYNAMIC), false,
         false, false},
        {"a kinematic proxy", int(ContactClass::KINEMATIC_PROXY), false,
         false, false},
        {"a sleeping solve", int(ContactClass::OTHER_STATIC), true, false,
         false},
        {"a solve it woke into", int(ContactClass::OTHER_STATIC), false, true,
         false},
    };
    for (const Row &row : rows) {
        NETW_FORMAT_TEXT(row_text, row.name);
        CAPTURE(row_text);
        Ref<NetwPredictionEngine> pool;
        pool.instantiate();
        const int64_t slot = witnessing_slot(pool);
        int64_t tick = 1;
        if (row.woke) {
            // The rest state that makes the NEXT solve one the solver started
            // rather than continued.
            pool->record_input(slot, tick, int32_t(tick));
            const Ref<NetwPredictDrive> asleep = pool->open_drive(
                slot, Dictionary(), tick, tick, 1.0 / 60.0, 1, true, 0, 0, 0, 0
            );
            REQUIRE(asleep.is_valid());
            record_one(pool, slot, asleep->transition(), row.realization, true);
            pool->close_drive(slot, asleep->transition(), 0, 0, 0, 0);
            tick += 1;
        }
        pool->record_input(slot, tick, int32_t(tick));
        const Ref<NetwPredictDrive> drive = pool->open_drive(
            slot, Dictionary(), tick, tick, 1.0 / 60.0, 1, true, 0, 0, 0, 0
        );
        REQUIRE(drive.is_valid());
        record_one(
            pool,
            slot,
            drive->transition(),
            row.realization,
            row.sleeping
        );
        CHECK(pool->witness_row_clean(slot, drive->transition(), true)
              == row.clean);
    }
}

TEST_CASE(
    "[Networked][Predict][Hosted] an unwitnessed transition is never clean, "
    "and a peer's disagreement withdraws a local one"
) {
    Ref<NetwPredictionEngine> pool;
    pool.instantiate();
    const int64_t slot = pool->open(carry_field("position"));
    pool->configure(
        slot,
        int(Schedule::FRAME),
        int(Role::PREDICT),
        int(CorrectionMode::SNAP),
        int(RestoreMode::EXACT),
        6,
        NetwPredictionEngine::ISLAND_NONE,
        false,
        true
    );

    pool->record_input(slot, 1, 11);
    const Ref<NetwPredictDrive> drive
        = pool->open_drive(slot, Dictionary(), 1, 10, 1.0 / 60.0, 1, true, 0, 0, 0, 0);
    REQUIRE(drive.is_valid());
    const int64_t transition = drive->transition();

    // No evidence recorded at all, so the row carries no witness mask.
    CHECK(!pool->witness_row_clean(slot, transition, false));

    PackedStringArray ids;
    ids.push_back("path:/ground");
    PackedInt32Array classes;
    classes.push_back(SENSOR_WITNESS_STATIC);
    PackedInt32Array realizations;
    realizations.push_back(int(ContactClass::OTHER_STATIC));
    PackedByteArray outside;
    outside.push_back(0);
    REQUIRE(pool->record_evidence(
        slot,
        transition,
        1,
        Dictionary(),
        Dictionary(),
        ids,
        classes,
        realizations,
        outside,
        false
    ));

    CHECK(pool->witness_row_clean(slot, transition, false));
    // Authority has not agreed yet, so the row is a LOCAL fact only.
    CHECK(!pool->witness_row_clean(slot, transition, true));
    pool->mark_witness_match(slot, transition, true);
    CHECK(pool->witness_row_clean(slot, transition, true));
}

TEST_CASE(
    "[Networked][Predict][Hosted] a sensor and carry declaration narrows no "
    "schedule and no island"
) {
    Ref<NetwPredictionEngine> pool;
    pool.instantiate();
    CHECK(pool->supports(
        int(Schedule::STEPPED),
        int(Role::PREDICT),
        int(CorrectionMode::SNAP),
        int(RestoreMode::EXACT),
        NetwPredictionEngine::ISLAND_DECLARED,
        true,
        true
    ));
    CHECK(pool->supports(
        int(Schedule::STEPPED),
        int(Role::PREDICT),
        int(CorrectionMode::SNAP),
        int(RestoreMode::EXACT),
        NetwPredictionEngine::ISLAND_JOINT,
        true,
        true
    ));
    CHECK(!pool->supports(
        int(Schedule::FRAME),
        int(Role::PREDICT),
        int(CorrectionMode::SNAP),
        int(RestoreMode::EXACT),
        NetwPredictionEngine::ISLAND_JOINT,
        true,
        true
    ));
}

TEST_CASE(
    "[Networked][Predict][Hosted] a witness verdict that never arrived is not "
    "a verdict of differs"
) {
    Ref<NetwPredictionEngine> pool;
    pool.instantiate();
    const int64_t slot = witnessing_slot(pool);
    pool->record_input(slot, 1, 1);
    const Ref<NetwPredictDrive> drive = pool->open_drive(
        slot, Dictionary(), 1, 1, 1.0 / 60.0, 1, true, 0, 0, 0, 0
    );
    REQUIRE(drive.is_valid());
    const int64_t at = drive->transition();
    pool->close_drive(slot, at, 0, 0, 0, 0);

    // A conditional operator waits on "has authority answered", and both
    // answers are answers. Reading the matched flag alone cannot tell an
    // unanswered row from one authority disagreed about, so the ladder would
    // stop waiting the moment the row existed.
    CHECK_FALSE(pool->witness_judged(slot, at));
    CHECK_FALSE(pool->witness_row_clean(slot, at, true));

    pool->mark_witness_match(slot, at, false);
    CHECK(pool->witness_judged(slot, at));
    CHECK_FALSE(pool->witness_row_clean(slot, at, true));

    pool->mark_witness_match(slot, at, true);
    CHECK(pool->witness_judged(slot, at));

    // A transition the journal never held is unanswered, not answered false.
    CHECK_FALSE(pool->witness_judged(slot, 900));
    CHECK_FALSE(pool->witness_judged(slot + 9000, at));
}

TEST_CASE(
    "[Networked][Predict][Hosted] a recorded state that is not a drive result "
    "bars the two transitions it bounds from judging a rule"
) {
    Ref<NetwPredictionEngine> pool;
    pool.instantiate();
    const int64_t slot = carrying_slot(pool, int(Schedule::FRAME));
    // A rule is never invoked from here. What the book needs is a slot that
    // declares one, and any valid callable declares it.
    pool->set_carry(
        slot,
        StringName("spin"),
        Callable(pool.ptr(), "carry_retired")
    );

    CHECK(pool->carry_judgeable(slot, 4));

    pool->mark_carry_dirty(slot, 5);
    // A rule is judged from one recorded state to the next, so the mark bars
    // the transition ENDING at it as well as the one starting there.
    CHECK_FALSE(pool->carry_judgeable(slot, 4));
    CHECK_FALSE(pool->carry_judgeable(slot, 5));
    CHECK(pool->carry_judgeable(slot, 6));

    for (int at = 0; at < TAPE_HISTORY_LIMIT; ++at) {
        pool->mark_carry_dirty(slot, 100 + at);
    }
    CHECK(pool->carry_judgeable(slot, 5));

    pool->mark_carry_dirty(slot, 500);
    CHECK_FALSE(pool->carry_judgeable(slot, 500));
    pool->rewire(slot, carry_field("spin"), carry_field("throttle"));
    CHECK(pool->carry_judgeable(slot, 500));

    CHECK_FALSE(pool->carry_judgeable(slot + 9000, 1));
}

TEST_CASE(
    "[Networked][Predict][Hosted] a slot declaring no rule records no mark"
) {
    Ref<NetwPredictionEngine> pool;
    pool.instantiate();
    const int64_t slot = carrying_slot(pool, int(Schedule::FRAME));

    pool->mark_carry_dirty(slot, 5);
    CHECK(pool->carry_judgeable(slot, 5));
}

TEST_CASE("[Networked][Predict][Hosted] the bound topology fingerprint is the "
          "core's, folding the quantum in and nothing else") {
    Dictionary facts;
    facts[StringName("floor")] = true;

    Dictionary folded = facts.duplicate();
    folded[StringName("quantum")] = 7;

    NETW_CHECK_EQ(
        netw::NetwPredictionCore::topology_fingerprint(facts, 7),
        netw::NetwPredictionCore::fact_fingerprint(folded)
    );
    NETW_CHECK_EQ(
        netw::NetwPredictionCore::topology_fingerprint(facts, 7),
        int64_t(netw::predict::topology_fingerprint(facts, 7))
    );
}

TEST_CASE("[Networked][Predict][Hosted] a different quantum is a different "
          "topology, which is the whole reason it is folded in") {
    Dictionary facts;
    facts[StringName("floor")] = true;

    NETW_CHECK_ORDER(
        netw::NetwPredictionCore::topology_fingerprint(facts, 7),
        netw::NetwPredictionCore::topology_fingerprint(facts, 8),
        !=
    );
}

} // namespace TestNetwPredictSensorLaws
