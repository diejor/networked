#include "support/fault_log.h"
#include "support/netw_test.h"

#include "netw/scene_core.hpp"
#include "support/minted_rids.h"
#include "support/netw_call_log.h"

#include <memory>

#include "godot/rid.hpp"
#include "godot/utility.hpp"

namespace TestNetwSceneCore {

using namespace godot;
using netw::NetwSceneCore;
using netw_test::CallLog;

constexpr int EVENT_PLAYER = 0;
constexpr int EVENT_PLAYER_ENTITY = 1;
constexpr int EVENT_ENTITY = 2;

Ref<NetwSceneCore> fresh() {
    Ref<NetwSceneCore> core;
    core.instantiate();
    return core;
}

class Scenes {
    netw_test::MintedRids owner;
    LocalVector<RID> minted;

public:
    Scenes() {
        for (int index = 0; index < 4; ++index) {
            minted.push_back(owner.mint(index));
        }
    }

    RID operator[](int p_index) {
        REQUIRE(p_index < int(minted.size()));
        return minted[p_index];
    }
};

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

    core->observe(first, EVENT_PLAYER_ENTITY, log.callable("heard"));

    NETW_CHECK_EQ(core->dispatch(first, EVENT_PLAYER_ENTITY, true, 0), 1);
    NETW_CHECK_EQ(core->dispatch(first, EVENT_ENTITY, true, 0), 0);
    NETW_CHECK_EQ(core->dispatch(first, EVENT_PLAYER, true, 0), 0);
    NETW_CHECK_EQ(core->dispatch(second, EVENT_PLAYER_ENTITY, true, 0), 0);
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

    core->observe(RID(), EVENT_PLAYER_ENTITY, log.callable("heard"));
    core->observe(scene, EVENT_PLAYER_ENTITY, Callable());

    NETW_CHECK_EQ(core->observer_count(scene, EVENT_PLAYER_ENTITY), 0);
    NETW_CHECK_EQ(core->dispatch(scene, EVENT_PLAYER_ENTITY, true, 0), 0);
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

    core->observe(scene, EVENT_PLAYER_ENTITY, heard);
    core->observe(scene, EVENT_PLAYER_ENTITY, heard);
    NETW_CHECK_EQ(core->observer_count(scene, EVENT_PLAYER_ENTITY), 1);

    core->unobserve(
        scene,
        EVENT_PLAYER_ENTITY,
        log.callable("never registered")
    );
    NETW_CHECK_EQ(core->observer_count(scene, EVENT_PLAYER_ENTITY), 1);

    core->unobserve(scene, EVENT_PLAYER_ENTITY, heard);
    NETW_CHECK_EQ(core->observer_count(scene, EVENT_PLAYER_ENTITY), 0);

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

    core->observe(doomed, EVENT_PLAYER_ENTITY, log.callable("doomed player"));
    core->observe(doomed, EVENT_ENTITY, log.callable("doomed entity"));
    core->observe(kept, EVENT_PLAYER_ENTITY, log.callable("kept"));

    core->forget_scene(doomed);

    NETW_CHECK_EQ(core->observer_count(doomed, EVENT_PLAYER_ENTITY), 0);
    NETW_CHECK_EQ(core->observer_count(doomed, EVENT_ENTITY), 0);
    NETW_CHECK_EQ(core->observer_count(kept, EVENT_PLAYER_ENTITY), 1);
    NETW_CHECK_EQ(core->dispatch(kept, EVENT_PLAYER_ENTITY, true, 0), 1);
}

TEST_CASE("[Networked][Scene][Hosted] N7 clearing forgets scene observers") {
    Ref<NetwSceneCore> core = fresh();
    CallLog log;
    Scenes scenes;
    const RID scene = scenes[0];
    core->observe(scene, EVENT_PLAYER_ENTITY, log.callable("heard"));

    core->clear();

    NETW_CHECK_EQ(core->observer_count(scene, EVENT_PLAYER_ENTITY), 0);
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

    const int rows_enough_to_force_growth = 8;
    for (int index = 0; index < rows_enough_to_force_growth; ++index) {
        core->observe(scenes[index % 4], EVENT_PLAYER, log.callable("filler"));
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

    core->observe(scene, EVENT_PLAYER_ENTITY, reentrant);
    core->observe(scene, EVENT_PLAYER_ENTITY, log.callable("after"));

    NETW_CHECK_EQ(core->dispatch(scene, EVENT_PLAYER_ENTITY, true, 0), 2);
    NETW_CHECK_EQ(*calls, 1);
    NETW_CHECK_EQ(log.count("after"), 1);

    NETW_CHECK_EQ(core->observer_count(other, EVENT_ENTITY), 1);
    NETW_CHECK_EQ(core->dispatch(other, EVENT_ENTITY, true, 0), 1);
    NETW_CHECK_EQ(log.count("guest"), 1);
}

TEST_CASE(
    "[Networked][Scene][Hosted] N9 a pruning dispatch keeps what was "
    "registered during it"
) {
    Ref<NetwSceneCore> core = fresh();
    CallLog log;
    Scenes scenes;
    const RID scene = scenes[0];

    auto transient = std::make_unique<CallLog>();
    const Callable doomed = transient->callable("doomed");

    auto calls = std::make_shared<int>(0);
    Ref<RefCounted> anchor;
    anchor.instantiate();
    const Callable guest = log.callable("guest");
    const Callable reentrant(memnew(ReentrantSink(
        core.ptr(),
        scene,
        EVENT_PLAYER_ENTITY,
        guest,
        anchor.ptr(),
        calls
    )));

    core->observe(scene, EVENT_PLAYER_ENTITY, doomed);
    core->observe(scene, EVENT_PLAYER_ENTITY, reentrant);
    NETW_CHECK_EQ(core->observer_count(scene, EVENT_PLAYER_ENTITY), 2);

    transient.reset();
    CHECK_FALSE(doomed.is_valid());

    NETW_CHECK_EQ(core->dispatch(scene, EVENT_PLAYER_ENTITY, true, 0), 1);
    NETW_CHECK_EQ(*calls, 1);

    NETW_CHECK_EQ(core->observer_count(scene, EVENT_PLAYER_ENTITY), 2);
    NETW_CHECK_EQ(core->dispatch(scene, EVENT_PLAYER_ENTITY, true, 0), 2);
    NETW_CHECK_EQ(log.count("guest"), 1);
}

TEST_CASE(
    "[Networked][Scene][Hosted] N13 a stem answers an instance, and the book "
    "answers every one of them"
) {
    Ref<NetwSceneCore> core = fresh();
    Scenes scenes;
    const StringName arena("Arena");

    core->scene_enter(scenes[0], arena, false);
    core->scene_enter(scenes[1], arena, false);
    core->scene_enter(scenes[2], StringName("Annex"), false);

    NETW_CHECK_EQ(core->live_count(), 3);
    CHECK(core->scene_named(arena) == scenes[1]);
    CHECK(core->scene_named(StringName("Nothing")) == RID());

    const Array instances = core->scenes_named(arena);
    REQUIRE(instances.size() == 2);
    CHECK(RID(instances[0]) == scenes[0]);
    CHECK(RID(instances[1]) == scenes[1]);

    const Array every = core->live_scenes();
    REQUIRE(every.size() == 3);
    CHECK(RID(every[0]) == scenes[0]);
    CHECK(RID(every[2]) == scenes[2]);
    CHECK(core->stem_of(scenes[2]) == StringName("Annex"));
    CHECK(core->is_live(scenes[0]));
}

TEST_CASE(
    "[Networked][Scene][Hosted] N14 a surviving sibling keeps its stem "
    "answerable"
) {
    Ref<NetwSceneCore> core = fresh();
    Scenes scenes;
    const StringName arena("Arena");

    core->scene_enter(scenes[0], arena, false);
    core->scene_enter(scenes[1], arena, false);
    CHECK(core->scene_named(arena) == scenes[1]);

    core->scene_exit(scenes[1]);
    CHECK(core->scene_named(arena) == scenes[0]);
    CHECK_FALSE(core->is_live(scenes[1]));

    core->scene_exit(scenes[0]);
    CHECK(core->scene_named(arena) == RID());
    NETW_CHECK_EQ(core->live_count(), 0);
    CHECK(core->stem_of(scenes[0]) == StringName());
}

TEST_CASE(
    "[Networked][Scene][Hosted] N21 a peer admitted before its roster row is "
    "parked once, and unparked once, so the retry reports one arrival"
) {
    Ref<NetwSceneCore> core = fresh();
    Scenes scenes;
    const RID scene = scenes[0];
    core->scene_enter(scene, "Arena", false);

    NETW_CHECK_EQ(core->admission_is_parked(scene, 7), false);
    NETW_CHECK_EQ(core->admission_park(scene, 7), true);
    NETW_CHECK_EQ(core->admission_is_parked(scene, 7), true);

    NETW_CHECK_EQ(core->admission_park(scene, 7), false);

    NETW_CHECK_EQ(core->admission_unpark(scene, 7), true);
    NETW_CHECK_EQ(core->admission_is_parked(scene, 7), false);
    NETW_CHECK_EQ(core->admission_unpark(scene, 7), false);
}

TEST_CASE(
    "[Networked][Scene][Hosted] N22 a scene that is not live parks nobody, so "
    "an admission cannot outlive the scene it was made against"
) {
    Ref<NetwSceneCore> core = fresh();
    Scenes scenes;
    const RID live_scene = scenes[0];
    const RID never_live = scenes[1];

    NETW_CHECK_EQ(core->admission_park(never_live, 7), false);
    NETW_CHECK_EQ(core->admission_is_parked(never_live, 7), false);

    core->scene_enter(live_scene, "Arena", false);
    core->admission_park(live_scene, 7);
    core->scene_exit(live_scene);

    NETW_CHECK_EQ(core->admission_is_parked(live_scene, 7), false);
    core->scene_enter(live_scene, "Arena", false);
    NETW_CHECK_EQ(core->admission_is_parked(live_scene, 7), false);
}

TEST_CASE(
    "[Networked][Scene][Hosted] N23 two scenes park their own peers, and two "
    "peers park separately in one scene"
) {
    Ref<NetwSceneCore> core = fresh();
    Scenes scenes;
    const RID first = scenes[0];
    const RID second = scenes[1];
    core->scene_enter(first, "Arena", false);
    core->scene_enter(second, "Arena", false);

    core->admission_park(first, 7);
    core->admission_park(first, 3);
    core->admission_park(second, 7);

    NETW_CHECK_EQ(core->admission_is_parked(second, 3), false);

    NETW_CHECK_EQ(core->admission_unpark(first, 7), true);
    NETW_CHECK_EQ(core->admission_is_parked(first, 3), true);
    NETW_CHECK_EQ(core->admission_is_parked(second, 7), true);
}

} // namespace TestNetwSceneCore
