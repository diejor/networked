#include "support/fault_log.h"
#include "support/netw_test.h"

#include "godot/callable.hpp"
#include "godot/multiplayer.hpp"
#include "godot/node.hpp"
#include "godot/scene_tree.hpp"
#include "godot/spatial_node.hpp"
#include "netw/api/context.hpp"
#include "netw/api/database.hpp"
#include "netw/api/database_backend.hpp"
#include "netw/api/entity.hpp"
#include "netw/api/loopback.hpp"
#include "netw/api/netw_multiplayer.hpp"
#include "netw/api/nodes/multiplayer_viewport.hpp"
#include "netw/api/persistence_config.hpp"
#include "netw/api/property_config.hpp"
#include "netw/api/schema_model.hpp"
#include "netw/persist/binding.hpp"
#include "netw/persist/memory_store.hpp"
#include "netw/schema_model.hpp"
#include "netw/script/model.hpp"

namespace TestDatabaseDeclaration {

using namespace godot;
using netw::MemoryDatabase;
using netw::Netw;
using netw::NetwDatabase;
using netw::NetwDatabaseBackend;
using netw::NetwMultiplayer;
using netw::NetwPromise;
using netw::NetwSchema;

struct World {
    Node *node = nullptr;
    Ref<NetwMultiplayer> api;

    explicit World(const char *p_name) {
        netw::schema_model::clear();
        netw::persist::forget_stores();
        netw::MultiplayerViewport *viewport = memnew(netw::MultiplayerViewport);
        node = viewport;
        node->set_name(StringName(p_name));
        netw::gd::scene_root()->add_child(node);
        api = viewport->get_api();
    }

    ~World() {
        api->NETW_API_VIRTUAL(set_multiplayer_peer)(Ref<MultiplayerPeer>());
        node->get_parent()->remove_child(node);
        memdelete(node);
        netw::schema_model::clear();
        netw::persist::forget_stores();
    }

    Node *author(const Ref<NetwDatabaseBackend> &p_backend) {
        Node *made = memnew(Node);
        made->set_name("Author");
        Netw::configure_database(made, "saves")->backend(p_backend);
        return made;
    }
};

Ref<MemoryDatabase> backend(const char *p_store) {
    Ref<MemoryDatabase> made;
    made.instantiate();
    made->set_store(StringName(p_store));
    return made;
}

void lookup_at_ready(Node *p_child) {
    const Ref<NetwDatabase> db = NetwDatabase::of(p_child, "saves");
    p_child->set_meta("found", db.is_valid());
}

void open_at_enter(Node *p_author) {
    const Ref<NetwDatabase> db = NetwDatabase::of(p_author, "saves");
    if (db.is_valid()) {
        db->open("slot1");
    }
}

StringName record_id(Node *p_node) {
    const int count = int(p_node->get_meta("calls", 0));
    p_node->set_meta("calls", count + 1);
    p_node->set_meta("inside", netw::gd::node_ready(p_node));
    return p_node->get_name();
}

TEST_CASE(
    "[Networked][Table][Hosted][SceneTree] DC1 an unmounted declaration "
    "is available before its child's ready"
) {
    World world("DC1");
    Node *author = world.author(backend("dc1"));
    Node *child = memnew(Node);
    child->set_name("Child");
    child->connect("ready", callable_mp_static(&lookup_at_ready).bind(child));
    author->add_child(child);
    world.node->add_child(author);
    CHECK(bool(child->get_meta("found", false)));
}

TEST_CASE(
    "[Networked][Table][Hosted][SceneTree] DC2 replacing a freed database "
    "author accepts a shared or a fresh backend"
) {
    for (int fresh = 0; fresh < 2; ++fresh) {
        World world("DC2");
        const Ref<MemoryDatabase> first = backend("dc2");
        Node *author = world.author(first);
        world.node->add_child(author);
        const Ref<NetwDatabase> db
            = NetwDatabase::named(world.api.ptr(), "saves");
        REQUIRE(db.is_valid());
        world.node->remove_child(author);
        memdelete(author);
        const Ref<MemoryDatabase> replacement = fresh ? backend("dc2") : first;
        author = world.author(replacement);
        world.node->add_child(author);
        CHECK(NetwDatabase::of(author, "saves") == db);
        Ref<NetwDatabaseBackend> selected;
        NETW_CHECK_EQ(
            int(world.api->database_backend_of(
                world.api->database_find("saves"),
                selected
            )),
            int(OK)
        );
        CHECK(selected == replacement);
        NETW_CHECK_EQ(int(db->open("slot1")->get_result()), int(OK));
    }
}

TEST_CASE(
    "[Networked][Table][Hosted][SceneTree] DC3 conflicting live backends "
    "refuse open with one fault and retain both declarations"
) {
    netw_test::FaultWatch faults;
    faults.expect_error(
        "[error][table] Database '*' is declared with two different "
        "backends*",
        1
    );
    World world("DC3");
    Node *one = world.author(backend("dc3-one"));
    Node *two = world.author(backend("dc3-two"));
    two->set_name("Other");
    world.node->add_child(one);
    world.node->add_child(two);
    const Ref<NetwDatabase> db = NetwDatabase::of(one, "saves");
    REQUIRE(db.is_valid());
    const Ref<NetwPromise> opening = db->open("slot1");
    CHECK(opening->get_is_failed());
    NETW_CHECK_EQ(opening->get_code(), ERR_ALREADY_EXISTS);
    CHECK(NetwDatabase::of(two, "saves") == db);
    world.node->remove_child(two);
    memdelete(two);
    NETW_CHECK_EQ(int(db->open("slot1")->get_result()), int(OK));
}

TEST_CASE(
    "[Networked][Table][Hosted][SceneTree] DC4 a database outlives its "
    "author for record access and reopening"
) {
    World world("DC4");
    Node *author = world.author(backend("dc4"));
    world.node->add_child(author);
    const Ref<NetwDatabase> db = NetwDatabase::of(author, "saves");
    REQUIRE(db.is_valid());
    NETW_CHECK_EQ(int(db->open("slot1")->get_result()), int(OK));
    world.node->remove_child(author);
    memdelete(author);
    const Ref<NetwSchema> schema = NetwSchema::declare("scores");
    schema->replicated(false);
    schema->i64("score", 1);
    Dictionary values;
    values["score"] = 7;
    NETW_CHECK_EQ(int(db->write(schema, "hero", values)->get_result()), OK);
    const Dictionary read = db->read(schema, "hero")->get_result();
    CHECK(bool(read["found"]));
    NETW_CHECK_EQ(int(Dictionary(read["values"])["score"]), 7);
    Node *other = memnew(Node);
    other->set_name("Other");
    Netw::configure_database(other, "other")->backend(backend("dc4-other"));
    world.node->add_child(other);
    db->close();
    const Ref<NetwPromise> reopened = db->open("slot1");
    CHECK(reopened->get_is_completed());
    NETW_CHECK_EQ(int(reopened->get_result()), int(OK));
}

TEST_CASE(
    "[Networked][Table][Hosted][SceneTree] DC5 children enroll at ready "
    "with separate record ids captured once and save their own properties"
) {
    World world("DC5");
    Ref<netw::LocalMultiplayerPeer> peer;
    peer.instantiate();
    NETW_CHECK_EQ(int(peer->create_server()), int(OK));
    world.api->NETW_API_VIRTUAL(set_multiplayer_peer)(peer);
    Node *author = world.author(backend("dc5"));
    author->connect(
        "tree_entered",
        callable_mp_static(&open_at_enter).bind(author)
    );
    Node2D *players[2];
    for (int at = 0; at < 2; ++at) {
        Node2D *player = memnew(Node2D);
        players[at] = player;
        player->set_name(at == 0 ? "Ada" : "Bob");
        Netw::configure_entity(player)->set_name(player->get_name());
        Netw::configure_property(player, "position")->persisted("where");
        Netw::configure_persistence(player)
            ->database("saves")
            ->table("players")
            ->record_id(callable_mp_static(&record_id).bind(player));
        author->add_child(player);
        NETW_CHECK_EQ(int(player->get_meta("calls", 0)), 0);
    }
    world.node->add_child(author);
    for (int at = 0; at < 2; ++at) {
        Node2D *player = players[at];
        const RID binding = world.api->get_bindings()->find(player);
        REQUIRE(binding.is_valid());
        NETW_CHECK_EQ(int(player->get_meta("calls", 0)), 1);
        CHECK(bool(player->get_meta("inside", false)));
        CHECK(
            world.api->persist_get_record_id_binding(binding)
            == player->get_name()
        );
        player->set_position(Vector2(at + 1, at + 2));
        CHECK(world.api->persist_save_binding(binding)->get_is_completed());
        CHECK(world.api->persist_load_binding(binding)->get_is_completed());
        NETW_CHECK_EQ(int(player->get_meta("calls", 0)), 1);
        const Ref<NetwDatabase> db = NetwDatabase::of(player, "saves");
        const Ref<NetwSchema> schema = NetwSchema::declare("players");
        const Dictionary read
            = db->read(schema, player->get_name())->get_result();
        CHECK(bool(read["found"]));
        CHECK(
            bool(
                Vector2(Dictionary(read["values"])["where"])
                == Vector2(at + 1, at + 2)
            )
        );
    }
}

class RefusingBackend : public NetwDatabaseBackend {
public:
    Ref<NetwPromise> open_default(Object *, const StringName &) override {
        return NetwPromise::rejected(ERR_CANT_OPEN, "refused open");
    }
};

TEST_CASE(
    "[Networked][Table][Hosted][SceneTree] DC6 declaring a database does "
    "not swallow a backend's open failure"
) {
    World world("DC6");
    const Ref<NetwDatabaseBackend> refusing(memnew(RefusingBackend));
    Node *author = world.author(refusing);
    world.node->add_child(author);
    const Ref<NetwDatabase> db = NetwDatabase::of(author, "saves");
    REQUIRE(db.is_valid());
    const Ref<NetwPromise> opening = db->open("slot1");
    CHECK(opening->get_is_failed());
    NETW_CHECK_EQ(opening->get_code(), ERR_CANT_OPEN);
    CHECK(NetwDatabase::of(author, "saves") == db);
}

} // namespace TestDatabaseDeclaration
