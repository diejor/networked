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
#include "support/netw_recorder.h"

#include <memory>

#include "godot/rid.hpp"
#include "godot/utility.hpp"

namespace TestNetwSceneCore {

using namespace godot;
using netw::NetwSceneCore;
using netw_test::CallLog;
using netw_test::Recorder;

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
    "[Networked][Scene][Hosted] N17 one scene declaring its own world is what "
    "stops the session sharing one"
) {
    Ref<NetwSceneCore> core = fresh();
    Scenes scenes;

    CHECK_FALSE(core->would_share_one_world());

    core->scene_enter(scenes[0], StringName("Arena"), false);
    CHECK(core->would_share_one_world());

    core->scene_enter(scenes[1], StringName("Annex"), true);
    CHECK_FALSE(core->would_share_one_world());

    core->scene_exit(scenes[1]);
    CHECK(core->would_share_one_world());

    core->scene_enter(scenes[0], StringName("Arena"), true);
    CHECK_FALSE(core->would_share_one_world());
}

TEST_CASE(
    "[Networked][Scene][Hosted] N15 a retired scene leaves the book at once "
    "and its wrapper at the end of the drain"
) {
    Ref<NetwSceneCore> core = fresh();
    Scenes scenes;

    core->scene_enter(scenes[0], StringName("Arena"), false);
    core->scene_retire(scenes[0], 2);

    CHECK_FALSE(core->is_live(scenes[0]));
    CHECK(core->scene_named(StringName("Arena")) == RID());
    REQUIRE(core->retiring_scenes().size() == 1);

    CHECK(core->pump_retired().is_empty());
    const Array closed = core->pump_retired();
    REQUIRE(closed.size() == 1);
    CHECK(RID(closed[0]) == scenes[0]);
    CHECK(core->retiring_scenes().is_empty());
    CHECK(core->pump_retired().is_empty());

    core->scene_enter(scenes[1], StringName("Annex"), false);
    core->scene_retire(scenes[1], 0);
    REQUIRE(core->pump_retired().size() == 1);
}

TEST_CASE(
    "[Networked][Scene][Hosted] N10 a requested path is bounded to a scene "
    "file under res://"
) {
    CHECK(
        NetwSceneCore::verify_requested_path("res://a/level.tscn")
        == String("res://a/level.tscn")
    );
    CHECK(
        NetwSceneCore::verify_requested_path("res://a/level.scn")
        == String("res://a/level.scn")
    );
    CHECK(NetwSceneCore::verify_requested_path("res://a/level.gd").is_empty());
    CHECK(NetwSceneCore::verify_requested_path("user://a/level.tscn").is_empty()
    );
    CHECK(NetwSceneCore::verify_requested_path("").is_empty());
    CHECK(NetwSceneCore::verify_requested_path("res://a/level").is_empty());

    String overlong = "res://";
    while (overlong.length() <= NetwSceneCore::MAX_REQUESTED_PATH_LENGTH) {
        overlong += "a";
    }
    CHECK(NetwSceneCore::verify_requested_path(overlong + ".tscn").is_empty());
}

TEST_CASE(
    "[Networked][Scene][Hosted] N11 the mark is the consent line a request "
    "clears"
) {
    const String path = "res://a/level.tscn";

    CHECK(NetwSceneCore::admits_request(true, false, false, path));
    CHECK(NetwSceneCore::admits_request(false, true, false, path));

    CHECK_FALSE(NetwSceneCore::admits_request(false, false, false, path));
    CHECK_FALSE(NetwSceneCore::admits_request(true, true, true, path));
    CHECK_FALSE(NetwSceneCore::admits_request(true, true, false, ""));
}

int entry_verdict(int p_bits, bool p_has_path, bool p_is_server) {
    return NetwSceneCore::native_entry_verdict(
        p_has_path,
        p_is_server,
        (p_bits & 1) != 0,
        (p_bits & 2) != 0,
        (p_bits & 4) != 0,
        (p_bits & 8) != 0,
        (p_bits & 16) != 0
    );
}

TEST_CASE(
    "[Networked][Scene][Hosted] how far a request reaches is server policy, "
    "and a reach naming nothing is refused"
) {
    Ref<NetwSceneCore> core = fresh();

    NETW_CHECK_EQ(
        core->get_request_reach(),
        int(NetwSceneCore::REACH_PARTICIPANT)
    );

    CHECK(core->set_request_reach(NetwSceneCore::REACH_SESSION));
    NETW_CHECK_EQ(
        core->get_request_reach(),
        int(NetwSceneCore::REACH_SESSION)
    );

    SUBCASE("an ordinal outside the enum names nothing and is refused") {
        ERR_PRINT_OFF;
        CHECK_FALSE(core->set_request_reach(NetwSceneCore::REACH_MAX));
        CHECK_FALSE(core->set_request_reach(-1));
        ERR_PRINT_ON;
        // The refusal leaves what was there, rather than storing a reach a
        // request would later be measured against.
        NETW_CHECK_EQ(
            core->get_request_reach(),
            int(NetwSceneCore::REACH_SESSION)
        );
    }
}

TEST_CASE(
    "[Networked][Scene][Hosted] N16 a marked native entry reads one verdict "
    "from the local presentation"
) {
    for (int bits = 0; bits < 32; ++bits) {
        NETW_CHECK_EQ(
            entry_verdict(bits, false, true),
            int(NetwSceneCore::CAPTURE_REFUSED)
        );
        NETW_CHECK_EQ(
            entry_verdict(bits, false, false),
            int(NetwSceneCore::CAPTURE_REFUSED)
        );
        NETW_CHECK_EQ(
            entry_verdict(bits, true, false),
            int(NetwSceneCore::CAPTURE_REQUEST)
        );
    }

    NETW_CHECK_EQ(
        NetwSceneCore::native_entry_verdict(
            true,
            true,
            true,
            true,
            true,
            true,
            true
        ),
        int(NetwSceneCore::CAPTURE_CHANGE_SESSION)
    );
    NETW_CHECK_EQ(
        NetwSceneCore::native_entry_verdict(
            true,
            true,
            true,
            false,
            false,
            false,
            false
        ),
        int(NetwSceneCore::CAPTURE_ACTIVATE)
    );

    NETW_CHECK_EQ(
        NetwSceneCore::native_entry_verdict(
            true,
            true,
            false,
            true,
            true,
            true,
            true
        ),
        int(NetwSceneCore::CAPTURE_MOVE_ME)
    );
    NETW_CHECK_EQ(
        NetwSceneCore::native_entry_verdict(
            true,
            true,
            false,
            true,
            false,
            true,
            true
        ),
        int(NetwSceneCore::CAPTURE_ACTIVATE)
    );
    NETW_CHECK_EQ(
        NetwSceneCore::native_entry_verdict(
            true,
            true,
            false,
            true,
            true,
            false,
            true
        ),
        int(NetwSceneCore::CAPTURE_ACTIVATE)
    );
    NETW_CHECK_EQ(
        NetwSceneCore::native_entry_verdict(
            true,
            true,
            false,
            true,
            true,
            true,
            false
        ),
        int(NetwSceneCore::CAPTURE_ACTIVATE)
    );
    NETW_CHECK_EQ(
        NetwSceneCore::native_entry_verdict(
            true,
            true,
            false,
            false,
            false,
            false,
            false
        ),
        int(NetwSceneCore::CAPTURE_ACTIVATE)
    );
}

TEST_CASE(
    "[Networked][Scene][Hosted] N12 settling an answer settles the request it "
    "answered, and only it"
) {
    Ref<NetwSceneCore> core = fresh();
    const Ref<netw::NetwPromise> promise = core->request_open(false);
    const int opened = core->get_pending_request_id();
    REQUIRE(promise.is_valid());

    CHECK_FALSE(core->request_settle(opened + 1, int(OK)));
    CHECK_FALSE(promise->get_is_settled());
    NETW_CHECK_EQ(core->get_pending_request_id(), opened);

    CHECK(core->request_settle(opened, int(ERR_TIMEOUT)));
    CHECK(promise->get_is_failed());
    NETW_CHECK_EQ(promise->get_code(), int(ERR_TIMEOUT));
    NETW_CHECK_EQ(core->get_pending_request_id(), 0);
    CHECK(core->get_pending_request().is_null());

    CHECK_FALSE(core->request_settle(opened, int(OK)));
    CHECK_FALSE(core->request_settle(0, int(OK)));
    NETW_CHECK_EQ(promise->get_code(), int(ERR_TIMEOUT));
}

TEST_CASE(
    "[Networked][Scene][Hosted] N18 a newer request supersedes the one still "
    "in flight, and an answer resolves rather than rejects"
) {
    Ref<NetwSceneCore> core = fresh();
    const Ref<netw::NetwPromise> first = core->request_open(false);
    first->catch_error(Callable());

    const Ref<netw::NetwPromise> second = core->request_open(false);

    CHECK(first->get_is_failed());
    NETW_CHECK_EQ(first->get_code(), int(ERR_SKIP));
    CHECK_FALSE(second->get_is_settled());
    CHECK(second != first);

    CHECK(core->request_settle(core->get_pending_request_id(), int(OK)));
    CHECK(second->get_is_completed());
    NETW_CHECK_EQ(second->get_code(), int(OK));

    const Ref<netw::NetwPromise> third = core->request_open(false);
    core->request_abandon(int(ERR_UNAVAILABLE));
    CHECK(third->get_is_failed());
    NETW_CHECK_EQ(third->get_code(), int(ERR_UNAVAILABLE));
    CHECK(core->get_pending_request().is_null());
}

TEST_CASE(
    "[Networked][Scene][Hosted] N19 only the server authors a scene result, "
    "and only a well-formed one settles"
) {
    Ref<NetwSceneCore> core = fresh();
    const Ref<netw::NetwPromise> promise = core->request_open(false);
    const int opened = core->get_pending_request_id();

    Array answer;
    answer.push_back(opened);
    answer.push_back(int(ERR_UNAUTHORIZED));
    const PackedByteArray payload = netw::gd::var_to_bytes(answer);

    CHECK_FALSE(core->receive_result_frame(payload, 2));
    CHECK_FALSE(promise->get_is_settled());
    NETW_CHECK_EQ(core->get_pending_request_id(), opened);

    Array short_row;
    short_row.push_back(opened);
    CHECK_FALSE(core->receive_result_frame(
        netw::gd::var_to_bytes(short_row),
        1
    ));
    CHECK_FALSE(core->receive_result_frame(
        netw::gd::var_to_bytes(Variant(7)),
        1
    ));
    CHECK_FALSE(promise->get_is_settled());

    CHECK(core->receive_result_frame(payload, 1));
    CHECK(promise->get_is_failed());
    NETW_CHECK_EQ(promise->get_code(), int(ERR_UNAUTHORIZED));
}

TEST_CASE(
    "[Networked][Scene][Hosted] N20 a request opened from a capture announces "
    "its outcome, and one that was not stays quiet"
) {
    Ref<NetwSceneCore> core = fresh();
    Recorder announced(
        core.ptr(),
        Vector<StringName>({"native_change_settled"})
    );

    core->request_open(false);
    core->request_settle(core->get_pending_request_id(), int(OK));
    NETW_CHECK_EQ(announced.count("native_change_settled"), 0);

    const Ref<netw::NetwPromise> captured = core->request_open(true);
    captured->catch_error(Callable());
    core->request_settle(core->get_pending_request_id(), int(ERR_TIMEOUT));

    NETW_CHECK_EQ(announced.count("native_change_settled"), 1);
    REQUIRE(announced.args("native_change_settled").size() == 1);
    NETW_CHECK_EQ(
        int(announced.args("native_change_settled")[0]),
        int(ERR_TIMEOUT)
    );

    core->request_open(false);
    core->request_abandon(int(ERR_UNAVAILABLE));
    NETW_CHECK_EQ(announced.count("native_change_settled"), 1);
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

    // Parking twice is the same admission arriving twice, not two of them.
    NETW_CHECK_EQ(core->admission_park(scene, 7), false);

    NETW_CHECK_EQ(core->admission_unpark(scene, 7), true);
    NETW_CHECK_EQ(core->admission_is_parked(scene, 7), false);
    // The second retry has nothing to report, which is what stops one arrival
    // being announced twice.
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

    // The parked peer left with the row it rode, so re-entering under the same
    // identity starts empty rather than retrying a peer into a scene that has
    // been rebuilt since.
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
    // Unparking one peer leaves the other standing, and the same peer parked
    // against a sibling instance is a separate admission.
    NETW_CHECK_EQ(core->admission_is_parked(first, 3), true);
    NETW_CHECK_EQ(core->admission_is_parked(second, 7), true);
}

TEST_CASE(
    "[Networked][Scene][Hosted] N24 a second level in one physics space is "
    "warned about and never refused, because sharing a world is legal"
) {
    Ref<NetwSceneCore> core = fresh();
    Scenes scenes;

    // Nothing live, so nothing to share with.
    NETW_CHECK_EQ(core->spawn_warns_shared_world(), false);

    core->scene_enter(scenes[0], "Arena", false);
    NETW_CHECK_EQ(core->spawn_warns_shared_world(), true);

    // The book carries no refusal at all: the only thing this decides is
    // whether the session says something, which is why a second shared-world
    // scene is admitted rather than rejected.
    ERR_PRINT_OFF;
    core->spawn_note("Annex");
    ERR_PRINT_ON;
    NETW_CHECK_EQ(core->live_count(), 1);
    NETW_CHECK_EQ(core->spawn_warns_shared_world(), true);

    // A scene that declared its own world is the one that says it meant it.
    core->scene_enter(scenes[1], "Annex", true);
    NETW_CHECK_EQ(core->spawn_warns_shared_world(), false);
}

TEST_CASE(
    "[Networked][Scene][Hosted] N25 a session replacing its scene expects two "
    "live at once, so the replacement is not warned about"
) {
    Ref<NetwSceneCore> core = fresh();
    Scenes scenes;
    core->scene_enter(scenes[0], "Arena", false);
    NETW_CHECK_EQ(core->spawn_warns_shared_world(), true);

    core->set_replacing(true);
    NETW_CHECK_EQ(core->is_replacing(), true);
    // The incoming scene stands beside the one it is about to replace, and
    // warning about that would fire on every scene change a session makes.
    NETW_CHECK_EQ(core->spawn_warns_shared_world(), false);

    core->set_replacing(false);
    NETW_CHECK_EQ(core->spawn_warns_shared_world(), true);
}

TEST_CASE(
    "[Networked][Scene][Hosted] N26 a native scene change strands this peer "
    "only while a session is live and the scene is not one it agreed to"
) {
    // Offline, the native pointer is plain local UI: a menu or a boot scene,
    // and nothing about it leaves a game this peer is not in.
    NETW_CHECK_EQ(NetwSceneCore::native_change_strands(false, false), false);
    NETW_CHECK_EQ(NetwSceneCore::native_change_strands(false, true), false);

    // Online and marked is the change going through the front door, which is
    // what marking a scene buys.
    NETW_CHECK_EQ(NetwSceneCore::native_change_strands(true, true), false);

    // Online and unmarked is the only stranding cell: the replicated session
    // is intact and this peer has locally left the game it presents.
    NETW_CHECK_EQ(NetwSceneCore::native_change_strands(true, false), true);
}

TEST_CASE(
    "[Networked][Scene][Hosted] N27 a whole-session transition claims one slot, "
    "and a second entered while it is held is refused rather than interleaved"
) {
    Ref<NetwSceneCore> core;
    core.instantiate();

    CHECK(core->transition_open());

    // Claiming and asking are one act, so a caller cannot read the slot free
    // and then take it after another caller already has.
    CHECK_FALSE(core->transition_open());
    CHECK_FALSE(core->transition_open());

    core->transition_close();
    CHECK(core->transition_open());

    // Closing is idempotent, because the transition unwinds down several
    // paths and each of them closes the slot it may not have opened.
    core->transition_close();
    core->transition_close();
    CHECK(core->transition_open());
}

} // namespace TestNetwSceneCore
