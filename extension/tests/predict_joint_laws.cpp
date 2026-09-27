#include "support/netw_test.h"

#include "godot/spatial_node.hpp"
#include "netw/api/prediction_handle.hpp"
#include "netw/api/property_set.hpp"
#include "netw/api/property_set_binding.hpp"
#include "netw/api/schema_core.hpp"
#include "netw/predict/engine.hpp"
#include "netw/predict/joint.hpp"
#include "netw/sim/select.hpp"

namespace TestNetwPredictJointLaws {

using namespace godot;
using namespace netw;
using namespace netw::predict;

LocalVector<FieldDecl> declaration() {
    LocalVector<FieldDecl> out;
    out.push_back(field_decl(StringName("value"), 0));
    return out;
}

Array state(int p_value) {
    Array out;
    out.push_back(p_value);
    return out;
}

void configure_joint(
    NetwPredictionEngine *p_pool,
    int64_t p_owner,
    int64_t p_member
) {
    CHECK(p_pool->configure(
        p_owner,
        int(Schedule::TICK),
        int(Role::PREDICT),
        int(CorrectionMode::SNAP),
        int(RestoreMode::EXACT),
        6,
        NetwPredictionEngine::ISLAND_JOINT
    ));
    CHECK(p_pool->configure(
        p_member,
        int(Schedule::TICK),
        int(Role::SIMULATE),
        int(CorrectionMode::SNAP),
        int(RestoreMode::EXACT),
        6,
        NetwPredictionEngine::ISLAND_DECLARED
    ));
}

LocalVector<sim::Candidate> chosen(int64_t p_member) {
    LocalVector<sim::Candidate> out;
    sim::Candidate row;
    row.key = p_member;
    row.order_key = 10;
    row.distance_squared = 1.0;
    row.pick = sim::Pick::CHOSEN;
    row.eligible = true;
    out.push_back(row);
    return out;
}

sim::Selection adopt_member(
    NetwPredictionEngine *p_pool,
    int64_t p_owner,
    int64_t p_member,
    int64_t p_frontier
) {
    sim::Selection selection;
    selection.owner_order_key = 20;
    selection.commit(chosen(p_member), p_frontier);
    p_pool->joint_adopt(p_owner, selection);
    return selection;
}

bool configure_pair(
    NetwPredictionEngine *p_pool,
    int64_t p_owner,
    int p_owner_schedule,
    int p_island,
    int64_t p_candidate,
    int p_candidate_schedule
) {
    return p_pool->configure(
               p_owner,
               p_owner_schedule,
               int(Role::PREDICT),
               int(CorrectionMode::SNAP),
               int(RestoreMode::EXACT),
               6,
               p_island
           )
        && p_pool->configure(
            p_candidate,
            p_candidate_schedule,
            int(Role::PREDICT),
            int(CorrectionMode::SNAP),
            int(RestoreMode::EXACT),
            6,
            NetwPredictionEngine::ISLAND_NONE
        );
}

TEST_CASE("[Networked][Predict][Hosted][Joint] oldest basis sets the floor") {
    LocalVector<int64_t> bases;
    bases.push_back(40);
    bases.push_back(34);
    LocalVector<int64_t> relays;
    const JointFloorDecision decision = joint_floor(bases, relays, -1, 0, 50);
    NETW_CHECK_EQ(decision.floor, 34);
    CHECK_FALSE(decision.heal);
}

TEST_CASE("[Networked][Predict][Hosted][Joint] relay and epoch lower floor") {
    LocalVector<int64_t> bases;
    bases.push_back(40);
    LocalVector<int64_t> relays;
    relays.push_back(31);
    JointFloorDecision decision = joint_floor(bases, relays, -1, 0, 50);
    NETW_CHECK_EQ(decision.floor, 31);
    decision = joint_floor(bases, relays, 12, 0, 50);
    NETW_CHECK_EQ(decision.floor, 12);
}

TEST_CASE("[Networked][Predict][Hosted][Joint] old floor heals to present") {
    LocalVector<int64_t> bases;
    bases.push_back(4);
    LocalVector<int64_t> relays;
    const JointFloorDecision decision = joint_floor(bases, relays, -1, 20, 50);
    CHECK(decision.heal);
    NETW_CHECK_EQ(decision.floor, 50);
}

TEST_CASE("[Networked][Predict][Hosted][Joint] no basis stays at present") {
    LocalVector<int64_t> bases;
    LocalVector<int64_t> relays;
    const JointFloorDecision decision = joint_floor(bases, relays, -1, 0, 50);
    CHECK_FALSE(decision.heal);
    NETW_CHECK_EQ(decision.floor, 50);
}

TEST_CASE("[Networked][Predict][Hosted][Joint] cell provenance is strict") {
    NETW_CHECK_EQ(int(joint_cell(true, true, true)), 3);
    NETW_CHECK_EQ(int(joint_cell(false, true, true)), 2);
    NETW_CHECK_EQ(int(joint_cell(false, false, true)), 1);
    NETW_CHECK_EQ(int(joint_cell(false, false, false)), 0);
}

TEST_CASE(
    "[Networked][Predict][Hosted][Joint] a JOINT subject admits a re-runnable "
    "candidate that declared nothing, refuses a framed one, and a subject "
    "that is not JOINT admits frames"
) {
    NetwPredictionEngine held;
    NetwPredictionEngine *const engine = &held;
    const int64_t owner = engine->open(declaration());
    const int64_t candidate = engine->open(declaration());

    REQUIRE(configure_pair(
        engine,
        owner,
        int(Schedule::TICK),
        NetwPredictionEngine::ISLAND_JOINT,
        candidate,
        int(Schedule::TICK)
    ));
    CHECK(engine->joint_admits(owner, candidate));

    REQUIRE(configure_pair(
        engine,
        owner,
        int(Schedule::TICK),
        NetwPredictionEngine::ISLAND_JOINT,
        candidate,
        int(Schedule::FRAME)
    ));
    CHECK_FALSE(engine->joint_admits(owner, candidate));

    REQUIRE(configure_pair(
        engine,
        owner,
        int(Schedule::FRAME),
        NetwPredictionEngine::ISLAND_DECLARED,
        candidate,
        int(Schedule::FRAME)
    ));
    CHECK(engine->joint_admits(owner, candidate));
    CHECK_FALSE(engine->joint_admits(owner, owner));
}

TEST_CASE(
    "[Networked][Predict][Hosted][Joint] an adopted roster is the owner's "
    "joint group, and each member's tenure opens where it was selected"
) {
    NetwPredictionEngine held;
    NetwPredictionEngine *const engine = &held;
    const int64_t owner = engine->open(declaration());
    const int64_t member = engine->open(declaration());
    configure_joint(engine, owner, member);

    adopt_member(engine, owner, member, 10);
    NETW_CHECK_EQ(engine->joint_member_count(owner), 1);
    CHECK(engine->joint_promoted(owner, member));
    NETW_CHECK_EQ(engine->tenure_begin(member), 11);
    NETW_CHECK_EQ(engine->tenure_end(member), -1);
}

TEST_CASE(
    "[Networked][Predict][Hosted][Joint] a late command keeps the state"
) {
    StateRow observed;
    observed.resize(1);
    observed.set(0, Variant(70));

    JointTrack track;
    track.record(4, observed, Variant(7), CellProvenance::SUBSTITUTED);
    track.record(4, StateRow(), Variant(9), CellProvenance::RELAYED);

    REQUIRE(track.state_at(4) != nullptr);
    NETW_CHECK_EQ(int(track.state_at(4)->values[0]), 70);
    REQUIRE(track.command_at(4) != nullptr);
    NETW_CHECK_EQ(int(track.command_at(4)->command), 9);
}

TEST_CASE("[Networked][Predict][Hosted][Joint] a slot with no island records") {
    NetwPredictionEngine held;
    NetwPredictionEngine *const engine = &held;
    const int64_t slot = engine->open(declaration());
    CHECK(engine->configure(
        slot,
        int(Schedule::TICK),
        int(Role::PREDICT),
        int(CorrectionMode::SNAP),
        int(RestoreMode::EXACT),
        6,
        NetwPredictionEngine::ISLAND_NONE
    ));

    engine->joint_record(slot, 3, state(30), Variant(7), false, false, true);
    NETW_CHECK_EQ(int(engine->joint_command_at(slot, 3)), 7);
    NETW_CHECK_EQ(
        engine->joint_provenance_at(slot, 3),
        int(CellProvenance::SUBSTITUTED)
    );

    engine->joint_record(slot, 3, Array(), Variant(9), false, true, false);
    NETW_CHECK_EQ(int(engine->joint_command_at(slot, 3)), 9);
    engine->joint_record(slot, 3, Array(), Variant(11), false, false, true);
    NETW_CHECK_EQ(int(engine->joint_command_at(slot, 3)), 9);
    NETW_CHECK_EQ(int(engine->joint_command_at(slot, 4)), 0);
    NETW_CHECK_EQ(engine->joint_provenance_at(slot, 4), -1);
}

TEST_CASE("[Networked][Predict][Hosted][Joint] command provenance improves") {
    JointTrack track;
    track.record(4, StateRow(), Variant(10), CellProvenance::SUBSTITUTED);
    track.record(4, StateRow(), Variant(20), CellProvenance::RELAYED);
    track.record(4, StateRow(), Variant(30), CellProvenance::COAST);
    NETW_CHECK_EQ(track.commands.size(), 1);
    const JointCommandRecord *command = track.command_at(4);
    REQUIRE(command != nullptr);
    NETW_CHECK_EQ(int(command->provenance), int(CellProvenance::RELAYED));
    NETW_CHECK_EQ(int(command->command), 20);
}

TEST_CASE("[Networked][Predict][Hosted][Joint] support admits declared axes") {
    NetwPredictionEngine held;
    NetwPredictionEngine *const engine = &held;
    CHECK(engine->supports(
        int(Schedule::TICK),
        int(Role::SIMULATE),
        int(CorrectionMode::SNAP),
        int(RestoreMode::EXACT),
        NetwPredictionEngine::ISLAND_DECLARED
    ));
    CHECK_FALSE(engine->supports(
        int(Schedule::TICK),
        int(Role::SIMULATE),
        int(CorrectionMode::SNAP),
        int(RestoreMode::EXACT)
    ));
    CHECK(engine->supports(
        int(Schedule::STEPPED),
        int(Role::PREDICT),
        int(CorrectionMode::SNAP),
        int(RestoreMode::EXACT),
        NetwPredictionEngine::ISLAND_JOINT
    ));
    CHECK_FALSE(engine->supports(
        int(Schedule::FRAME),
        int(Role::PREDICT),
        int(CorrectionMode::SNAP),
        int(RestoreMode::EXACT),
        NetwPredictionEngine::ISLAND_JOINT
    ));
}

TEST_CASE(
    "[Networked][Predict][Hosted][Joint] a stepped group commits and passes"
) {
    NetwPredictionEngine held;
    NetwPredictionEngine *const engine = &held;
    const int64_t owner = engine->open(declaration());
    const int64_t member = engine->open(declaration());
    CHECK(engine->configure(
        owner,
        int(Schedule::STEPPED),
        int(Role::PREDICT),
        int(CorrectionMode::SNAP),
        int(RestoreMode::EXACT),
        6,
        NetwPredictionEngine::ISLAND_JOINT
    ));
    CHECK(engine->configure(
        member,
        int(Schedule::STEPPED),
        int(Role::SIMULATE),
        int(CorrectionMode::SNAP),
        int(RestoreMode::EXACT),
        6,
        NetwPredictionEngine::ISLAND_DECLARED
    ));
    adopt_member(engine, owner, member, 0);
    NETW_CHECK_EQ(engine->joint_member_count(owner), 1);

    engine->joint_record(owner, 0, state(0), Variant(0), true, false, false);
    engine
        ->joint_record(member, 0, state(100), Variant(200), false, false, true);
    engine->joint_record(owner, 1, state(1), Variant(1), true, false, false);
    engine
        ->joint_record(member, 1, state(101), Variant(201), false, false, true);
    engine->joint_note_basis(owner, 0, NetwPredictionEngine::JOINT_FLOOR_STATE);

    const predict::JointPassPlan plan = engine->joint_pass(owner, 1);
    CHECK(plan.valid);
    NETW_CHECK_EQ(plan.floor, 0);
    NETW_CHECK_EQ(int(plan.restores.size()), 2);
    NETW_CHECK_EQ(int(plan.steps.size()), 2);
}

TEST_CASE(
    "[Networked][Predict][Hosted][Joint] pass is transition then member order"
) {
    NetwPredictionEngine held;
    NetwPredictionEngine *const engine = &held;
    const int64_t owner = engine->open(declaration());
    const int64_t member = engine->open(declaration());
    configure_joint(engine, owner, member);
    adopt_member(engine, owner, member, 0);

    for (int transition = 0; transition <= 3; ++transition) {
        engine->joint_record(
            owner,
            transition,
            state(transition),
            Variant(transition),
            true,
            false,
            false
        );
        engine->joint_record(
            member,
            transition,
            state(100 + transition),
            Variant(200 + transition),
            false,
            false,
            true
        );
    }
    engine
        ->joint_record(member, 2, state(102), Variant(302), false, true, false);
    engine->joint_note_basis(owner, 1, NetwPredictionEngine::JOINT_FLOOR_STATE);
    engine->joint_note_basis(owner, 0, NetwPredictionEngine::JOINT_FLOOR_STATE);

    const predict::JointPassPlan plan = engine->joint_pass(owner, 3);
    CHECK(plan.valid);
    CHECK_FALSE(plan.heal);
    NETW_CHECK_EQ(plan.floor, 1);
    NETW_CHECK_EQ(int(plan.restores.size()), 2);
    NETW_CHECK_EQ(plan.restores[0].slot, member);
    NETW_CHECK_EQ(plan.restores[1].slot, owner);
    NETW_CHECK_EQ(int(plan.steps.size()), 4);
    NETW_CHECK_EQ(plan.steps[0].transition, 2);
    NETW_CHECK_EQ(plan.steps[0].slot, member);
    NETW_CHECK_EQ(int(plan.steps[0].provenance), int(CellProvenance::RELAYED));
    NETW_CHECK_EQ(int(plan.steps[0].command), 302);
    NETW_CHECK_EQ(plan.steps[1].slot, owner);
    NETW_CHECK_EQ(plan.steps[2].transition, 3);
    NETW_CHECK_EQ(plan.steps[2].slot, member);
    NETW_CHECK_EQ(int(engine->state_at(member, 0)), 101);

    const PackedInt64Array stats = engine->joint_stats(owner);
    NETW_CHECK_EQ(stats[NetwPredictionEngine::STAT_JOINT_PASSES], 1);
    NETW_CHECK_EQ(stats[NetwPredictionEngine::STAT_JOINT_MEMBERS], 2);
    NETW_CHECK_EQ(stats[NetwPredictionEngine::STAT_JOINT_CELLS_RELAYED], 1);
    NETW_CHECK_EQ(stats[NetwPredictionEngine::STAT_JOINT_CELLS_SUBSTITUTED], 1);
    NETW_CHECK_EQ(stats[NetwPredictionEngine::STAT_JOINT_FLOOR], 1);
    NETW_CHECK_EQ(stats[NetwPredictionEngine::STAT_JOINT_PRESENT], 3);
    NETW_CHECK_EQ(stats[NetwPredictionEngine::STAT_JOINT_MAX_DEPTH], 2);
    NETW_CHECK_EQ(stats[NetwPredictionEngine::STAT_JOINT_FLOOR_STATE_MOVES], 1);
}

TEST_CASE("[Networked][Predict][Hosted][Joint] linger ends past tenure") {
    NetwPredictionEngine held;
    NetwPredictionEngine *const engine = &held;
    const int64_t owner = engine->open(declaration());
    const int64_t member = engine->open(declaration());
    configure_joint(engine, owner, member);
    sim::Selection selection = adopt_member(engine, owner, member, 0);

    selection.commit(LocalVector<sim::Candidate>(), 8);
    engine->joint_adopt(owner, selection);
    NETW_CHECK_EQ(engine->tenure_end(member), 8);
    for (int transition = 8; transition <= 9; ++transition) {
        engine->joint_record(
            owner,
            transition,
            state(transition),
            Variant(),
            false,
            false,
            false
        );
        engine->joint_record(
            member,
            transition,
            state(100 + transition),
            Variant(),
            false,
            false,
            false
        );
    }
    engine->joint_note_basis(owner, 8, NetwPredictionEngine::JOINT_FLOOR_STATE);
    predict::JointPassPlan plan = engine->joint_pass(owner, 8);
    NETW_CHECK_EQ(int(plan.restores.size()), 2);
    NETW_CHECK_EQ(
        engine->joint_stats(
            owner
        )[NetwPredictionEngine::STAT_JOINT_LINGER_HELD],
        1
    );

    engine->joint_note_basis(owner, 9, NetwPredictionEngine::JOINT_FLOOR_STATE);
    plan = engine->joint_pass(owner, 9);
    NETW_CHECK_EQ(int(plan.restores.size()), 1);
    NETW_CHECK_EQ(
        engine->joint_stats(
            owner
        )[NetwPredictionEngine::STAT_JOINT_LINGER_HELD],
        0
    );
}

struct SeatedEntity {
    Node *owner = nullptr;
    Ref<NetwEntity> entity;

    SeatedEntity(const char *p_id) {
        owner = memnew(Node);
        entity = NetwEntity::ensure(owner);
        REQUIRE(entity.is_valid());
        entity->set_entity_id(p_id);
        entity->set_peer_id(1);
    }

    ~SeatedEntity() {
        memdelete(owner);
    }
};

struct RoutedEntity {
    Node *owner = nullptr;
    Ref<NetwEntity> entity;

    RoutedEntity(int64_t p_route) {
        owner = memnew(Node);
        entity = NetwEntity::ensure(owner);
        REQUIRE(entity.is_valid());
        entity->set_route(p_route);
    }

    ~RoutedEntity() {
        memdelete(owner);
    }
};

TEST_CASE(
    "[Networked][Predict][Hosted][Island] two participants carrying no "
    "entity_id are still told apart, because the roster keys on route"
) {
    NetwPredictionEngine held;
    NetwPredictionEngine *const engine = &held;
    RoutedEntity owner_body(11);
    RoutedEntity first(7);
    RoutedEntity second(9);

    const int64_t owner = engine->slot_register(owner_body.entity);
    engine->roster_add(
        owner,
        NetwPredictionEngine::ROSTER_ISLAND_MEMBERS,
        first.entity
    );
    engine->roster_add(
        owner,
        NetwPredictionEngine::ROSTER_ISLAND_MEMBERS,
        second.entity
    );

    const PackedStringArray ids = engine->live_participant_ids(owner);
    NETW_CHECK_EQ(ids.size(), 2);
    CHECK(ids[0] != ids[1]);
    CHECK_FALSE(String(ids[0]).is_empty());
    CHECK_FALSE(String(ids[1]).is_empty());
}

TEST_CASE(
    "[Networked][Predict][Hosted][Island] a promotion opens a tenure, a "
    "demotion closes one and lingers, and the floor releases it"
) {
    NetwPredictionEngine held;
    NetwPredictionEngine *const engine = &held;
    SeatedEntity owner_body("owner");
    SeatedEntity member_body("member");

    const int64_t owner = engine->slot_register(owner_body.entity);
    const int64_t member = engine->slot_register(member_body.entity);
    configure_joint(engine, owner, member);

    TypedArray<NetwEntity> promoted;
    promoted.push_back(member_body.entity);
    CHECK(engine->apply_island_promotions(owner, promoted));
    CHECK(engine->roster_has(
        owner,
        NetwPredictionEngine::ROSTER_SIMULATED,
        member_body.entity
    ));
    CHECK_FALSE(engine->roster_has(
        owner,
        NetwPredictionEngine::ROSTER_JOINT_LINGERING,
        member_body.entity
    ));
    NETW_CHECK_EQ(
        engine->tenure_begin_of(member),
        engine->drive_frontier(owner) + 1
    );

    CHECK_FALSE(engine->apply_island_promotions(owner, promoted));

    CHECK(engine->apply_island_promotions(owner, TypedArray<NetwEntity>()));
    CHECK_FALSE(engine->roster_has(
        owner,
        NetwPredictionEngine::ROSTER_SIMULATED,
        member_body.entity
    ));
    CHECK(engine->roster_has(
        owner,
        NetwPredictionEngine::ROSTER_JOINT_LINGERING,
        member_body.entity
    ));

    NETW_CHECK_EQ(
        engine->release_lingering(owner, engine->tenure_end_of(member)),
        1
    );
    NETW_CHECK_EQ(
        engine->release_lingering(owner, engine->tenure_end_of(member) + 1),
        0
    );
    CHECK_FALSE(engine->roster_has(
        owner,
        NetwPredictionEngine::ROSTER_JOINT_LINGERING,
        member_body.entity
    ));
}

TEST_CASE(
    "[Networked][Predict][Hosted][Island] a contact is equivalent only where "
    "every body it names is seated AND simulated, never merely seated"
) {
    NetwPredictionEngine held;
    NetwPredictionEngine *const engine = &held;
    SeatedEntity owner_body("owner");
    SeatedEntity member_body("member");

    const int64_t owner = engine->slot_register(owner_body.entity);
    CHECK_FALSE(engine->contact_is_equivalent(owner, false));

    engine->roster_add(
        owner,
        NetwPredictionEngine::ROSTER_ISLAND_MEMBERS,
        member_body.entity
    );
    const Ref<NetwPredictionHandle> handle
        = member_body.entity->get_prediction();
    REQUIRE(handle.is_valid());
    handle->set_sim_mode(NetwPredict::SIM_MODE_DISPLAY);
    CHECK_FALSE(engine->contact_is_equivalent(owner, false));

    handle->set_sim_mode(NetwPredict::SIM_MODE_SPECULATIVE);
    CHECK(engine->contact_is_equivalent(owner, false));
}

StateRow one_field(const Variant &p_value) {
    StateRow out;
    out.resize(1);
    out.set(0, p_value);
    return out;
}

TEST_CASE(
    "[Networked][Predict][Hosted][Joint] a late transition evicts the oldest"
) {
    JointTrack track;
    for (int64_t transition = 1; transition <= 256; ++transition) {
        track.record(
            transition,
            one_field(transition),
            Variant(),
            CellProvenance::COAST
        );
    }
    NETW_CHECK_EQ(int(track.states.size()), 256);
    CHECK((track.state_at(1) != nullptr));

    track.record(0, one_field(0), Variant(), CellProvenance::COAST);

    NETW_CHECK_EQ(int(track.states.size()), 256);
    CHECK((track.state_at(0) == nullptr));
    CHECK((track.state_at(1) != nullptr));
    CHECK((track.state_at(256) != nullptr));
}

struct DeclaredBody {
    Node3D *owner = nullptr;
    Ref<NetwEntity> entity;
    int64_t slot = -1;

    DeclaredBody(
        NetwPredictionEngine *p_pool,
        const char *p_id,
        int p_role,
        int p_island
    ) {
        owner = memnew(Node3D);
        entity = NetwEntity::ensure(owner);
        REQUIRE(entity.is_valid());
        entity->set_entity_id(p_id);
        entity->set_peer_id(1);
        slot = p_pool->slot_register(entity);
        REQUIRE(slot >= 0);
        Ref<NetwPropertySet> set;
        set.instantiate();
        set->record = NetwPropertySet::RECORD_STATE;
        set->bind_column(
            NetwPropertySetColumn::create(
                StringName("position"),
                Ref<NetwQuantize>(),
                false,
                int64_t(SchemaCore::VARIANT)
            )
        );
        p_pool->adopt_declaration(
            entity,
            NetwPropertySetBinding::create(set, owner),
            Ref<NetwPropertySetBinding>(),
            int(Schedule::TICK),
            p_role,
            int(CorrectionMode::SNAP),
            int(RestoreMode::EXACT),
            6,
            p_island,
            false
        );
    }

    ~DeclaredBody() {
        memdelete(owner);
    }
};

Array pose(double p_x) {
    Array out;
    out.push_back(Vector3(p_x, 0.0, 0.0));
    return out;
}

TEST_CASE(
    "[Networked][Predict][Hosted][Joint] a pass restores a member from the "
    "payload that arrived at the floor"
) {
    NetwPredictionEngine held;
    NetwPredictionEngine *const engine = &held;
    DeclaredBody owner(
        engine,
        "owner",
        int(Role::PREDICT),
        NetwPredictionEngine::ISLAND_JOINT
    );
    DeclaredBody member(
        engine,
        "member",
        int(Role::SIMULATE),
        NetwPredictionEngine::ISLAND_DECLARED
    );
    adopt_member(engine, owner.slot, member.slot, 0);
    NETW_CHECK_EQ(engine->joint_member_count(owner.slot), 1);

    for (int transition = 0; transition <= 2; ++transition) {
        engine->joint_record(
            owner.slot,
            transition,
            pose(transition),
            Variant(transition),
            true,
            false,
            false
        );
        engine->joint_record(
            member.slot,
            transition,
            pose(100 + transition),
            Variant(),
            false,
            false,
            true
        );
    }
    Dictionary arrived;
    arrived[StringName("position")] = Vector3(-7.0, 0.0, 0.0);
    engine->note_joint_basis(
        member.slot,
        1,
        arrived,
        NetwPredictionEngine::JOINT_FLOOR_STATE
    );

    const predict::JointPassPlan plan = engine->joint_pass(owner.slot, 2);
    REQUIRE(plan.valid);
    NETW_CHECK_EQ(plan.floor, 1);
    int restored = -1;
    for (uint32_t at = 0; at < plan.restores.size(); ++at) {
        if (plan.restores[at].slot == member.slot) {
            restored = int(at);
        }
    }
    REQUIRE(restored >= 0);
    const Dictionary payload
        = engine->restore_payload_of(member.slot, plan, restored);
    const Vector3 restored_pose
        = payload.get(StringName("position"), Vector3());
    NETW_CHECK_CLOSE(restored_pose.x, -7.0, 1.0e-9);
}

} // namespace TestNetwPredictJointLaws
