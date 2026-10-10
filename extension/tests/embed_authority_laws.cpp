#include "support/fault_log.h"
#include "support/mesh_stand.h"

#if defined(NETW_TIER_HOSTED)

#include "godot/callable.hpp"
#include "godot/node.hpp"
#include "godot/packed_scene.hpp"
#include "godot/scene_tree.hpp"
#include "netw/api/netw_multiplayer.hpp"
#include "netw/api/nodes/view/stretch_viewport_container.hpp"
#include "netw/api/scene.hpp"

namespace TestEmbedAuthority {

using namespace godot;
using namespace netw_test;
using netw::NetwMultiplayer;
using netw::NetwScene;

constexpr int SERVER = 1;
constexpr int MEMBER = 9;

const char *LEVEL_PATH = "res://tests/support/declared_level.tscn";
const char *ISOLATED_LEVEL_PATH = "res://tests/support/isolated_level.tscn";

Node *mount_under_root(const char *p_name) {
    Node *mount = memnew(Node);
    mount->set_name(StringName(p_name));
    netw::gd::scene_root()->add_child(mount);
    return mount;
}

RID first_scene(NetwMultiplayer *p_session) {
    const TypedArray<RID> held = p_session->scene_list();
    return held.is_empty() ? RID() : RID(held[0]);
}

Node *a_declared_level() {
    const Ref<PackedScene> packed = netw::gd::load_scene(String(LEVEL_PATH));
    REQUIRE(packed.is_valid());
    Node *level = packed->instantiate();
    REQUIRE(level != nullptr);
    return level;
}

void drop(Node *p_mount) {
    netw::gd::scene_root()->remove_child(p_mount);
    memdelete(p_mount);
}

TEST_CASE(
    "[Networked][Session][SceneTree] EA1 the server adopts its declared level "
    "and a member receives that world, so the bare copy is replaced rather "
    "than presented"
) {
    MeshStand stand;
    stand.seat_server();
    stand.seat_member(MEMBER);
    stand.wire(SERVER, MEMBER);
    stand.pump(4);

    NetwMultiplayer *host = stand.session_of(SERVER);
    REQUIRE(host != nullptr);
    NETW_CHECK_EQ(int(host->is_host()), 1);

    NetwMultiplayer *guest = stand.session_of(MEMBER);
    REQUIRE(guest != nullptr);
    NETW_CHECK_EQ(int(guest->is_host()), 0);

    Node *mount = mount_under_root("EA1Host");
    Node *seen = mount_under_root("EA1Member");
    stand.mount(SERVER, mount);
    stand.mount(MEMBER, seen);
    NETW_CHECK_EQ(int(guest->embed_settle()), int(OK));

    Node *level = a_declared_level();
    const ObjectID bare = netw::gd::instance_id(level);
    mount->add_child(level);

    NETW_CHECK_EQ(int(host->embed_settle()), int(OK));
    stand.step_ticks(6);

    NETW_CHECK_EQ(int(netw::gd::instance_from_id(bare) == nullptr), 1);
    NETW_CHECK_EQ(mount->get_child_count(), 1);

    NETW_CHECK_EQ(int(host->scene_list().size()), 1);
    NETW_CHECK_EQ(int(host->scene_watch(first_scene(host), MEMBER)), int(OK));
    stand.step_ticks(4);
    NETW_CHECK_EQ(seen->get_child_count(), 1);

    drop(seen);
    drop(mount);
}

TEST_CASE(
    "[Networked][Session][SceneTree] EA2 a peer holding no scene authority "
    "drops its declared level and spawns nothing, because the world it "
    "presents is the one the server sends"
) {
    MeshStand stand;
    stand.seat_server();
    stand.seat_member(MEMBER);
    stand.wire(SERVER, MEMBER);
    stand.pump(4);

    NetwMultiplayer *guest = stand.session_of(MEMBER);
    REQUIRE(guest != nullptr);
    NETW_CHECK_EQ(int(guest->is_host()), 0);

    Node *mount = mount_under_root("EA2Member");
    stand.mount(MEMBER, mount);
    Node *level = a_declared_level();
    const ObjectID bare = netw::gd::instance_id(level);
    mount->add_child(level);

    NETW_CHECK_EQ(int(guest->embed_settle()), int(OK));

    NETW_CHECK_EQ(int(netw::gd::instance_from_id(bare) == nullptr), 1);
    NETW_CHECK_EQ(mount->get_child_count(), 0);

    drop(mount);
}

TEST_CASE(
    "[Networked][Session][SceneTree] EA3 a declared level the server "
    "cannot respawn is kept and reported, because deleting it would leave "
    "the session presenting nothing at all"
) {
    MeshStand stand;
    stand.seat_server();
    stand.pump(2);

    NetwMultiplayer *host = stand.session_of(SERVER);
    REQUIRE(host != nullptr);

    Node *mount = mount_under_root("EA3Host");
    stand.mount(SERVER, mount);
    Node *level = a_declared_level();
    const ObjectID bare = netw::gd::instance_id(level);
    level->set_scene_file_path(String());
    mount->add_child(level);

    NETW_CHECK_EQ(int(host->embed_settle()), int(OK));

    NETW_CHECK_EQ(int(netw::gd::instance_from_id(bare) != nullptr), 1);
    NETW_CHECK_EQ(mount->get_child_count(), 1);

    drop(mount);
}

TEST_CASE(
    "[Networked][Session][SceneTree] EA5 two declared scenes under the "
    "session root at settle adopt neither and warn once, because picking one "
    "would drop a level the game placed"
) {
    MeshStand stand;
    stand.seat_server();
    stand.pump(2);

    NetwMultiplayer *host = stand.session_of(SERVER);
    REQUIRE(host != nullptr);

    Node *mount = mount_under_root("EA5Host");
    stand.mount(SERVER, mount);
    Node *first = a_declared_level();
    first->set_name(StringName("FirstLevel"));
    Node *second = a_declared_level();
    second->set_name(StringName("SecondLevel"));
    const ObjectID kept_first = netw::gd::instance_id(first);
    const ObjectID kept_second = netw::gd::instance_id(second);
    mount->add_child(first);
    mount->add_child(second);

    const FaultWatch faults;
    NETW_CHECK_EQ(int(host->embed_settle()), int(OK));

    NETW_CHECK_EQ(int(netw::gd::instance_from_id(kept_first) != nullptr), 1);
    NETW_CHECK_EQ(int(netw::gd::instance_from_id(kept_second) != nullptr), 1);
    NETW_CHECK_EQ(mount->get_child_count(), 2);
    NETW_CHECK_EQ(faults.warnings_containing(String("FirstLevel")), int64_t(1));

    drop(mount);
}

TEST_CASE(
    "[Networked][Session][SceneTree] EA6 an isolated scene placed under the "
    "session root is spawned again at settle inside its own world, so a "
    "placed level is wrapped like a spawned one"
) {
    MeshStand stand;
    stand.seat_server();
    stand.pump(2);

    NetwMultiplayer *host = stand.session_of(SERVER);
    REQUIRE(host != nullptr);

    Node *mount = mount_under_root("EA6Host");
    stand.mount(SERVER, mount);
    const Ref<PackedScene> packed
        = netw::gd::load_scene(String(ISOLATED_LEVEL_PATH));
    REQUIRE(packed.is_valid());
    Node *placed = packed->instantiate();
    REQUIRE(placed != nullptr);
    const ObjectID authored = netw::gd::instance_id(placed);
    mount->add_child(placed);

    NETW_CHECK_EQ(int(host->embed_settle()), int(OK));

    NETW_CHECK_EQ(int(netw::gd::instance_from_id(authored) == nullptr), 1);
    REQUIRE(mount->get_child_count() == 1);
    Node *outer = mount->get_child(0);
    const bool wrapped
        = Object::cast_to<netw::StretchViewportContainer>(outer) != nullptr;
    CHECK(wrapped);
    Node *level = NetwMultiplayer::scene_inner_of(outer);
    const bool inside = level != nullptr && level != outer
        && NetwMultiplayer::scene_world_of(level) != nullptr;
    CHECK(inside);

    drop(mount);
}

Node *an_isolated_level(const char *p_name) {
    const Ref<PackedScene> packed
        = netw::gd::load_scene(String(ISOLATED_LEVEL_PATH));
    REQUIRE(packed.is_valid());
    Node *level = packed->instantiate();
    REQUIRE(level != nullptr);
    level->set_name(StringName(p_name));
    return level;
}

const char *UNWRAPPED_WARNING = "is declared isolated but was added outside";

TEST_CASE(
    "[Networked][Session][SceneTree] EA7 an isolated scene replicated in "
    "place after settle shares a world and warns once, even when it is "
    "added again"
) {
    MeshStand stand;
    stand.seat_server();
    stand.pump(2);

    NetwMultiplayer *host = stand.session_of(SERVER);
    REQUIRE(host != nullptr);

    Node *mount = mount_under_root("EA7Host");
    stand.mount(SERVER, mount);
    NETW_CHECK_EQ(int(host->embed_settle()), int(OK));

    const FaultWatch faults;
    Node *loose = an_isolated_level("LooseLevel");
    REQUIRE(host->entity_replicate(loose, nullptr).is_valid());
    mount->add_child(loose);

    CHECK(host->scene_containing(loose) == loose);
    CHECK(NetwMultiplayer::scene_world_of(loose) == nullptr);
    const String named = String("'LooseLevel' ") + UNWRAPPED_WARNING;
    NETW_CHECK_EQ(faults.warnings_containing(named), int64_t(1));

    mount->remove_child(loose);
    mount->add_child(loose);
    host->scene_root_online(loose);

    NETW_CHECK_EQ(faults.warnings_containing(UNWRAPPED_WARNING), int64_t(1));

    drop(mount);
}

TEST_CASE(
    "[Networked][Session][SceneTree] EA8 at settle an isolated scene nested "
    "below the session root's children warns, and the one placed directly "
    "under the session root is adopted without a warning"
) {
    MeshStand stand;
    stand.seat_server();
    stand.pump(2);

    NetwMultiplayer *host = stand.session_of(SERVER);
    REQUIRE(host != nullptr);

    Node *mount = mount_under_root("EA8Host");
    stand.mount(SERVER, mount);
    Node *holder = memnew(Node);
    holder->set_name(StringName("Holder"));
    mount->add_child(holder);
    Node *nested = an_isolated_level("NestedLevel");
    REQUIRE(host->entity_replicate(nested, nullptr).is_valid());
    holder->add_child(nested);
    Node *placed = an_isolated_level("PlacedLevel");
    REQUIRE(host->entity_replicate(placed, nullptr).is_valid());
    mount->add_child(placed);

    const FaultWatch faults;
    NETW_CHECK_EQ(int(host->embed_settle()), int(OK));

    CHECK(NetwMultiplayer::scene_world_of(nested) == nullptr);
    const String named = String("'NestedLevel' ") + UNWRAPPED_WARNING;
    NETW_CHECK_EQ(faults.warnings_containing(named), int64_t(1));
    NETW_CHECK_EQ(faults.warnings_containing(UNWRAPPED_WARNING), int64_t(1));

    drop(mount);
}

const char *PLAYER_ID = "embed_authority_player";

Node *build_player(const Variant &p_name) {
    Node *made = memnew(Node);
    made->set_name(String(p_name));
    return made;
}

Array one_string_type() {
    Array out;
    out.push_back(int(Variant::STRING));
    return out;
}

Array named(const char *p_name) {
    Array out;
    out.push_back(String(p_name));
    return out;
}

void teach_player(NetwMultiplayer *p_session) {
    REQUIRE(p_session != nullptr);
    Array quantizers;
    quantizers.push_back(Variant());
    p_session->spawn_register_constructor(
        StringName(PLAYER_ID),
        callable_mp_static(&build_player),
        one_string_type(),
        quantizers
    );
}

TEST_CASE(
    "[Networked][Session][SceneTree] EA4 the server's startup gives its "
    "joined member at 9 one scene and the one body seated for that "
    "player, so the authoritative world arrives whole"
) {
    MeshStand stand;
    stand.seat_server();
    stand.seat_member(MEMBER);
    stand.wire(SERVER, MEMBER);
    stand.pump(4);

    NetwMultiplayer *host = stand.session_of(SERVER);
    NetwMultiplayer *guest = stand.session_of(MEMBER);
    REQUIRE(host != nullptr);
    REQUIRE(guest != nullptr);

    Node *mount = mount_under_root("EA4Host");
    Node *seen = mount_under_root("EA4Member");
    stand.mount(SERVER, mount);
    stand.mount(MEMBER, seen);
    NETW_CHECK_EQ(int(guest->embed_settle()), int(OK));

    Node *level = a_declared_level();
    mount->add_child(level);
    NETW_CHECK_EQ(int(host->embed_settle()), int(OK));
    stand.step_ticks(6);

    NETW_CHECK_EQ(mount->get_child_count(), 1);

    REQUIRE(stand.join(SERVER, StringName("one")).is_valid());
    REQUIRE(stand.join(MEMBER, StringName("nine")).is_valid());

    const RID opened = first_scene(host);
    NETW_CHECK_EQ(int(host->scene_list().size()), 1);
    const Ref<NetwScene> world = host->scene_handle_of(opened);
    NETW_CHECK_EQ(int(world.is_valid()), 1);
    NETW_CHECK_EQ(
        int(world.is_valid() ? world->watch(host->player_of(MEMBER))
                             : ERR_DOES_NOT_EXIST),
        int(OK)
    );
    stand.step_ticks(4);
    NETW_CHECK_EQ(seen->get_child_count(), 1);

    teach_player(host);
    teach_player(guest);
    const RID player = host->spawn_registered(
        StringName(PLAYER_ID),
        named("NinePlayer"),
        host->player_of(MEMBER).ptr()
    );
    REQUIRE(player.is_valid());
    Node *body = host->entity_get_node(player);
    REQUIRE(body != nullptr);
    mount->get_child(0)->add_child(body);
    stand.step_ticks(8);

    NETW_CHECK_EQ(int(host->entity_get_peer(player)), MEMBER);
    NETW_CHECK_EQ(mount->get_child_count(), 1);
    NETW_CHECK_EQ(mount->get_child(0)->get_child_count(), 1);
    NETW_CHECK_EQ(seen->get_child_count(), 1);
    NETW_CHECK_EQ(seen->get_child(0)->get_child_count(), 1);

    NETW_CHECK_EQ(int(host->scene_list().size()), 1);
    NETW_CHECK_EQ(int(guest->scene_list().size()), 1);
    NETW_CHECK_EQ(int(host->scene_bodies_all().size()), 1);
    NETW_CHECK_EQ(int(guest->scene_bodies_all().size()), 1);
    NETW_CHECK_EQ(int(host->scene_get_viewers(opened).size()), 1);

    drop(seen);
    drop(mount);
}

} // namespace TestEmbedAuthority

#endif
