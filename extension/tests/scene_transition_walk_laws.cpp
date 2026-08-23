#include "support/netw_call_log.h"

#include "netw/scene_core.hpp"

namespace TestSceneTransitionWalkLaws {

using namespace godot;
using netw::NetwPromise;
using netw::NetwSceneCore;
using netw_test::CallLog;

StringName roster_tag() {
    return StringName("roster");
}

Array two_sources() {
    Array out;
    out.push_back(StringName("A"));
    out.push_back(StringName("B"));
    return out;
}

Ref<NetwSceneCore> armed(const Ref<NetwPromise> &p_promise) {
    Ref<NetwSceneCore> core;
    core.instantiate();
    CHECK(core->transition_open());
    core->transition_arm(StringName("Arena"), two_sources(), p_promise);
    return core;
}

TEST_CASE(
    "[Networked][Scene][Hosted] ST1 every exit from a transition releases the "
    "one slot, so a landed or a failed change never holds the next one shut"
) {
    Ref<NetwPromise> landed;
    landed.instantiate();
    const Ref<NetwSceneCore> core = armed(landed);

    CHECK_FALSE(core->transition_open());
    core->transition_land();
    CHECK(landed->get_is_completed());

    Ref<NetwPromise> refused;
    refused.instantiate();
    CHECK(core->transition_open());
    core->transition_arm(StringName("Annex"), two_sources(), refused);
    CHECK_FALSE(core->transition_open());
    core->transition_fail(int(ERR_UNAUTHORIZED));
    NETW_CHECK_EQ(refused->get_code(), int(ERR_UNAUTHORIZED));

    CHECK(core->transition_open());
}

TEST_CASE(
    "[Networked][Scene][Hosted] ST2 a source's roster is read when the walk "
    "reaches that source, never flattened ahead of it"
) {
    CallLog log;
    Array players;
    players.push_back(StringName("p1"));
    players.push_back(StringName("p2"));
    const Callable roster_of = log.answering(roster_tag(), players);

    Ref<NetwPromise> promise;
    promise.instantiate();
    const Ref<NetwSceneCore> core = armed(promise);

    NETW_CHECK_EQ(log.count(roster_tag()), 0);

    CHECK(bool(
        core->transition_next_mover(roster_of) == Variant(StringName("p1"))
    ));
    NETW_CHECK_EQ(log.count(roster_tag()), 1);
    CHECK(bool(
        log.args(roster_tag(), 0)[0] == Variant(StringName("A"))
    ));

    CHECK(bool(
        core->transition_next_mover(roster_of) == Variant(StringName("p2"))
    ));
    NETW_CHECK_EQ(log.count(roster_tag()), 1);

    CHECK(bool(
        core->transition_next_mover(roster_of) == Variant(StringName("p1"))
    ));
    NETW_CHECK_EQ(log.count(roster_tag()), 2);
    CHECK(bool(
        log.args(roster_tag(), 1)[0] == Variant(StringName("B"))
    ));

    core->transition_next_mover(roster_of);
    NETW_CHECK_EQ(
        int(core->transition_next_mover(roster_of).get_type()),
        int(Variant::NIL)
    );
    NETW_CHECK_EQ(log.count(roster_tag()), 2);
}

TEST_CASE(
    "[Networked][Scene][Hosted] ST3 a source holding no players hands the "
    "walk on to the next source rather than ending it"
) {
    CallLog log;
    const Callable roster_of = log.answering(roster_tag(), Array());

    Ref<NetwPromise> promise;
    promise.instantiate();
    const Ref<NetwSceneCore> core = armed(promise);

    NETW_CHECK_EQ(
        int(core->transition_next_mover(roster_of).get_type()),
        int(Variant::NIL)
    );
    NETW_CHECK_EQ(log.count(roster_tag()), 2);
}

TEST_CASE(
    "[Networked][Scene][Hosted] ST4 the first mover that fails ends the whole "
    "transition, and it ends UNAVAILABLE whatever the mover answered"
) {
    CallLog log;
    Array players;
    players.push_back(StringName("p1"));
    const Callable roster_of = log.answering(roster_tag(), players);

    Ref<NetwPromise> promise;
    promise.instantiate();
    const Ref<NetwSceneCore> core = armed(promise);

    CHECK(core->transition_accept(int(OK), 7));
    CHECK_FALSE(core->transition_accept(int(ERR_BUSY), 8));

    CHECK(promise->get_is_failed());
    NETW_CHECK_EQ(promise->get_code(), int(ERR_UNAVAILABLE));
    CHECK_FALSE(core->transition_moved(7));
    NETW_CHECK_EQ(
        int(core->transition_next_mover(roster_of).get_type()),
        int(Variant::NIL)
    );
    NETW_CHECK_EQ(log.count(roster_tag()), 0);
    CHECK(core->transition_open());
}

TEST_CASE(
    "[Networked][Scene][Hosted] ST5 a landing that cannot admit everyone "
    "fails with the admission's own code rather than half-landing"
) {
    Ref<NetwPromise> promise;
    promise.instantiate();
    const Ref<NetwSceneCore> core = armed(promise);

    CHECK(core->transition_accept(int(OK), 7));
    CHECK(core->transition_moved(7));

    core->transition_fail(int(ERR_UNAUTHORIZED));

    CHECK(promise->get_is_failed());
    NETW_CHECK_EQ(promise->get_code(), int(ERR_UNAUTHORIZED));
    NETW_CHECK_EQ(int(core->transition_sources().size()), 0);
    CHECK(core->transition_open());
}

TEST_CASE(
    "[Networked][Scene][Hosted] ST6 a landing answers once and drops the "
    "record the walk read, target, sources and carried peers together"
) {
    Ref<NetwPromise> promise;
    promise.instantiate();
    const Ref<NetwSceneCore> core = armed(promise);

    CHECK(bool(core->transition_target() == Variant(StringName("Arena"))));
    NETW_CHECK_EQ(int(core->transition_sources().size()), 2);
    CHECK(core->transition_accept(int(OK), 7));
    CHECK(core->transition_moved(7));
    CHECK_FALSE(core->transition_moved(8));

    core->transition_land();

    CHECK(promise->get_is_completed());
    NETW_CHECK_EQ(promise->get_code(), int(OK));
    NETW_CHECK_EQ(
        int(core->transition_target().get_type()),
        int(Variant::NIL)
    );
    NETW_CHECK_EQ(int(core->transition_sources().size()), 0);
    CHECK_FALSE(core->transition_moved(7));
}

TEST_CASE(
    "[Networked][Scene][Hosted] ST7 arming a second transition starts its "
    "walk at the first source, so no cursor survives the change before it"
) {
    CallLog log;
    Array players;
    players.push_back(StringName("p1"));
    const Callable roster_of = log.answering(roster_tag(), players);

    Ref<NetwPromise> first;
    first.instantiate();
    const Ref<NetwSceneCore> core = armed(first);

    core->transition_next_mover(roster_of);
    core->transition_next_mover(roster_of);
    NETW_CHECK_EQ(log.count(roster_tag()), 2);
    core->transition_land();

    Ref<NetwPromise> second;
    second.instantiate();
    CHECK(core->transition_open());
    core->transition_arm(StringName("Annex"), two_sources(), second);

    CHECK(bool(
        core->transition_next_mover(roster_of) == Variant(StringName("p1"))
    ));
    NETW_CHECK_EQ(log.count(roster_tag()), 3);
    CHECK(bool(
        log.args(roster_tag(), 2)[0] == Variant(StringName("A"))
    ));
}

} // namespace TestSceneTransitionWalkLaws
