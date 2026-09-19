#include "support/netw_test.h"

#include "netw/scene_core.hpp"

namespace TestSceneTransitionSlotLaws {

using namespace godot;
using netw::NetwPromise;
using netw::NetwSceneCore;

Array two_sources() {
    Array out;
    out.push_back(StringName("A"));
    out.push_back(StringName("B"));
    return out;
}

PackedInt64Array two_arrivals() {
    PackedInt64Array out;
    out.push_back(7);
    out.push_back(8);
    return out;
}

Ref<NetwSceneCore> armed(const Ref<NetwPromise> &p_promise) {
    Ref<NetwSceneCore> core;
    core.instantiate();
    CHECK(core->transition_open());
    core->transition_arm(
        StringName("Arena"),
        two_sources(),
        p_promise,
        NetwSceneCore::SCOPE_SESSION,
        two_arrivals()
    );
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
    core->transition_arm(
        StringName("Annex"),
        two_sources(),
        refused,
        NetwSceneCore::SCOPE_SESSION,
        two_arrivals()
    );
    CHECK_FALSE(core->transition_open());
    core->transition_fail(int(ERR_UNAUTHORIZED));
    NETW_CHECK_EQ(refused->get_code(), int(ERR_UNAUTHORIZED));

    CHECK(core->transition_open());
}

TEST_CASE(
    "[Networked][Scene][Hosted] ST2 an armed transition answers the target, "
    "the sources and the arrivals it was armed with, so a landing reads one "
    "record rather than recomputing it"
) {
    Ref<NetwPromise> promise;
    promise.instantiate();
    const Ref<NetwSceneCore> core = armed(promise);

    CHECK(bool(core->transition_target() == Variant(StringName("Arena"))));
    NETW_CHECK_EQ(int(core->transition_sources().size()), 2);
    NETW_CHECK_EQ(core->transition_scope(), int(NetwSceneCore::SCOPE_SESSION));
    NETW_CHECK_EQ(int(core->transition_arrivals().size()), 2);
    NETW_CHECK_EQ(int(core->transition_arrivals()[0]), 7);
}

TEST_CASE(
    "[Networked][Scene][Hosted] ST3 a failure answers the code it was given "
    "rather than half-landing"
) {
    Ref<NetwPromise> promise;
    promise.instantiate();
    const Ref<NetwSceneCore> core = armed(promise);

    core->transition_fail(int(ERR_UNAUTHORIZED));

    CHECK(promise->get_is_failed());
    NETW_CHECK_EQ(promise->get_code(), int(ERR_UNAUTHORIZED));
    NETW_CHECK_EQ(int(core->transition_sources().size()), 0);
    CHECK(core->transition_open());
}

TEST_CASE(
    "[Networked][Scene][Hosted] ST4 a landing answers once and drops the "
    "record it read, target, sources and arrivals together"
) {
    Ref<NetwPromise> promise;
    promise.instantiate();
    const Ref<NetwSceneCore> core = armed(promise);

    core->transition_land();

    CHECK(promise->get_is_completed());
    NETW_CHECK_EQ(promise->get_code(), int(OK));
    NETW_CHECK_EQ(int(core->transition_target().get_type()), int(Variant::NIL));
    NETW_CHECK_EQ(int(core->transition_sources().size()), 0);
    NETW_CHECK_EQ(int(core->transition_arrivals().size()), 0);
}

} // namespace TestSceneTransitionSlotLaws
