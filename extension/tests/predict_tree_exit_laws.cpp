#include "support/netw_test.h"

#include "support/fault_log.h"
#include "support/loopback_rig.h"

#if defined(NETW_TIER_HOSTED)

#include "godot/variant.hpp"
#include "netw/api/entity.hpp"
#include "netw/api/netw_multiplayer.hpp"
#include "netw/api/sync_pipeline.hpp"
#include "netw/predict/engine.hpp"

namespace TestNetwPredictTreeExitLaws {

using namespace godot;
using namespace netw_test;

constexpr int SERVER = -1;
constexpr int CLIENT = 0;

struct BookSizes {
    int64_t slots_open = 0;
    int64_t store_slots = 0;
    int64_t bindings = 0;
    int64_t derived_sets = 0;
};

struct ExitRig {
    LoopbackRig rig;

    ExitRig() : rig(1) {
        rig.mount();
        rig.declare_world(
            WorldDecl().clocked(30, 3).lag_compensated().player(
                EntityDecl()
                    .named("P")
                    .on_schema("PredictedPose")
                    .synced("position")
                    .placed_at(Vector2())
                    .predicted(0.01)
                    .scheduled(netw::Schedule::TICK)
                    .mounted(),
                CLIENT
            )
        );
        drive(10, Vector2(1.0, 0.0));
    }

    netw::NetwMultiplayer *api(int p_side) const {
        return p_side == SERVER ? rig.server() : rig.client(p_side);
    }

    Node *owner(int p_side) const {
        return rig.node_of(rig.entity_of("P", p_side), p_side);
    }

    int64_t slot(int p_side) const {
        Node *held = owner(p_side);
        return held != nullptr ? api(p_side)->get_prediction_engine()->slot_of(
                                     netw::NetwEntity::of(held)
                                 )
                               : int64_t(-1);
    }

    BookSizes books(int p_side) const {
        netw::NetwMultiplayer *session = api(p_side);
        BookSizes sizes;
        sizes.slots_open = session->get_prediction_engine()->open_count();
        sizes.store_slots = int64_t(session->store().live_slot_count());
        sizes.bindings = int64_t(session->property_binding_book().count());
        sizes.derived_sets
            = int64_t(session->sync_pipeline()
                          ->counters()[StringName("derived_sets_active")]);
        return sizes;
    }

    void drive(int p_ticks, const Vector2 &p_motion) {
        for (int at = 0; at < p_ticks; ++at) {
            if (Node *controlled = owner(CLIENT)) {
                controlled->set("motion", p_motion);
            }
            rig.step_ticks(1);
        }
    }

    double height(int p_side) const {
        const Vector2 at = owner(p_side)->get("position");
        return double(at.y);
    }
};

void check_empty(const BookSizes &p_sizes) {
    NETW_CHECK_EQ(p_sizes.slots_open, int64_t(0));
    NETW_CHECK_EQ(p_sizes.store_slots, int64_t(0));
    NETW_CHECK_EQ(p_sizes.bindings, int64_t(0));
    NETW_CHECK_EQ(p_sizes.derived_sets, int64_t(0));
}

void readded_in_one_frame_keeps_predicting(int p_side) {
    ExitRig stand;
    const int64_t seated = stand.slot(p_side);
    REQUIRE(seated >= 0);
    Node *owner = stand.owner(p_side);
    Node *parent = owner->get_parent();
    parent->remove_child(owner);
    parent->add_child(owner);
    owner->set_owner(parent);

    stand.drive(10, Vector2(0.0, 1.0));
    NETW_CHECK_EQ(stand.slot(p_side), seated);
    NETW_CHECK_EQ(stand.books(SERVER).slots_open, int64_t(1));
    NETW_CHECK_EQ(stand.books(CLIENT).slots_open, int64_t(1));
    const bool client_moved = stand.height(CLIENT) > 0.0;
    const bool server_moved = stand.height(SERVER) > 0.0;
    CHECK(client_moved);
    CHECK(server_moved);
}

void left_out_of_the_tree_releases_its_slot(int p_side) {
    ExitRig stand;
    const int other = p_side == SERVER ? CLIENT : SERVER;
    Node *owner = stand.owner(p_side);
    const FaultWatch faults;
    owner->get_parent()->remove_child(owner);

    stand.drive(5, Vector2(0.0, 1.0));
    NETW_CHECK_EQ(faults.errors(), int64_t(0));
    NETW_CHECK_EQ(stand.books(p_side).slots_open, int64_t(0));
    NETW_CHECK_EQ(stand.books(other).slots_open, int64_t(1));

    stand.api(p_side)->clear_session_state();
    stand.rig.step_ticks(1);
    check_empty(stand.books(p_side));
    memdelete(owner);
}

void freed_off_the_tree_releases_every_book(int p_side) {
    ExitRig stand;
    const int other = p_side == SERVER ? CLIENT : SERVER;
    Node *owner = stand.owner(p_side);
    const FaultWatch faults;
    owner->get_parent()->remove_child(owner);
    memdelete(owner);

    stand.drive(5, Vector2(0.0, 1.0));
    NETW_CHECK_EQ(faults.errors(), int64_t(0));
    check_empty(stand.books(p_side));
    NETW_CHECK_EQ(stand.books(other).slots_open, int64_t(1));
}

TEST_CASE(
    "[Networked][Predict][TreeExit] a server's predicted owner taken out of "
    "the tree and put back in the same frame keeps its slot and predicts on"
) {
    readded_in_one_frame_keeps_predicting(SERVER);
}

TEST_CASE(
    "[Networked][Predict][TreeExit] a client's predicted owner taken out of "
    "the tree and put back in the same frame keeps its slot and predicts on"
) {
    readded_in_one_frame_keeps_predicting(CLIENT);
}

TEST_CASE(
    "[Networked][Predict][TreeExit] a server's predicted owner left out of "
    "the tree releases its slot without an error, and a session clear "
    "releases the rest"
) {
    left_out_of_the_tree_releases_its_slot(SERVER);
}

TEST_CASE(
    "[Networked][Predict][TreeExit] a client's predicted owner left out of "
    "the tree releases its slot without an error, and a session clear "
    "releases the rest"
) {
    left_out_of_the_tree_releases_its_slot(CLIENT);
}

TEST_CASE(
    "[Networked][Predict][TreeExit] a server's predicted owner freed off the "
    "tree releases every book on the server without an error"
) {
    freed_off_the_tree_releases_every_book(SERVER);
}

TEST_CASE(
    "[Networked][Predict][TreeExit] a client's predicted owner freed off the "
    "tree releases every book on the client without an error"
) {
    freed_off_the_tree_releases_every_book(CLIENT);
}

} // namespace TestNetwPredictTreeExitLaws

#endif
