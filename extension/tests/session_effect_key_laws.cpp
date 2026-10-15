#include "support/fault_log.h"
#include "support/netw_test.h"

#include "godot/callable.hpp"
#include "netw/api/entity.hpp"
#include "netw/api/netw_multiplayer.hpp"

namespace TestNetwSessionEffectKey {

using namespace godot;
using netw::NetwEntity;
using netw::NetwMultiplayer;

Ref<NetwMultiplayer> make_session() {
    Ref<NetwMultiplayer> session;
    session.instantiate();
    return session;
}

bool reads(const StringName &p_said, const char *p_expected) {
    return String(p_said) == String(p_expected);
}

TEST_CASE(
    "[Networked][Session][Hosted] L1 an effect key an entity cannot name is "
    "still unique per tick and slot, so two effects never collide"
) {
    Ref<NetwMultiplayer> session = make_session();
    const RID entity = session->entity_create();

    CHECK(reads(session->lagcomp_effect_key(entity, 12, 0), "act__12__0"));

    SUBCASE("the slot separates two effects on one tick") {
        CHECK(reads(session->lagcomp_effect_key(entity, 12, 1), "act__12__1"));
    }

    SUBCASE("the tick separates two effects in one slot") {
        CHECK(reads(session->lagcomp_effect_key(entity, 13, 0), "act__13__0"));
    }

    SUBCASE("an entity nobody minted keys the same way") {
        CHECK(reads(session->lagcomp_effect_key(RID(), 12, 0), "act__12__0"));
    }
}

TEST_CASE(
    "[Networked][Session][Hosted] L2 an effect watched before it is armed is "
    "refused and says so, rather than silently never reverting"
) {
    netw_test::FaultWatch expected_faults;

    Ref<NetwMultiplayer> session = make_session();

    CHECK_FALSE(session->lagcomp_effect_watch(
        StringName("act__1__0"),
        Callable(),
        Callable()
    ));

    SUBCASE("nothing is pending for a key that was never armed") {
        expected_faults.expect_warning(
            "[warn][prediction] Condition \"!watching\" is true. "
            "Effect '*' is not armed, so there is nothing to watch."
            " Call NetwMultiplayer.lagcomp_effect_arm before "
            "lagcomp_effect_watch.",
            1
        );

        CHECK_FALSE(session->lagcomp_effect_pending(StringName("act__1__0")));
        NETW_CHECK_EQ(session->lagcomp_effect_count(), 0);
    }
}

TEST_CASE(
    "[Networked][Session][Hosted] L3 a timeline is declared only for an "
    "entity the session actually holds a wrapper for"
) {
    Ref<NetwMultiplayer> session = make_session();
    const RID entity = session->entity_create();

    NETW_CHECK_EQ(
        session->lagcomp_timeline_declare(entity),
        ERR_DOES_NOT_EXIST
    );
    NETW_CHECK_EQ(session->lagcomp_timeline_declare(RID()), ERR_DOES_NOT_EXIST);

    SUBCASE("and undeclaring one that was never declared is a no-op") {
        session->lagcomp_timeline_undeclare(entity);
        NETW_CHECK_EQ(
            session->get_lagcomp_core()->timeline_registered(),
            int64_t(0)
        );
    }
}

TEST_CASE(
    "[Networked][Session][Hosted] L4 an effect key an entity CAN name carries "
    "that name, and the key stands as an entity id of its own"
) {
    Ref<NetwMultiplayer> session = make_session();
    Node *body = memnew(Node);
    body->set_name("PlayerBody");
    const Ref<NetwEntity> acting = NetwEntity::ensure(body);
    acting->set_name(StringName("player"));
    acting->set_peer_id(7);
    const RID entity = session->entity_of(body);
    REQUIRE(entity.is_valid());

    const StringName key = session->lagcomp_effect_key(entity, 12, 3);
    CHECK(reads(key, "act__player__12__3"));

    Node *spawned = memnew(Node);
    const Ref<NetwEntity> wrapper = NetwEntity::ensure(spawned);
    REQUIRE(wrapper.is_valid());
    wrapper->set_name(key);
    CHECK(wrapper->get_name() == key);

    memdelete(spawned);
    memdelete(body);
}

TEST_CASE(
    "[Networked][Session][Hosted] L5 the census the session publishes counts "
    "exactly what is armed, so a resolved act leaves the book"
) {
    Ref<NetwMultiplayer> session = make_session();
    const StringName adopted = StringName("act__1__0");
    const StringName discarded = StringName("act__2__0");
    session->lagcomp_effect_arm(adopted, Callable(), 10);
    session->lagcomp_effect_arm(discarded, Callable(), 10);

    NETW_CHECK_EQ(int64_t(session->lagcomp_metrics()["effects_armed"]), 2);
    NETW_CHECK_EQ(session->lagcomp_effect_count(), 2);

    session->lagcomp_effect_adopt(adopted);
    NETW_CHECK_EQ(int64_t(session->lagcomp_metrics()["effects_armed"]), 1);

    session->lagcomp_effect_discard(discarded);
    NETW_CHECK_EQ(int64_t(session->lagcomp_metrics()["effects_armed"]), 0);
}

} // namespace TestNetwSessionEffectKey
