#include "support/netw_test.h"

#include "netw/api/entity.hpp"
#include "netw/api/netw_multiplayer.hpp"
#include "netw/entity/stage.hpp"
#include "support/netw_call_log.h"

namespace TestNetwStage {

using netw::entity::Stage;
using netw::entity::stage_can_begin_despawn;
using netw::entity::stage_edge_is_legal;

constexpr int STAGES = 6;

struct Edge {
    Stage from;
    Stage to;
};

constexpr Edge LEGAL_EDGES[] = {
    {Stage::UNBOUND, Stage::ARMED},
    {Stage::UNBOUND, Stage::DESPAWNING},
    {Stage::ARMED, Stage::LIVE},
    {Stage::ARMED, Stage::DESPAWNING},
    {Stage::LIVE, Stage::DESPAWNING},
    {Stage::DESPAWNING, Stage::LINGERING},
    {Stage::DESPAWNING, Stage::FREED},
    {Stage::LINGERING, Stage::FREED},
};

bool listed_as_legal(int p_from, int p_to) {
    for (const Edge &edge : LEGAL_EDGES) {
        if (int(edge.from) == p_from && int(edge.to) == p_to) {
            return true;
        }
    }
    return false;
}

TEST_CASE("[Networked][Entity][Hosted] S1 the whole edge table, cell by cell") {
    for (int from = 0; from < STAGES; ++from) {
        for (int to = 0; to < STAGES; ++to) {
            NETW_FORMAT_INT(from_text, from);
            NETW_FORMAT_INT(to_text, to);
            CAPTURE(from_text);
            CAPTURE(to_text);
            CHECK(stage_edge_is_legal(from, to) == listed_as_legal(from, to));
        }
    }
}

TEST_CASE(
    "[Networked][Entity][Hosted] S2 a stage is never re-entered, which is "
    "where this differs from the session"
) {
    for (int stage = 0; stage < STAGES; ++stage) {
        NETW_FORMAT_INT(stage_text, stage);
        CAPTURE(stage_text);
        CHECK_FALSE(stage_edge_is_legal(stage, stage));
    }
}

TEST_CASE(
    "[Networked][Entity][Hosted] S3 the terminal stage admits nothing, and "
    "a stage off the table admits nothing either"
) {
    for (int to = 0; to < STAGES; ++to) {
        CHECK_FALSE(stage_edge_is_legal(int(Stage::FREED), to));
    }

    CHECK_FALSE(stage_edge_is_legal(99, int(Stage::LIVE)));
    CHECK_FALSE(stage_edge_is_legal(-1, int(Stage::LIVE)));
    CHECK_FALSE(stage_edge_is_legal(int(Stage::LIVE), 99));
    CHECK_FALSE(stage_can_begin_despawn(99));
}

TEST_CASE(
    "[Networked][Entity][Hosted] S4 teardown opens wider than LIVE, so a "
    "hand-bound route is never stranded"
) {
    CHECK(stage_can_begin_despawn(int(Stage::UNBOUND)));
    CHECK(stage_can_begin_despawn(int(Stage::ARMED)));
    CHECK(stage_can_begin_despawn(int(Stage::LIVE)));

    CHECK_FALSE(stage_can_begin_despawn(int(Stage::DESPAWNING)));
    CHECK_FALSE(stage_can_begin_despawn(int(Stage::LINGERING)));
    CHECK_FALSE(stage_can_begin_despawn(int(Stage::FREED)));

    for (int stage = 0; stage < STAGES; ++stage) {
        if (!stage_can_begin_despawn(stage)) {
            continue;
        }
        NETW_FORMAT_INT(stage_text, stage);
        CAPTURE(stage_text);
        CHECK(stage_edge_is_legal(stage, int(Stage::DESPAWNING)));
    }
}

using namespace godot;
using netw::NetwEntity;
using netw::NetwMultiplayer;
using netw_test::CallLog;

class StageProbe final : public CallableCustom {
    ObjectID entity;
    CallLog *log = nullptr;
    int64_t target = -1;
    bool erase_owner = false;
    Ref<NetwEntity> *release = nullptr;
    mutable bool acted = false;

    static bool same(const CallableCustom *p_a, const CallableCustom *p_b) {
        return p_a == p_b;
    }

    static bool before(const CallableCustom *p_a, const CallableCustom *p_b) {
        return p_a < p_b;
    }

public:
    StageProbe(
        NetwEntity *p_entity,
        CallLog *p_log,
        int64_t p_target = -1,
        bool p_erase_owner = false,
        Ref<NetwEntity> *p_release = nullptr
    )
        : entity(netw::gd::instance_id(p_entity)), log(p_log), target(p_target),
          erase_owner(p_erase_owner), release(p_release) {
    }

    uint32_t hash() const override {
        return uint32_t(uintptr_t(this));
    }

    String get_as_text() const override {
        return String("StageProbe");
    }

    CompareEqualFunc get_compare_equal_func() const override {
        return &StageProbe::same;
    }

    CompareLessFunc get_compare_less_func() const override {
        return &StageProbe::before;
    }

    ObjectID get_object() const override {
        return entity;
    }

    void call(
        const Variant **p_arguments,
        int p_count,
        Variant &r_return_value,
        netw::gd::CallError &r_call_error
    ) const override {
        NETW_CHECK_EQ(p_count, 2);
        NetwEntity *wrapper
            = Object::cast_to<NetwEntity>(netw::gd::object_of(entity));
        REQUIRE(wrapper != nullptr);
        const int64_t from = *p_arguments[0];
        const int64_t to = *p_arguments[1];
        NETW_CHECK_EQ(int64_t(wrapper->get_stage()), to);
        log->callable("stage").call(from, to);
        if (to == int64_t(Stage::UNBOUND)) {
            NETW_CHECK_EQ(wrapper->get_route(), int64_t(0));
        }
        if (!acted && to == target) {
            acted = true;
            if (erase_owner) {
                Node *owner = wrapper->get_owner();
                memdelete(owner);
                if (release != nullptr) {
                    release->unref();
                }
            } else {
                wrapper->get_record()->complete_departure(wrapper);
            }
        }
        r_return_value = Variant();
        netw::gd::call_ok(r_call_error);
    }
};

void connect_lifecycle(const Ref<NetwEntity> &p_entity, CallLog &p_log) {
    p_entity->connect("despawning", p_log.callable("despawning"));
    p_entity->connect("despawned", p_log.callable("despawned"));
}

void check_edge(CallLog &p_log, int p_index, Stage p_from, Stage p_to) {
    const Array args = p_log.args("stage", p_index);
    NETW_REQUIRE_EQ(args.size(), 2);
    NETW_CHECK_EQ(int64_t(args[0]), int64_t(p_from));
    NETW_CHECK_EQ(int64_t(args[1]), int64_t(p_to));
}

TEST_CASE(
    "[Networked][Entity][Hosted] stage publication covers classification "
    "and no-session lifecycle edges in committed order"
) {
    Node *owner = memnew(Node);
    const Ref<NetwEntity> entity = NetwEntity::ensure(owner);
    CallLog log;
    connect_lifecycle(entity, log);
    netw::gd::add_signal(entity.ptr(), "stage_changed", 2);
    entity->connect(
        "stage_changed",
        Callable(memnew(StageProbe(entity.ptr(), &log)))
    );
    NETW_CHECK_EQ(log.count("stage"), 0);

    SUBCASE("live is published once, and ready runs once it is live") {
        entity->set_name("stage_crate");
        entity->arm(Ref<NetwMultiplayer>());
        NetwMultiplayer::entity_enter_tree(
            entity.ptr(),
            owner,
            entity->get_record(),
            nullptr,
            true
        );
        check_edge(log, 0, Stage::UNBOUND, Stage::ARMED);
        check_edge(log, 1, Stage::ARMED, Stage::LIVE);
        CHECK_FALSE(entity->get_ready_fired());
        entity->_on_owner_ready();
        CHECK(entity->get_ready_fired());
        NetwMultiplayer::entity_enter_tree(
            entity.ptr(),
            owner,
            entity->get_record(),
            nullptr,
            true
        );
        NETW_CHECK_EQ(log.count("stage"), 2);
        entity->_handle_tree_exiting();
        check_edge(log, 2, Stage::LIVE, Stage::DESPAWNING);
        check_edge(log, 3, Stage::DESPAWNING, Stage::FREED);
        NETW_CHECK_EQ(log.count("despawned"), 1);
        CHECK(log.order()[log.order().size() - 2] == StringName("stage"));
        CHECK(log.order()[log.order().size() - 1] == StringName("despawned"));
        entity->_handle_tree_exiting();
        NETW_CHECK_EQ(log.count("stage"), 4);
    }

    SUBCASE("linger completion and readmission expose every edge") {
        entity->arm(Ref<NetwMultiplayer>());
        entity->_remote_despawn("stage_test", 1.0);
        check_edge(log, 1, Stage::ARMED, Stage::DESPAWNING);
        check_edge(log, 2, Stage::DESPAWNING, Stage::LINGERING);
        CHECK(log.order()[1] == StringName("stage"));
        CHECK(log.order()[2] == StringName("despawning"));
        entity->_handle_tree_exiting();
        check_edge(log, 3, Stage::LINGERING, Stage::FREED);
        const RID old_handle = entity->get_rid_handle();
        entity->set_route(19);
        entity->tree_entered_edge();
        check_edge(log, 4, Stage::FREED, Stage::UNBOUND);
        CHECK(entity->get_rid_handle() != old_handle);
    }

    memdelete(owner);
}

TEST_CASE(
    "[Networked][Entity][Hosted] stage callbacks can complete teardown "
    "without a stale ready or despawning announcement"
) {
    Node *owner = memnew(Node);
    const Ref<NetwEntity> entity = NetwEntity::ensure(owner);
    entity->set_name("stage_reentrant");
    CallLog log;
    connect_lifecycle(entity, log);
    netw::gd::add_signal(entity.ptr(), "stage_changed", 2);

    SUBCASE("teardown during live publication suppresses ready") {
        entity->connect(
            "stage_changed",
            Callable(
                memnew(StageProbe(entity.ptr(), &log, int64_t(Stage::LIVE)))
            )
        );
        entity->arm(Ref<NetwMultiplayer>());
        NetwMultiplayer::entity_enter_tree(
            entity.ptr(),
            owner,
            entity->get_record(),
            nullptr,
            true
        );
        check_edge(log, 1, Stage::ARMED, Stage::LIVE);
        check_edge(log, 2, Stage::LIVE, Stage::DESPAWNING);
        check_edge(log, 3, Stage::DESPAWNING, Stage::FREED);
        NETW_CHECK_EQ(log.count("despawned"), 1);
        entity->_on_owner_ready();
        CHECK_FALSE(entity->get_ready_fired());
    }

    SUBCASE("completion during despawn publication suppresses despawning") {
        entity->connect(
            "stage_changed",
            Callable(memnew(
                StageProbe(entity.ptr(), &log, int64_t(Stage::DESPAWNING))
            ))
        );
        entity->arm(Ref<NetwMultiplayer>());
        entity->_remote_despawn("stage_complete", 1.0);
        check_edge(log, 1, Stage::ARMED, Stage::DESPAWNING);
        check_edge(log, 2, Stage::DESPAWNING, Stage::FREED);
        NETW_CHECK_EQ(log.count("despawning"), 0);
        NETW_CHECK_EQ(log.count("despawned"), 1);
        NETW_CHECK_EQ(int64_t(entity->get_stage()), int64_t(Stage::FREED));
    }
    memdelete(owner);
}

TEST_CASE(
    "[Networked][Entity][Hosted] a stage callback can delete its owner and "
    "release the last external wrapper reference"
) {
    Node *owner = memnew(Node);
    Ref<NetwEntity> entity = NetwEntity::ensure(owner);
    entity->set_name("stage_release");
    const ObjectID wrapper_id = netw::gd::instance_id(entity.ptr());
    CallLog log;
    connect_lifecycle(entity, log);
    netw::gd::add_signal(entity.ptr(), "stage_changed", 2);
    entity->connect(
        "stage_changed",
        Callable(memnew(
            StageProbe(entity.ptr(), &log, int64_t(Stage::LIVE), true, &entity)
        ))
    );
    entity->arm(Ref<NetwMultiplayer>());
    NetwMultiplayer::entity_enter_tree(
        entity.ptr(),
        owner,
        entity->get_record(),
        nullptr,
        true
    );
    CHECK(entity.is_null());
    CHECK(netw::gd::object_of(wrapper_id) == nullptr);
    NETW_CHECK_EQ(log.count("stage"), 2);
}

} // namespace TestNetwStage
