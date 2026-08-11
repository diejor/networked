// The scene record plane's laws.
//
// The trace beside this file pins what the GDScript record plane did. These are
// the properties that trace could not reach, and there is one reason for each:
// either the GDScript arm had no way to drive it, or driving it there would
// have meant reaching for state instead of behaviour.
//
// The re-entrancy case is the one worth reading twice. A callback that
// registers another callback during its own dispatch invalidates the row
// pointer the dispatch is walking, and in GDScript that was a Dictionary lookup
// per edge so the hazard did not exist. It exists here, and a case that did not
// state it would leave a use-after-free waiting for the first game that
// subscribes from inside an observer.

#include "support/netw_test.h"

#include "netw/scene_core.hpp"
#include "support/netw_call_log.h"

#include <memory>

#include "godot/rid.hpp"

namespace TestNetwSceneCore {

using namespace godot;
using netw::NetwSceneCore;
using netw_test::CallLog;

// The three events a registration is keyed under, by value rather than by name,
// because this tier cannot see the GDScript enum that owns them.
constexpr int EVENT_PARTICIPANT = 0;
constexpr int EVENT_PLAYER = 1;
constexpr int EVENT_ENTITY = 2;

Ref<NetwSceneCore> fresh() {
    Ref<NetwSceneCore> core;
    core.instantiate();
    return core;
}

// Distinct scene identities. NetwSceneCore mints none of its own — it is keyed
// by identities the liveness plane hands it — so a case gets them from an owner
// that can, and never asserts on their values.
class Scenes {
    RID_Owner<int> owner;
    LocalVector<RID> minted;

public:
    Scenes() {
        for (int index = 0; index < 4; ++index) {
            minted.push_back(owner.make_rid(index));
        }
    }

    RID operator[](int p_index) {
        REQUIRE(p_index < int(minted.size()));
        return minted[p_index];
    }
};

// A callback that registers ANOTHER observer while it is being dispatched.
//
// This is the hazard the GDScript original did not have. Its dispatch read a
// fresh Dictionary lookup per edge, so a registration during the walk was
// merely surprising. A contiguous row is different: growing the table moves it,
// and a walk holding a pointer into it would read freed memory. The case that
// uses this is the only thing standing between that and a crash in the first
// game that subscribes from inside an observer.
class ReentrantSink final : public CallableCustom {
    netw::NetwSceneCore *core;
    RID scene;
    int event;
    Callable guest;
    ObjectID anchor;
    std::shared_ptr<int> calls;

    static bool same(const CallableCustom *a, const CallableCustom *b) {
        return a == b;
    }

    static bool before(const CallableCustom *a, const CallableCustom *b) {
        return a < b;
    }

public:
    ReentrantSink(
        netw::NetwSceneCore *p_core,
        const RID &p_scene,
        int p_event,
        const Callable &p_guest,
        const Object *p_anchor,
        const std::shared_ptr<int> &p_calls
    )
        : core(p_core), scene(p_scene), event(p_event), guest(p_guest),
          anchor(netw::gd::instance_id(p_anchor)), calls(p_calls) {
    }

    uint32_t hash() const override {
        return uint32_t(uintptr_t(this));
    }

    String get_as_text() const override {
        return "ReentrantSink";
    }

    CompareEqualFunc get_compare_equal_func() const override {
        return &ReentrantSink::same;
    }

    CompareLessFunc get_compare_less_func() const override {
        return &ReentrantSink::before;
    }

    ObjectID get_object() const override {
        return anchor;
    }

    void call(
        const Variant **,
        int,
        Variant &r_return_value,
        netw::gd::CallError &r_call_error
    ) const override {
        *calls += 1;
        core->observe(scene, event, guest);
        r_return_value = Variant();
        netw::gd::call_ok(r_call_error);
    }
};

TEST_CASE(
    "[Networked][Scene][Hosted] N1 a registration is keyed by scene AND event"
) {
    Ref<NetwSceneCore> core = fresh();
    CallLog log;
    Scenes scenes;
    const RID first = scenes[0];
    const RID second = scenes[1];

    core->observe(first, EVENT_PLAYER, log.callable("heard"));

    NETW_CHECK_EQ(core->dispatch(first, EVENT_PLAYER, true, 0), 1);
    // The same scene under another event, and another scene under the same
    // event, are separate rows and neither hears it.
    NETW_CHECK_EQ(core->dispatch(first, EVENT_ENTITY, true, 0), 0);
    NETW_CHECK_EQ(core->dispatch(first, EVENT_PARTICIPANT, true, 0), 0);
    NETW_CHECK_EQ(core->dispatch(second, EVENT_PLAYER, true, 0), 0);
    NETW_CHECK_EQ(log.count("heard"), 1);
}

TEST_CASE(
    "[Networked][Scene][Hosted] N2 an invalid scene or an unset callback "
    "registers nothing"
) {
    Ref<NetwSceneCore> core = fresh();
    CallLog log;
    Scenes scenes;
    const RID scene = scenes[0];

    core->observe(RID(), EVENT_PLAYER, log.callable("heard"));
    core->observe(scene, EVENT_PLAYER, Callable());

    NETW_CHECK_EQ(core->observer_count(scene, EVENT_PLAYER), 0);
    NETW_CHECK_EQ(core->dispatch(scene, EVENT_PLAYER, true, 0), 0);
}

TEST_CASE(
    "[Networked][Scene][Hosted] N3 a duplicate registration is stored once and "
    "unobserving something absent is inert"
) {
    Ref<NetwSceneCore> core = fresh();
    CallLog log;
    Scenes scenes;
    const RID scene = scenes[0];
    const Callable heard = log.callable("heard");

    core->observe(scene, EVENT_PLAYER, heard);
    core->observe(scene, EVENT_PLAYER, heard);
    NETW_CHECK_EQ(core->observer_count(scene, EVENT_PLAYER), 1);

    core->unobserve(scene, EVENT_PLAYER, log.callable("never registered"));
    NETW_CHECK_EQ(core->observer_count(scene, EVENT_PLAYER), 1);

    core->unobserve(scene, EVENT_PLAYER, heard);
    NETW_CHECK_EQ(core->observer_count(scene, EVENT_PLAYER), 0);

    // Unobserving a scene that has no row at all must not create one.
    core->unobserve(scenes[2], EVENT_ENTITY, heard);
    NETW_CHECK_EQ(core->observer_count(scenes[2], EVENT_ENTITY), 0);
}

TEST_CASE(
    "[Networked][Scene][Hosted] N4 dispatch reaches every callback in "
    "registration order"
) {
    Ref<NetwSceneCore> core = fresh();
    CallLog log;
    Scenes scenes;
    const RID scene = scenes[0];

    core->observe(scene, EVENT_ENTITY, log.callable("first"));
    core->observe(scene, EVENT_ENTITY, log.callable("second"));
    core->observe(scene, EVENT_ENTITY, log.callable("third"));

    NETW_CHECK_EQ(core->dispatch(scene, EVENT_ENTITY, true, 42), 3);
    const Vector<StringName> order = log.order();
    NETW_CHECK_EQ(order.size(), 3);
    CHECK(bool(order[0] == StringName("first")));
    CHECK(bool(order[1] == StringName("second")));
    CHECK(bool(order[2] == StringName("third")));
}

TEST_CASE(
    "[Networked][Scene][Hosted] N5 forgetting a scene drops it under every "
    "event and leaves its neighbours alone"
) {
    Ref<NetwSceneCore> core = fresh();
    CallLog log;
    Scenes scenes;
    const RID doomed = scenes[0];
    const RID kept = scenes[1];

    core->observe(doomed, EVENT_PLAYER, log.callable("doomed player"));
    core->observe(doomed, EVENT_ENTITY, log.callable("doomed entity"));
    core->observe(kept, EVENT_PLAYER, log.callable("kept"));

    core->forget_scene(doomed);

    NETW_CHECK_EQ(core->observer_count(doomed, EVENT_PLAYER), 0);
    NETW_CHECK_EQ(core->observer_count(doomed, EVENT_ENTITY), 0);
    NETW_CHECK_EQ(core->observer_count(kept, EVENT_PLAYER), 1);
    NETW_CHECK_EQ(core->dispatch(kept, EVENT_PLAYER, true, 0), 1);
}

TEST_CASE(
    "[Networked][Scene][Hosted] N6 ids are drawn in sequence and only the live "
    "one is current"
) {
    Ref<NetwSceneCore> core = fresh();

    NETW_CHECK_EQ(core->get_pending_request_id(), 0);
    // Nothing is in flight, so no id is current, and zero never is.
    CHECK_FALSE(core->is_current(0));
    CHECK_FALSE(core->is_current(1));

    const int first = core->open_request();
    const int second = core->open_request();
    NETW_CHECK_EQ(second, first + 1);

    // Opening supersedes: the older id is no longer the one in flight.
    CHECK(core->is_current(second));
    CHECK_FALSE(core->is_current(first));

    core->close_request();
    CHECK_FALSE(core->is_current(second));
    NETW_CHECK_EQ(core->get_pending_request_id(), 0);
    // A closed request cannot be closed into currency again.
    CHECK_FALSE(core->is_current(0));
}

TEST_CASE(
    "[Networked][Scene][Hosted] N7 clearing forgets the routing table and "
    "restarts the counter"
) {
    Ref<NetwSceneCore> core = fresh();
    CallLog log;
    Scenes scenes;
    const RID scene = scenes[0];
    core->observe(scene, EVENT_PLAYER, log.callable("heard"));
    const int before = core->open_request();

    core->clear();

    NETW_CHECK_EQ(core->observer_count(scene, EVENT_PLAYER), 0);
    NETW_CHECK_EQ(core->get_pending_request_id(), 0);
    NETW_CHECK_EQ(core->open_request(), before);
}

TEST_CASE(
    "[Networked][Scene][Hosted] N8 a callback that registers during its own "
    "dispatch neither corrupts the walk nor loses what it registered"
) {
    Ref<NetwSceneCore> core = fresh();
    CallLog log;
    Scenes scenes;
    const RID scene = scenes[0];
    const RID other = scenes[1];

    // Rows enough that the registration below is certain to grow the vector
    // rather than fit in whatever it already reserved.
    for (int index = 0; index < 8; ++index) {
        core->observe(
            scenes[index % 4],
            EVENT_PARTICIPANT,
            log.callable("filler")
        );
    }

    auto calls = std::make_shared<int>(0);
    Ref<RefCounted> anchor;
    anchor.instantiate();
    const Callable guest = log.callable("guest");
    const Callable reentrant(memnew(ReentrantSink(
        core.ptr(),
        other,
        EVENT_ENTITY,
        guest,
        anchor.ptr(),
        calls
    )));

    core->observe(scene, EVENT_PLAYER, reentrant);
    core->observe(scene, EVENT_PLAYER, log.callable("after"));

    // The walk survives the table moving under it, and both callbacks run.
    NETW_CHECK_EQ(core->dispatch(scene, EVENT_PLAYER, true, 0), 2);
    NETW_CHECK_EQ(*calls, 1);
    NETW_CHECK_EQ(log.count("after"), 1);

    // What the callback registered is really there, on the row it named.
    NETW_CHECK_EQ(core->observer_count(other, EVENT_ENTITY), 1);
    NETW_CHECK_EQ(core->dispatch(other, EVENT_ENTITY, true, 0), 1);
    NETW_CHECK_EQ(log.count("guest"), 1);
}

TEST_CASE(
    "[Networked][Scene][Hosted] N9 a pruning dispatch keeps what was "
    "registered during it"
) {
    // The pruning write-back is entitled to drop what it found dead. It is not
    // entitled to roll the row back to the set it started with, which would
    // silently swallow any registration the walk itself provoked.
    Ref<NetwSceneCore> core = fresh();
    CallLog log;
    Scenes scenes;
    const RID scene = scenes[0];

    // A callback has to be ALIVE to register at all, so the one that dies dies
    // after it is in the row. Registering an already-dead callable is refused,
    // which is N2's law and not this one's.
    auto transient = std::make_unique<CallLog>();
    const Callable doomed = transient->callable("doomed");

    auto calls = std::make_shared<int>(0);
    Ref<RefCounted> anchor;
    anchor.instantiate();
    const Callable guest = log.callable("guest");
    const Callable reentrant(memnew(ReentrantSink(
        core.ptr(),
        scene,
        EVENT_PLAYER,
        guest,
        anchor.ptr(),
        calls
    )));

    core->observe(scene, EVENT_PLAYER, doomed);
    core->observe(scene, EVENT_PLAYER, reentrant);
    NETW_CHECK_EQ(core->observer_count(scene, EVENT_PLAYER), 2);

    transient.reset();
    CHECK_FALSE(doomed.is_valid());

    // One live callback ran, so one is reported, and the dead one is gone.
    NETW_CHECK_EQ(core->dispatch(scene, EVENT_PLAYER, true, 0), 1);
    NETW_CHECK_EQ(*calls, 1);

    // Two, not one: the guest the callback added mid-walk survived the prune
    // that was writing the row back at the time.
    NETW_CHECK_EQ(core->observer_count(scene, EVENT_PLAYER), 2);
    NETW_CHECK_EQ(core->dispatch(scene, EVENT_PLAYER, true, 0), 2);
    NETW_CHECK_EQ(log.count("guest"), 1);
}

} // namespace TestNetwSceneCore
