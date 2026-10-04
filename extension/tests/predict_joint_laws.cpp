#include "support/books.h"
#include "support/netw_test.h"
#include "support/predict_rows.h"

#include "godot/spatial_node.hpp"
#include "netw/api/prediction.hpp"
#include "netw/api/schema_core.hpp"
#include "netw/predict/engine.hpp"
#include "netw/predict/joint.hpp"
#include "netw/repl/property_binding.hpp"
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

predict::StateRow one_field(const Variant &p_value) {
    predict::StateRow out;
    out.resize(1);
    out.set(0, p_value);
    return out;
}

predict::StateRow state(int p_value) {
    return one_field(p_value);
}

int64_t commanded(NetwPredictionEngine *p_pool) {
    const int64_t slot = p_pool->open(declaration());
    LocalVector<FieldDecl> input;
    input.push_back(field_decl(StringName("command"), 0));
    p_pool->rewire(slot, declaration(), input);
    return slot;
}

predict::StateRow command(int p_value) {
    return one_field(p_value);
}

predict::StateRow held_command(
    NetwPredictionEngine *p_pool,
    int64_t p_slot,
    int64_t p_transition
) {
    predict::StateRow out;
    p_pool->read_command_at(p_slot, p_transition, out);
    return out;
}

int command_of(const predict::StateRow &p_command) {
    return p_command.has(0) ? int(p_command.values[0]) : 0;
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
    NetwPredictionEngine held;
    NetwPredictionEngine *const engine = &held;
    const int64_t slot = commanded(engine);
    engine->joint_record(slot, 4, state(70), command(7), false, false, true);
    engine->joint_record(
        slot,
        4,
        predict::StateRow(),
        command(9),
        false,
        true,
        false
    );

    const Dictionary kept = netw_test::predicted_state(engine, slot, 5);
    REQUIRE(kept.has(StringName("value")));
    NETW_CHECK_EQ(int(kept[StringName("value")]), 70);
    NETW_CHECK_EQ(command_of(held_command(engine, slot, 4)), 9);
}

TEST_CASE("[Networked][Predict][Hosted][Joint] a slot with no island records") {
    NetwPredictionEngine held;
    NetwPredictionEngine *const engine = &held;
    const int64_t slot = commanded(engine);
    CHECK(engine->configure(
        slot,
        int(Schedule::TICK),
        int(Role::PREDICT),
        int(CorrectionMode::SNAP),
        int(RestoreMode::EXACT),
        6,
        NetwPredictionEngine::ISLAND_NONE
    ));

    engine->joint_record(slot, 3, state(30), command(7), false, false, true);
    NETW_CHECK_EQ(command_of(held_command(engine, slot, 3)), 7);
    NETW_CHECK_EQ(
        engine->joint_provenance_at(slot, 3),
        int(CellProvenance::SUBSTITUTED)
    );

    engine->joint_record(
        slot,
        3,
        predict::StateRow(),
        command(9),
        false,
        true,
        false
    );
    NETW_CHECK_EQ(command_of(held_command(engine, slot, 3)), 9);
    engine->joint_record(
        slot,
        3,
        predict::StateRow(),
        command(11),
        false,
        false,
        true
    );
    NETW_CHECK_EQ(command_of(held_command(engine, slot, 3)), 9);
    NETW_CHECK_EQ(command_of(held_command(engine, slot, 4)), 0);
    NETW_CHECK_EQ(engine->joint_provenance_at(slot, 4), -1);
}

TEST_CASE("[Networked][Predict][Hosted][Joint] command provenance improves") {
    NetwPredictionEngine held;
    NetwPredictionEngine *const engine = &held;
    const int64_t slot = commanded(engine);
    engine->joint_record(
        slot,
        4,
        predict::StateRow(),
        command(10),
        false,
        false,
        true
    );
    engine->joint_record(
        slot,
        4,
        predict::StateRow(),
        command(20),
        false,
        true,
        false
    );
    engine->joint_record(
        slot,
        4,
        predict::StateRow(),
        command(30),
        false,
        false,
        false
    );
    NETW_CHECK_EQ(int(engine->input_book(slot)->count()), 1);
    NETW_CHECK_EQ(
        engine->joint_provenance_at(slot, 4),
        int(CellProvenance::RELAYED)
    );
    NETW_CHECK_EQ(command_of(held_command(engine, slot, 4)), 20);
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

    engine->joint_record(owner, 0, state(0), command(0), true, false, false);
    engine
        ->joint_record(member, 0, state(100), command(200), false, false, true);
    engine->joint_record(owner, 1, state(1), command(1), true, false, false);
    engine
        ->joint_record(member, 1, state(101), command(201), false, false, true);
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
    const int64_t owner = commanded(engine);
    const int64_t member = commanded(engine);
    configure_joint(engine, owner, member);
    adopt_member(engine, owner, member, 0);

    for (int transition = 0; transition <= 3; ++transition) {
        engine->joint_record(
            owner,
            transition,
            state(transition),
            command(transition),
            true,
            false,
            false
        );
        engine->joint_record(
            member,
            transition,
            state(100 + transition),
            command(200 + transition),
            false,
            false,
            true
        );
    }
    engine
        ->joint_record(member, 2, state(102), command(302), false, true, false);
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
    NETW_CHECK_EQ(command_of(plan.steps[0].command), 302);
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
            predict::StateRow(),
            false,
            false,
            false
        );
        engine->joint_record(
            member,
            transition,
            state(100 + transition),
            predict::StateRow(),
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
        entity->set_name(p_id);
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
    const Ref<NetwPrediction> handle = member_body.entity->get_prediction();
    REQUIRE(handle.is_valid());
    handle->set_sim_mode(NetwPrediction::SIM_MODE_DISPLAY);
    CHECK_FALSE(engine->contact_is_equivalent(owner, false));

    handle->set_sim_mode(NetwPrediction::SIM_MODE_SPECULATIVE);
    CHECK(engine->contact_is_equivalent(owner, false));
}

TEST_CASE(
    "[Networked][Predict][Hosted][Joint] a late transition evicts the oldest"
) {
    NetwPredictionEngine held;
    NetwPredictionEngine *const engine = &held;
    const int64_t slot = engine->open(declaration());
    const int depth = int(engine->book_depth(slot));
    for (int transition = 1; transition <= depth; ++transition) {
        engine->joint_record(
            slot,
            transition,
            state(transition),
            predict::StateRow(),
            false,
            false,
            false
        );
    }
    const predict::TransitionBook *book = engine->predicted_book(slot);
    REQUIRE(book != nullptr);
    NETW_CHECK_EQ(int(book->count()), depth);
    CHECK(book->holds(2));

    engine->joint_record(
        slot,
        0,
        state(0),
        predict::StateRow(),
        false,
        false,
        false
    );

    NETW_CHECK_EQ(int(book->count()), depth);
    CHECK_FALSE(book->holds(1));
    CHECK(book->holds(2));
    CHECK(book->holds(depth + 1));
}

struct DeclaredBody {
    Node3D *owner = nullptr;
    Ref<NetwEntity> entity;
    int64_t slot = -1;

    DeclaredBody(
        netw_test::Books &p_books,
        NetwPredictionEngine *p_pool,
        const char *p_id,
        int p_role,
        int p_island
    ) {
        owner = memnew(Node3D);
        entity = NetwEntity::ensure(owner);
        REQUIRE(entity.is_valid());
        entity->set_name(p_id);
        entity->set_peer_id(1);
        slot = p_pool->slot_register(entity);
        REQUIRE(slot >= 0);
        const uint64_t declared = p_books.sets.create();
        repl::PropertySet *set = p_books.sets.get(declared);
        set->record = repl::PropertySet::RECORD_STATE;
        set->bind_column(
            repl::PropertyColumn::create(
                StringName("position"),
                Ref<NetwQuantize>(),
                false,
                enums::NetwMultiplayer::ColumnType(SchemaCore::VARIANT)
            )
        );
        p_pool->adopt_declaration(
            entity,
            p_books.bindings.create(declared, owner),
            repl::PropertyBindingPtr(),
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

predict::StateRow pose(double p_x) {
    return one_field(Vector3(p_x, 0.0, 0.0));
}

TEST_CASE(
    "[Networked][Predict][Hosted][Joint] a pass restores a member from the "
    "payload that arrived at the floor"
) {
    netw_test::Books books;
    NetwPredictionEngine held;
    held.attach_bindings(&books.bindings);
    NetwPredictionEngine *const engine = &held;
    DeclaredBody owner(
        books,
        engine,
        "owner",
        int(Role::PREDICT),
        NetwPredictionEngine::ISLAND_JOINT
    );
    DeclaredBody member(
        books,
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
            command(transition),
            true,
            false,
            false
        );
        engine->joint_record(
            member.slot,
            transition,
            pose(100 + transition),
            predict::StateRow(),
            false,
            false,
            true
        );
    }
    engine->note_joint_basis(
        member.slot,
        1,
        pose(-7.0),
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
    const predict::StateRow &state = plan.restores[uint32_t(restored)].state;
    REQUIRE(state.has(0));
    const Vector3 restored_pose = state.values[0];
    NETW_CHECK_CLOSE(restored_pose.x, -7.0, 1.0e-9);
}

} // namespace TestNetwPredictJointLaws
