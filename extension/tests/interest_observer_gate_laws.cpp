#include "support/netw_test.h"

#include "godot/node.hpp"
#include "godot/scene_tree.hpp"
#include "netw/api/entity.hpp"
#include "netw/api/netw_multiplayer.hpp"
#include "netw/api/tests.hpp"
#include "netw/interest/relay.hpp"
#include "support/loopback_rig.h"

namespace TestNetwInterestObserverGate {

using namespace godot;
using netw::NetwEntity;
using netw::NetwMultiplayer;
using netw::NetwNativeTests;
using netw::interest::Awareness;

const int64_t WATCHER = 9;

struct Stand {
    netw_test::LoopbackRig rig{1};
    NetwMultiplayer *session = nullptr;
    Ref<NetwEntity> entity;
    Node *body = nullptr;
    int64_t owner = 0;

    void build() {
        rig.mount();
        rig.pump(4);
        session = Object::cast_to<NetwMultiplayer>(rig.server());
        REQUIRE(session != nullptr);
        owner = rig.peer_id(0);

        body = memnew(Node);
        rig.branch()->add_child(body);
        entity = NetwEntity::ensure(body);
        entity->set_peer_id(owner);
    }

    void tear_down() {
        entity = Ref<NetwEntity>();
        if (body != nullptr) {
            body->get_parent()->remove_child(body);
            memdelete(body);
            body = nullptr;
        }
    }

    void watched_by(int64_t p_peer) {
        NetwNativeTests::interest_queue_observer_awareness(
            session,
            StringName("sight"),
            entity.ptr(),
            p_peer,
            int(Awareness::ENTER)
        );
    }

    Array owed() {
        return NetwNativeTests::interest_awareness_drain(session);
    }
};

TEST_CASE(
    "[Networked][Interest][Observer] OG1 an owner is told who is watching it "
    "only when its own declaration asked to be told, so the awareness relay "
    "costs nothing for the entities that never opted in"
) {
    Stand stand;
    stand.build();

    NetwNativeTests::interest_set_report_observers(stand.entity.ptr(), false);
    stand.watched_by(WATCHER);
    NETW_CHECK_EQ(int(stand.owed().size()), 0);

    NetwNativeTests::interest_set_report_observers(stand.entity.ptr(), true);
    stand.watched_by(WATCHER);

    const Array told = stand.owed();
    NETW_CHECK_EQ(int(told.size()), 1);
    if (told.size() != 1) {
        stand.tear_down();
        return;
    }
    const Array addressed = told[0];
    NETW_CHECK_EQ(int(int64_t(addressed[0])), int(stand.owner));
    const Array rows = addressed[1];
    NETW_CHECK_EQ(int(rows.size()), 1);
    if (rows.size() != 1) {
        stand.tear_down();
        return;
    }
    const Array edge = rows[0];
    NETW_CHECK_EQ(int(int64_t(edge[0])), int(Awareness::OBSERVER));
    CHECK(StringName(edge[2]) == StringName("sight"));
    NETW_CHECK_EQ(int(int64_t(edge[3])), int(WATCHER));
    NETW_CHECK_EQ(int(int64_t(edge[4])), int(Awareness::ENTER));

    stand.tear_down();
}

TEST_CASE(
    "[Networked][Interest][Observer] OG2 an owner is never told that it is "
    "watching itself, because a self-echo would announce an observer the "
    "owner already is"
) {
    Stand stand;
    stand.build();
    NetwNativeTests::interest_set_report_observers(stand.entity.ptr(), true);

    stand.watched_by(stand.owner);
    NETW_CHECK_EQ(int(stand.owed().size()), 0);

    stand.watched_by(WATCHER);
    NETW_CHECK_EQ(int(stand.owed().size()), 1);

    stand.tear_down();
}

} // namespace TestNetwInterestObserverGate
