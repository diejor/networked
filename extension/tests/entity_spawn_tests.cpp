// Laws for the spawn verbs: instantiating a template's scene, carrying its
// marked spawn state onto the copy, and seating the copy.
//
// A template is named by its scene FILE, so these load a project path and the
// file carries no [Hosted] tag: it runs in the tier that can read res://.

#include "support/netw_test.h"

#include "godot/node.hpp"
#include "godot/packed_scene.hpp"
#include "netw/netw_multiplayer.hpp"
#include "support/entity_facets.h"
#include "support/netw_call_log.h"

namespace TestNetwEntitySpawn {

using namespace godot;
using netw::NetwMultiplayerCore;
using netw_test::CallLog;
using netw_test::EntityFactories;

constexpr const char *TEMPLATE_SCENE
    = "res://tests/support/scene/treeless_level.tscn";

Node *a_template() {
    Ref<PackedScene> scene = netw::gd::load_scene(TEMPLATE_SCENE);
    return scene.is_valid() ? scene->instantiate() : nullptr;
}

TEST_CASE(
    "[Networked][Entity] a copy is unparented, and its wrapper is configured "
    "before anything can see it"
) {
    EntityFactories factories;
    CallLog log;
    NetwMultiplayerCore::set_wrapper_factory(log.minting("wrapper"));
    Node *original = a_template();
    REQUIRE(original != nullptr);

    Node *copy = NetwMultiplayerCore::entity_instantiate_copy(
        original,
        log.callable("configure")
    );

    REQUIRE(copy != nullptr);
    CHECK(copy != original);
    NETW_CHECK_EQ(copy->get_parent(), nullptr);
    // The wrapper exists by the time configure runs, because a caller
    // configures the ENTITY rather than the node.
    NETW_CHECK_EQ(log.count("wrapper"), 1);
    NETW_CHECK_EQ(log.count("configure"), 1);
    CHECK(log.order() == Vector<StringName>({"wrapper", "configure"}));

    SUBCASE("a template that is not a scene copies nothing") {
        Node *bare = memnew(Node);
        ERR_PRINT_OFF;
        CHECK(
            NetwMultiplayerCore::entity_instantiate_copy(bare, Callable())
            == nullptr
        );
        ERR_PRINT_ON;
        memdelete(bare);
    }

    memdelete(copy);
    memdelete(original);
}

TEST_CASE(
    "[Networked][Entity] a spawn carries the marked values the session names, "
    "and nothing else"
) {
    Ref<NetwMultiplayerCore> core;
    core.instantiate();
    EntityFactories factories;
    CallLog log;
    NetwMultiplayerCore::set_wrapper_factory(log.minting("wrapper"));
    Node *original = a_template();
    REQUIRE(original != nullptr);
    original->set("position", Vector2(3, 4));

    Dictionary marked;
    marked["node"] = original;
    marked["prop"] = StringName("position");
    Array rows;
    rows.push_back(marked);
    core->set_spawn_state_gather(log.answering("gather", rows));

    Node *copy = core->entity_instantiate_from(original, Callable());

    REQUIRE(copy != nullptr);
    NETW_CHECK_EQ(log.count("gather"), 1);
    CHECK(Vector2(copy->get("position")) == Vector2(3, 4));

    SUBCASE("a source the copy has no counterpart for is skipped") {
        Node *stranger = memnew(Node);
        original->add_child(stranger);
        stranger->set_name("Stranger");
        Dictionary elsewhere;
        elsewhere["node"] = stranger;
        elsewhere["prop"] = StringName("name");
        Array other;
        other.push_back(elsewhere);
        core->set_spawn_state_gather(log.answering("gather", other));

        Node *second = core->entity_instantiate_from(original, Callable());

        REQUIRE(second != nullptr);
        // The copy has no counterpart for a node the template grew after it
        // was packed, so the value lands nowhere rather than on the root.
        NETW_CHECK_EQ(second->get_child_count(), 0);
        CHECK(second->get_name() != StringName("Stranger"));
        memdelete(second);
    }

    SUBCASE("a session told nothing carries nothing") {
        core->set_spawn_state_gather(Callable());
        Node *plain = core->entity_instantiate_from(original, Callable());
        REQUIRE(plain != nullptr);
        CHECK(Vector2(plain->get("position")) == Vector2(0, 0));
        memdelete(plain);
    }

    memdelete(copy);
    memdelete(original);
}

TEST_CASE(
    "[Networked][Entity] a spawn under a parent is named, seated, and falls "
    "back to the template's own parent"
) {
    Ref<NetwMultiplayerCore> core;
    core.instantiate();
    EntityFactories factories;
    CallLog log;
    NetwMultiplayerCore::set_wrapper_factory(log.minting("wrapper"));
    Node *home = memnew(Node);
    Node *original = a_template();
    REQUIRE(original != nullptr);
    home->add_child(original);
    Node *elsewhere = memnew(Node);

    Node *named = core->entity_spawn_under(original, elsewhere, "skeleton_1");

    REQUIRE(named != nullptr);
    NETW_CHECK_EQ(named->get_parent(), elsewhere);
    CHECK(named->get_name() == StringName("skeleton_1|0"));

    SUBCASE("no parent given seats the copy beside its template") {
        Node *wild = core->entity_spawn_under(original, nullptr, StringName());
        REQUIRE(wild != nullptr);
        NETW_CHECK_EQ(wild->get_parent(), home);
    }

    SUBCASE("a template with no parent and no destination seats nothing") {
        Node *orphan = a_template();
        REQUIRE(orphan != nullptr);
        ERR_PRINT_OFF;
        CHECK(
            core->entity_spawn_under(orphan, nullptr, StringName()) == nullptr
        );
        ERR_PRINT_ON;
        memdelete(orphan);
    }

    memdelete(elsewhere);
    memdelete(home);
}

TEST_CASE(
    "[Networked][Entity] a participant that names no join builds no player"
) {
    Ref<NetwMultiplayerCore> core;
    core.instantiate();
    EntityFactories factories;
    CallLog log;
    NetwMultiplayerCore::set_wrapper_factory(log.minting("wrapper"));
    Node *original = a_template();
    REQUIRE(original != nullptr);
    Ref<RefCounted> unaccepted;
    unaccepted.instantiate();

    // A participant with no join has not been accepted, so it names no player
    // to build one for, and a scene is never asked to seat one.
    CHECK(core->entity_instantiate_player(original, unaccepted.ptr()) == nullptr
    );
    CHECK(core->entity_spawn_player(original, nullptr, nullptr) == nullptr);
    NETW_CHECK_EQ(log.count("wrapper"), 0);

    memdelete(original);
}

} // namespace TestNetwEntitySpawn
