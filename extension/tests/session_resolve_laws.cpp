#include "support/netw_test.h"

#if defined(NETW_TIER_HOSTED)
#include <godot_cpp/classes/class_db_singleton.hpp>
#include <godot_cpp/classes/resource_loader.hpp>
#endif

#include "godot/scene_tree.hpp"
#include "godot/script.hpp"
#include "netw/api/netw_multiplayer.hpp"
#include "netw/api/nodes/lobby_directory.hpp"
#include "netw/api/resolved_join.hpp"
#include "support/declared_seams.h"
#include "support/minted_script.h"

namespace TestNetwSessionResolve {

using namespace godot;
using netw::NetwMultiplayer;

bool is_session(const NetwMultiplayer *p_a, const NetwMultiplayer *p_b) {
    return p_a == p_b;
}

TEST_CASE(
    "[Networked][Session][Hosted] L1 resolving a session from no node, or "
    "from a node no tree holds, answers nothing rather than reaching"
) {
    CHECK(is_session(NetwMultiplayer::of(nullptr), nullptr));

    Node *loose = memnew(Node);
    CHECK_FALSE(loose->is_inside_tree());
    CHECK(is_session(NetwMultiplayer::of(loose), nullptr));
    memdelete(loose);
}

#if defined(NETW_TIER_HOSTED)

TEST_CASE(
    "[Networked][Session][SceneTree] L2 a node in the tree resolves "
    "to the session its branch installed, through whatever object "
    "publishes it"
) {
    Node *mounted = memnew(Node);
    netw::gd::scene_root()->add_child(mounted);
    CHECK(mounted->is_inside_tree());

    const Ref<MultiplayerAPI> installed = mounted->get_multiplayer();
    NetwMultiplayer *resolved = NetwMultiplayer::of(mounted);
    CHECK(resolved != nullptr);
    CHECK(resolved->session_api() == installed.ptr());
    CHECK(resolved->session_is_active());
    CHECK(NetwMultiplayer::session_of(mounted) == installed);

    mounted->queue_free();
}

#endif

TEST_CASE(
    "[Networked][Session][Hosted] L3 with no law extension named, the "
    "environment resolves to no script rather than to a broken one"
) {
    CHECK_FALSE(NetwMultiplayer::law_extension_named());
    CHECK(NetwMultiplayer::law_extension_script().is_null());
}

Ref<NetwMultiplayer> session_rooted_at(Node *p_node) {
    Ref<SceneMultiplayer> inner;
    inner.instantiate();
    inner->set_root_path(p_node->get_path());
    Ref<NetwMultiplayer> session;
    session.instantiate();
    session->session_set_inner(inner);
    return session;
}

TEST_CASE(
    "[Networked][Session][Hosted][SceneTree] L4 every branch-installed "
    "session answers live_sessions, and one uninstalled leaves the others "
    "standing"
) {
    SceneTree *loop = netw::gd::scene_tree();
    Node *branch_a = memnew(Node);
    Node *branch_b = memnew(Node);
    netw::gd::scene_root()->add_child(branch_a);
    netw::gd::scene_root()->add_child(branch_b);

    const int64_t before = NetwMultiplayer::session_get_all().size();

    Ref<NetwMultiplayer> session_a = session_rooted_at(branch_a);
    Ref<NetwMultiplayer> session_b = session_rooted_at(branch_b);
    CHECK_FALSE(session_a->session_is_active());
    CHECK_FALSE(session_b->session_is_active());

    loop->set_multiplayer(session_a, branch_a->get_path());
    loop->set_multiplayer(session_b, branch_b->get_path());
    CHECK(session_a->session_is_active());
    CHECK(session_b->session_is_active());

    TypedArray<MultiplayerAPI> both = NetwMultiplayer::session_get_all();
    NETW_CHECK_EQ(int64_t(both.size()), before + 2);
    CHECK(both.has(session_a));
    CHECK(both.has(session_b));

    loop->set_multiplayer(Ref<MultiplayerAPI>(), branch_a->get_path());
    CHECK_FALSE(session_a->session_is_active());

    TypedArray<MultiplayerAPI> one = NetwMultiplayer::session_get_all();
    NETW_CHECK_EQ(int64_t(one.size()), before + 1);
    CHECK_FALSE(one.has(session_a));
    CHECK(one.has(session_b));

    loop->set_multiplayer(Ref<MultiplayerAPI>(), branch_b->get_path());
    branch_a->queue_free();
    branch_b->queue_free();
}

TEST_CASE(
    "[Networked][Session][Hosted] L5 the registry outlives no session: a "
    "session the registry has seen is freed with its last reference"
) {
    ObjectID seen;
    {
        Ref<NetwMultiplayer> session;
        session.instantiate();
        seen = netw::gd::instance_id(session.ptr());
        CHECK(netw::gd::instance_from_id(seen) != nullptr);
        NetwMultiplayer::session_get_all();
    }
    CHECK(netw::gd::instance_from_id(seen) == nullptr);
    NetwMultiplayer::session_get_all();
    CHECK(netw::gd::instance_from_id(seen) == nullptr);
}

TEST_CASE(
    "[Networked][Session][Hosted] L6 a session mints its own transport and "
    "roots it at /root, so no installer has to write the root path"
) {
    Ref<NetwMultiplayer> session;
    session.instantiate();
    CHECK(session->session_get_inner().is_valid());
    if (session->session_get_inner().is_valid()) {
        CHECK(
            session->session_get_inner()->get_root_path() == NodePath("/root")
        );
    }
    CHECK(session->get_root_path() == NodePath("/root"));
}

TEST_CASE(
    "[Networked][Session][Hosted] L7 the engine's own default-interface "
    "route builds a session that is ready with no installer script"
) {
    const StringName previous = MultiplayerAPI::get_default_interface();
    CHECK(previous != StringName());

    MultiplayerAPI::set_default_interface(StringName("NetwMultiplayer"));
    CHECK(
        MultiplayerAPI::get_default_interface() == StringName("NetwMultiplayer")
    );

    const Ref<MultiplayerAPI> made = MultiplayerAPI::create_default_interface();
    NetwMultiplayer *session = Object::cast_to<NetwMultiplayer>(made.ptr());
    CHECK(session != nullptr);
    if (session != nullptr) {
        CHECK(session->session_get_inner().is_valid());
        CHECK(session->get_root_path() == NodePath("/root"));
    }

    MultiplayerAPI::set_default_interface(previous);
    CHECK(MultiplayerAPI::get_default_interface() == previous);
}

TEST_CASE(
    "[Networked][Session][Hosted] L8 a session that has armed its persistence "
    "quit guard is still freed with its last reference, because the guard "
    "holds no session"
) {
    ObjectID seen;
    {
        Ref<NetwMultiplayer> session;
        session.instantiate();
        seen = netw::gd::instance_id(session.ptr());
        session->persistence_arm_quit_guard();
        session->persistence_arm_quit_guard();
    }
    CHECK(netw::gd::instance_from_id(seen) == nullptr);
}

TEST_CASE(
    "[Networked][Session][Hosted] L9 a service book belongs to the session "
    "that holds it, keys on any object identity, and holding a service keeps "
    "no session alive"
) {
    ObjectID seen;
    Node *service = memnew(Node);
    Node *key = memnew(Node);
    {
        Ref<NetwMultiplayer> session;
        session.instantiate();
        seen = netw::gd::instance_id(session.ptr());

        session->service_register(service, key);
        CHECK(session->get_service(key) == service);

        Ref<NetwMultiplayer> other;
        other.instantiate();
        CHECK(other->get_service(key) == nullptr);

        session->service_clear();
        CHECK(session->get_service(key) == nullptr);
    }
    CHECK(netw::gd::instance_from_id(seen) == nullptr);
    memdelete(key);
    memdelete(service);
}

#if defined(NETW_TIER_HOSTED)

TEST_CASE(
    "[Networked][Session] L11 a service registered under its own "
    "script answers a query naming any script it descends from, so a game "
    "asks for the family it depends on rather than the exact leaf a mod "
    "happened to install"
) {
    const Ref<Script> base = ResourceLoader::get_singleton()->load(
        "res://tests/support/chains/declaration_base.gd"
    );
    NETW_CHECK_EQ(int(base.is_valid()), 1);
    if (base.is_null()) {
        return;
    }
    const Ref<Script> derived
        = ClassDBSingleton::get_singleton()->instantiate("GDScript");
    derived->set_source_code(
        String("extends \"res://tests/support/chains/declaration_base.gd\"\n")
    );
    derived->reload();

    Ref<NetwMultiplayer> session;
    session.instantiate();
    Node *service = Object::cast_to<Node>(derived->call("new"));
    NETW_CHECK_EQ(int(service != nullptr), 1);
    if (service == nullptr) {
        return;
    }
    session->service_register(service, derived.ptr());

    NETW_CHECK_EQ(int(session->service_get_all(derived.ptr()).size()), 1);
    const TypedArray<Node> family = session->service_get_all(base.ptr());
    NETW_CHECK_EQ(int(family.size()), 1);
    if (family.size() == 1) {
        CHECK(family[0] == Variant(service));
    }

    session->service_unregister(service, derived.ptr());
    NETW_CHECK_EQ(int(session->service_get_all(base.ptr()).size()), 0);

    memdelete(service);
    session->embed_dispose();
}

#endif

TEST_CASE(
    "[Networked][Session][Hosted][SceneTree] L10 a session displaced as the "
    "tree default and disposed is holding no roster either, so the roster "
    "does not outlive the session that seated it"
) {
    SceneTree *loop = netw::gd::scene_tree();
    const Ref<MultiplayerAPI> original = loop->get_multiplayer();

    const Ref<NetwMultiplayer> installed
        = NetwMultiplayer::make(Ref<SceneMultiplayer>(), Ref<Script>());
    NETW_CHECK_EQ(int(installed.is_valid()), 1);
    if (installed.is_null()) {
        return;
    }
    loop->set_multiplayer(installed);
    CHECK(loop->get_multiplayer().ptr() == installed.ptr());
    CHECK(installed->session_is_active());

    Ref<netw::ResolvedJoin> seated;
    seated.instantiate();
    seated->set_peer_id(7);
    seated->set_username(StringName("ana"));
    installed->session_remember_join(seated);
    NETW_CHECK_EQ(int(installed->session_accepted_joins().size()), 1);

    Ref<SceneMultiplayer> stock;
    stock.instantiate();
    loop->set_multiplayer(stock);
    installed->embed_dispose();

    CHECK(loop->get_multiplayer().ptr() != installed.ptr());
    CHECK(loop->get_multiplayer().ptr() != nullptr);
    CHECK_FALSE(installed->session_is_active());
    CHECK_FALSE(NetwMultiplayer::session_get_all().has(installed));
    NETW_CHECK_EQ(int(installed->session_accepted_joins().size()), 0);

    loop->set_multiplayer(original);
}

TEST_CASE(
    "[Networked][Session][Hosted][SceneTree] L11 the tree is born holding a "
    "NetwMultiplayer, because the library names itself the default interface "
    "before any SceneTree is constructed"
) {
    CHECK(
        MultiplayerAPI::get_default_interface() == StringName("NetwMultiplayer")
    );
    SceneTree *loop = netw::gd::scene_tree();
    CHECK(
        Object::cast_to<NetwMultiplayer>(loop->get_multiplayer().ptr())
        != nullptr
    );
}

TEST_CASE(
    "[Networked][Session] a service carrying no script registers under its "
    "own class and answers a lookup by that name, so a service written in C++ "
    "reaches the registry the same way a scripted one does"
) {
    Ref<NetwMultiplayer> session;
    session.instantiate();
    Node *service = memnew(Node);

    session->service_register(service, nullptr);

    NETW_CHECK_EQ(
        int(session->get_service_named(StringName("Node")) == service),
        1
    );
    NETW_CHECK_EQ(
        int(session->get_service_named(StringName("Nothing")) == nullptr),
        1
    );

    session->service_unregister(service, nullptr);
    NETW_CHECK_EQ(
        int(session->get_service_named(StringName("Node")) == nullptr),
        1
    );

    memdelete(service);
}

#if defined(NETW_TIER_HOSTED)

TEST_CASE(
    "[Networked][Session] a class name written in a script resolves the "
    "service registered under it, so a game asks for a native service with "
    "the same one verb and the same one spelling it asks for a scripted one"
) {
    const Ref<Script> fixture
        = netw_test::script_from(netw_test::gdsrc::NATIVE_CLASS_HANDLE);
    REQUIRE(fixture.is_valid());
    Object *named = netw::gd::live_object(fixture->call("directory"));
    REQUIRE(named != nullptr);

    Ref<NetwMultiplayer> session;
    session.instantiate();
    Node *service = memnew(netw::LobbyDirectory);

    NETW_CHECK_EQ(int(session->get_service(named) == nullptr), 1);

    session->service_register(service, nullptr);

    NETW_CHECK_EQ(int(session->get_service(named) == service), 1);

    session->service_unregister(service, nullptr);
    NETW_CHECK_EQ(int(session->get_service(named) == nullptr), 1);

    memdelete(service);
}

#endif

} // namespace TestNetwSessionResolve
