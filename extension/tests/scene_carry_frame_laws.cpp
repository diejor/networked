#include "support/frame_drive.h"
#include "support/netw_test.h"

#include "godot/node.hpp"
#include "godot/physics_body.hpp"
#include "godot/scene_tree.hpp"
#include "netw/api/entity.hpp"
#include "netw/api/entity_options.hpp"
#include "netw/api/entity_record.hpp"
#include "netw/api/netw_multiplayer.hpp"
#include "netw/api/promise.hpp"
#include "netw/liveness_core.hpp"

#if defined(NETW_TIER_HOSTED)

using namespace godot;

namespace NetwTests {

namespace {

constexpr int CARRY_FRAME_CAP = 16;
constexpr int64_t MOVER_LAYER = 6;
constexpr int64_t MOVER_MASK = 3;

struct LandingEvidence {
    bool driven = false;
    bool built = false;
    bool settled_on_the_starting_frame = false;
    bool started_under_source = false;
    int mode_in_flight = 0;
    int64_t layer_in_flight = -1;
    int64_t mask_in_flight = -1;
    int frames_spent = 0;
    bool settled = false;
    bool failed = false;
    int code = -1;
    bool landed_under_target = false;
    int mode_after = 0;
    int64_t layer_after = -1;
    int64_t mask_after = -1;
};

struct AbandonEvidence {
    bool driven = false;
    bool settled_when_the_body_went = false;
    bool settled = false;
    bool failed = false;
    int code = -1;
    int frames_spent = 0;
};

struct CarryEvidence {
    LandingEvidence landing;
    AbandonEvidence abandon;
};

CarryEvidence &carry_evidence() {
    static CarryEvidence evidence;
    return evidence;
}

struct CarryRun {
    Ref<netw::NetwMultiplayer> core;
    Ref<netw::NetwPromise> settling;
    Ref<netw::NetwEntity> mover_entity;
    Node *root = nullptr;
    Node *source_level = nullptr;
    Node *target_level = nullptr;
    CharacterBody2D *mover = nullptr;
    RID mover_handle;
    RID target_handle;

    RID seat(Node *p_container) {
        const Ref<netw::NetwEntity> entity
            = netw::NetwEntity::ensure(p_container);
        const RID handle = core->get_liveness_core()->entity_create();
        entity->get_record()->adopt_handle(handle);
        core->liveness_bind(
            handle,
            core->get_liveness_core()->reserve_route(),
            entity,
            entity->get_record(),
            p_container
        );
        p_container->set_meta(
            netw::NetwMultiplayer::wrapper_meta(),
            Variant(entity)
        );
        return handle;
    }

    Node *level_named(const char *p_name) {
        Node *level = memnew(Node);
        level->set_name(p_name);
        root->add_child(level);
        return level;
    }

    CarryRun() {
        core.instantiate();
        root = memnew(Node);
        netw::gd::scene_root()->add_child(root);

        source_level = level_named("Source");
        target_level = level_named("Destination");
        target_handle = seat(target_level);

        mover = memnew(CharacterBody2D);
        mover->set_name("Crate");
        mover->set_collision_layer(MOVER_LAYER);
        mover->set_collision_mask(MOVER_MASK);
        source_level->add_child(mover);
        mover_handle = seat(mover);
        mover_entity = netw::NetwEntity::of(mover);
    }

    bool built() const {
        return mover_entity.is_valid() && mover_handle.is_valid()
            && target_handle.is_valid();
    }

    void start() {
        settling
            = core->scene_move_entity(mover_handle, target_handle, Variant());
    }

    void drop_mover() {
        mover->get_parent()->remove_child(mover);
        memdelete(mover);
        mover = nullptr;
    }

    ~CarryRun() {
        settling.unref();
        mover_entity.unref();
        if (mover != nullptr) {
            drop_mover();
        }
        core->scene_dispose();
        core.unref();
        netw::gd::scene_root()->remove_child(root);
        memdelete(root);
    }
};

class CarryScenario final : public netw_test::FrameScenario {
    CarryRun *live = nullptr;
    int run = 0;
    int step = 0;

    void close() {
        delete live;
        live = nullptr;
        step = 0;
        ++run;
    }

    bool drive_landing() {
        LandingEvidence &seen = carry_evidence().landing;
        if (step == 0) {
            live = new CarryRun();
            seen.driven = true;
            seen.built = live->built();
            live->start();
            seen.settled_on_the_starting_frame
                = live->settling->get_is_settled();
            seen.started_under_source
                = live->mover->get_parent() == live->source_level;
            seen.mode_in_flight = int(live->mover->get_process_mode());
            seen.layer_in_flight = int64_t(live->mover->get_collision_layer());
            seen.mask_in_flight = int64_t(live->mover->get_collision_mask());
            ++step;
            return true;
        }
        if (!live->settling->get_is_settled() && step < CARRY_FRAME_CAP) {
            ++step;
            return true;
        }
        seen.frames_spent = step;
        seen.settled = live->settling->get_is_settled();
        seen.failed = live->settling->get_is_failed();
        seen.code = live->settling->get_code();
        seen.landed_under_target
            = live->mover->get_parent() == live->target_level;
        seen.mode_after = int(live->mover->get_process_mode());
        seen.layer_after = int64_t(live->mover->get_collision_layer());
        seen.mask_after = int64_t(live->mover->get_collision_mask());
        close();
        return true;
    }

    bool drive_abandon() {
        AbandonEvidence &seen = carry_evidence().abandon;
        if (step == 0) {
            live = new CarryRun();
            live->start();
            seen.driven = true;
            ++step;
            return true;
        }
        if (step == 1) {
            live->drop_mover();
            seen.settled_when_the_body_went = live->settling->get_is_settled();
            ++step;
            return true;
        }
        if (!live->settling->get_is_settled() && step < CARRY_FRAME_CAP) {
            ++step;
            return true;
        }
        seen.frames_spent = step;
        seen.settled = live->settling->get_is_settled();
        seen.failed = live->settling->get_is_failed();
        seen.code = live->settling->get_code();
        close();
        return false;
    }

public:
    bool advance() override {
        if (netw::gd::scene_root() == nullptr) {
            return false;
        }
        if (run == 0) {
            return drive_landing();
        }
        if (run == 1) {
            return drive_abandon();
        }
        return false;
    }
};

NETW_FRAME_SCENARIO(CarryScenario, carry_scenario);

} // namespace

TEST_CASE(
    "[Networked][Scene][Frame] CF1 a move inside the tree stays unsettled on "
    "the frame it starts, because the physics server has not yet dropped the "
    "body from the areas it is leaving"
) {
    const LandingEvidence &seen = carry_evidence().landing;
    REQUIRE(seen.driven);
    REQUIRE(seen.built);
    CHECK_FALSE(seen.settled_on_the_starting_frame);
    CHECK(seen.started_under_source);
}

TEST_CASE(
    "[Networked][Scene][Frame] CF2 the carry holds the body out of the "
    "physics world while it is in flight, so the reparent generates no "
    "phantom area enter and exit pair"
) {
    const LandingEvidence &seen = carry_evidence().landing;
    REQUIRE(seen.driven);
    NETW_CHECK_EQ(seen.mode_in_flight, int(Node::PROCESS_MODE_DISABLED));
    NETW_CHECK_EQ(int(seen.layer_in_flight), 0);
    NETW_CHECK_EQ(int(seen.mask_in_flight), 0);
}

TEST_CASE(
    "[Networked][Scene][Frame] CF3 the carry lands the body under its "
    "destination within the frames it spends, and answers its promise there"
) {
    const LandingEvidence &seen = carry_evidence().landing;
    REQUIRE(seen.driven);
    CHECK(seen.settled);
    CHECK_FALSE(seen.failed);
    NETW_CHECK_EQ(seen.code, int(OK));
    CHECK(seen.landed_under_target);
    NETW_CHECK_LT(seen.frames_spent, CARRY_FRAME_CAP);
}

TEST_CASE(
    "[Networked][Scene][Frame] CF4 a landed carry gives the body back the "
    "process mode and the collision masks it was suppressing"
) {
    const LandingEvidence &seen = carry_evidence().landing;
    REQUIRE(seen.driven);
    NETW_CHECK_EQ(seen.mode_after, int(Node::PROCESS_MODE_INHERIT));
    NETW_CHECK_EQ(int(seen.layer_after), int(MOVER_LAYER));
    NETW_CHECK_EQ(int(seen.mask_after), int(MOVER_MASK));
}

TEST_CASE(
    "[Networked][Scene][Frame] CF5 a body freed mid-flight abandons its "
    "carry and answers the promise, so nothing dereferences it on the next "
    "frame and no move is left waiting forever"
) {
    const AbandonEvidence &seen = carry_evidence().abandon;
    REQUIRE(seen.driven);
    CHECK_FALSE(seen.settled_when_the_body_went);
    CHECK(seen.settled);
    CHECK(seen.failed);
    NETW_CHECK_EQ(seen.code, int(ERR_UNAVAILABLE));
    NETW_CHECK_LT(seen.frames_spent, CARRY_FRAME_CAP);
}

} // namespace NetwTests

#endif
