#include "support/netw_call_log.h"
#include "support/netw_test.h"

#include "godot/callable.hpp"
#include "godot/multiplayer_spawner.hpp"
#include "godot/node.hpp"
#include "godot/variant.hpp"
#include "netw/api/netw_multiplayer.hpp"
#include "netw/api/replication_core.hpp"
#include "netw/spawn/spawner_compat.hpp"

namespace TestNetwSpawnerCompat {

using namespace godot;
using netw::spawn::SpawnerCompat;
using netw_test::CallLog;

Node *build_probe(const Variant &) {
    return memnew(Node);
}

struct Stand {
    Ref<netw::NetwMultiplayer> core;
    SpawnerCompat *compat = nullptr;
    MultiplayerSpawner *spawner = nullptr;
    CallLog log;

    Stand() {
        core.instantiate();
        compat = core->get_replication_plane()->get_spawner_compat();
        spawner = memnew(MultiplayerSpawner);
        spawner->set_spawn_function(callable_mp_static(&build_probe));
    }

    ~Stand() {
        memdelete(spawner);
    }

    void arm_seams(bool p_booked) {
        compat->set_spawn_seams(
            log.answering("booked", p_booked),
            log.callable("arm")
        );
    }

    Node *spawn_through_wrap(const Variant &p_data) {
        const Variant made = spawner->get_spawn_function().call(p_data);
        return Object::cast_to<Node>(netw::gd::live_object(made));
    }

    int64_t drops() const {
        return int64_t(
            compat->counters()[StringName("drops_uncaptured_custom")]
        );
    }
};

TEST_CASE(
    "[Networked][Spawn][Hosted] the authority's wrap captures the spawn "
    "argument, and the registration that follows arms with it"
) {
    Stand stand;
    stand.arm_seams(false);
    stand.compat->wrap_spawner(stand.spawner);

    Node *made = stand.spawn_through_wrap(Variant("carried"));
    REQUIRE(made != nullptr);

    NETW_CHECK_EQ(int(stand.compat->consume(made, stand.spawner)), int(OK));
    NETW_CHECK_EQ(stand.log.count("arm"), 1);
    NETW_CHECK_EQ(stand.drops(), int64_t(0));

    const Array armed = stand.log.args("arm");
    if (armed.size() == 4) {
        NETW_CHECK_EQ(int(armed[2]), -1);
        NETW_FORMAT_TEXT(carried, String(armed[3]).utf8().get_data());
        CAPTURE(carried);
        const bool argument_rode = String(armed[3]) == String("carried");
        CHECK(argument_rode);
    } else {
        NETW_CHECK_EQ(armed.size(), 4);
    }

    memdelete(made);
}

TEST_CASE(
    "[Networked][Spawn][Hosted] a captured argument is consumed by the one "
    "registration it belongs to, and a second registration has none"
) {
    Stand stand;
    stand.arm_seams(false);
    stand.compat->wrap_spawner(stand.spawner);

    Node *made = stand.spawn_through_wrap(Variant("carried"));
    REQUIRE(made != nullptr);

    stand.compat->consume(made, stand.spawner);
    NETW_CHECK_EQ(stand.drops(), int64_t(0));

    NETW_CHECK_EQ(int(stand.compat->consume(made, stand.spawner)), int(OK));
    NETW_CHECK_EQ(stand.log.count("arm"), 1);
    NETW_CHECK_EQ(stand.drops(), int64_t(1));

    memdelete(made);
}

TEST_CASE(
    "[Networked][Spawn][Hosted] a container argument is deep copied, so the "
    "record carries what was spawned rather than what the caller kept"
) {
    Stand stand;
    stand.arm_seams(false);
    stand.compat->wrap_spawner(stand.spawner);

    Array carried;
    carried.push_back(7);
    Node *made = stand.spawn_through_wrap(carried);
    REQUIRE(made != nullptr);
    carried.push_back(9);

    stand.compat->consume(made, stand.spawner);

    const Array armed = stand.log.args("arm");
    REQUIRE(armed.size() == 4);
    const Array recorded = armed[3];
    NETW_CHECK_EQ(recorded.size(), 1);

    memdelete(made);
}

TEST_CASE(
    "[Networked][Spawn][Hosted] a second wrap does not chain, so a receiver "
    "reconstructing through the original captures nothing"
) {
    Stand stand;
    stand.arm_seams(false);
    stand.compat->wrap_spawner(stand.spawner);
    stand.compat->wrap_spawner(stand.spawner);

    Node *made
        = stand.compat->instantiate(stand.spawner, -1, Variant("carried"));
    REQUIRE(made != nullptr);

    NETW_CHECK_EQ(int(stand.compat->consume(made, stand.spawner)), int(OK));
    NETW_CHECK_EQ(stand.log.count("arm"), 0);
    NETW_CHECK_EQ(stand.drops(), int64_t(1));

    memdelete(made);
}

TEST_CASE(
    "[Networked][Spawn][Hosted] a node this peer already books is ignored "
    "rather than given a second identity"
) {
    Stand stand;
    stand.arm_seams(true);
    stand.compat->wrap_spawner(stand.spawner);

    Node *made = stand.spawn_through_wrap(Variant("carried"));
    REQUIRE(made != nullptr);

    NETW_CHECK_EQ(int(stand.compat->consume(made, stand.spawner)), int(OK));
    NETW_CHECK_EQ(stand.log.count("arm"), 0);
    NETW_CHECK_EQ(stand.drops(), int64_t(0));

    memdelete(made);
}

TEST_CASE(
    "[Networked][Spawn][Hosted] a registration with no seams to arm through "
    "is refused rather than silently lost"
) {
    Stand stand;
    SpawnerCompat unseamed;
    Node *made = memnew(Node);

    NETW_CHECK_EQ(
        int(unseamed.consume(made, stand.spawner)),
        int(ERR_UNCONFIGURED)
    );

    unseamed.set_spawn_seams(
        stand.log.answering("booked", false),
        stand.log.callable("arm")
    );
    NETW_CHECK_EQ(
        int(unseamed.consume(nullptr, stand.spawner)),
        int(ERR_INVALID_PARAMETER)
    );
    NETW_CHECK_EQ(
        int(unseamed.consume(made, nullptr)),
        int(ERR_INVALID_PARAMETER)
    );
    NETW_CHECK_EQ(stand.log.count("arm"), 0);

    memdelete(made);
}

TEST_CASE(
    "[Networked][Spawn][Hosted] the receiver-side spawn limit counts what "
    "this peer produced through that spawner"
) {
    Stand stand;
    stand.spawner->set_spawn_limit(1);

    Node *first = stand.compat->instantiate(stand.spawner, -1, Variant());
    REQUIRE(first != nullptr);
    stand.compat->note_recv(31, stand.spawner);

    Node *refused = stand.compat->instantiate(stand.spawner, -1, Variant());
    const bool over_limit_refused = refused == nullptr;
    if (refused != nullptr) {
        memdelete(refused);
    }
    CHECK(over_limit_refused);

    stand.compat->emit_despawned(31, first);
    Node *again = stand.compat->instantiate(stand.spawner, -1, Variant());
    const bool freed_slot_allows = again != nullptr;
    if (again != nullptr) {
        memdelete(again);
    }
    CHECK(freed_slot_allows);

    memdelete(first);
}

TEST_CASE(
    "[Networked][Spawn][Hosted] a scene index the spawner does not offer is "
    "refused rather than reconstructed through the spawn function"
) {
    Stand stand;

    const bool empty_list_refused
        = stand.compat->instantiate(stand.spawner, 0, Variant()) == nullptr;
    const bool past_end_refused
        = stand.compat->instantiate(stand.spawner, 3, Variant()) == nullptr;
    const bool no_spawner_refused
        = stand.compat->instantiate(nullptr, -1, Variant()) == nullptr;

    CHECK(empty_list_refused);
    CHECK(past_end_refused);
    CHECK(no_spawner_refused);
}

TEST_CASE(
    "[Networked][Spawn][Hosted] a spawner with no spawn function is enrolled "
    "and left unwrapped, so the wrap can wait for it"
) {
    Stand stand;
    stand.spawner->set_spawn_function(Callable());

    stand.compat->register_spawner(stand.spawner);

    NETW_CHECK_EQ(stand.compat->get_roster().size(), 1);
    CHECK_FALSE(stand.spawner->get_spawn_function().is_valid());

    stand.spawner->set_spawn_function(callable_mp_static(&build_probe));
    stand.compat->register_spawner(stand.spawner);

    NETW_CHECK_EQ(stand.compat->get_roster().size(), 1);
    const bool wrapped_now
        = stand.spawner->get_spawn_function().get_object() == stand.core.ptr();
    CHECK(wrapped_now);
}

TEST_CASE(
    "[Networked][Spawn][Hosted] a session that ends gives the spawners back "
    "their own functions and leaves an empty roster, because the peer holds "
    "no claim on a spawner past the session it enrolled it for"
) {
    Stand stand;
    const Callable own = stand.spawner->get_spawn_function();
    stand.arm_seams(false);
    stand.compat->register_spawner(stand.spawner);

    NETW_CHECK_EQ(stand.compat->get_roster().size(), 1);
    CHECK(stand.spawner->get_spawn_function().get_object() == stand.core.ptr());

    stand.core->emit_signal(StringName("session_ended"));

    NETW_CHECK_EQ(stand.compat->get_roster().size(), 1);

    stand.core->settle_drain();

    NETW_CHECK_EQ(stand.compat->get_roster().size(), 0);
    CHECK(stand.spawner->get_spawn_function() == own);
}

TEST_CASE(
    "[Networked][Spawn][Hosted] a despawn announces the node on the spawner "
    "that booked its route, so a receiver's despawned listener runs with the "
    "node it was given rather than never running at all"
) {
    Stand stand;
    stand.arm_seams(true);
    stand.compat->wrap_spawner(stand.spawner);

    Node *made = stand.spawn_through_wrap(Variant("carried"));
    REQUIRE(made != nullptr);

    stand.compat->get_roster().note_producer(7, stand.spawner);
    stand.spawner->connect(
        StringName("despawned"),
        stand.log.callable("despawned")
    );

    stand.compat->emit_despawned(7, made);

    NETW_CHECK_EQ(stand.log.count("despawned"), 1);
    const Array carried = stand.log.args("despawned", 0);
    NETW_CHECK_EQ(carried.size(), 1);
    if (carried.size() == 1) {
        CHECK(Object::cast_to<Node>(carried[0]) == made);
    }

    memdelete(made);
}

} // namespace TestNetwSpawnerCompat
