#include "support/mesh_stand.h"

#if defined(NETW_TIER_HOSTED)

#include "godot/callable.hpp"
#include "godot/node.hpp"
#include "godot/packed_scene.hpp"
#include "godot/scene_tree.hpp"
#include "netw/api/netw_multiplayer.hpp"
#include "netw/api/scene_handle.hpp"

namespace TestEmbedAuthority {

using namespace godot;
using namespace netw_test;
using netw::NetwMultiplayer;
using netw::NetwSceneHandle;

constexpr int COORDINATOR = 7;
constexpr int MEMBER = 9;
constexpr int TRANSPORT_SERVER = 1;

const char *LEVEL_PATH = "res://tests/support/declared_level.tscn";

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
    "[Networked][Session][SceneTree] EA1 a coordinator that is not transport "
    "peer 1 adopts its declared level and transport peer 1 receives that "
    "world, so the bare copy is replaced rather than presented"
) {
    MeshStand stand;
    stand.seat_coordinator(COORDINATOR);
    stand.seat_member(TRANSPORT_SERVER);
    stand.wire(COORDINATOR, TRANSPORT_SERVER);
    stand.pump(4);

    NetwMultiplayer *host = stand.session_of(COORDINATOR);
    REQUIRE(host != nullptr);
    NETW_CHECK_EQ(int(host->is_server()), 0);
    NETW_CHECK_EQ(int(host->is_host()), 1);

    NetwMultiplayer *guest = stand.session_of(TRANSPORT_SERVER);
    REQUIRE(guest != nullptr);
    NETW_CHECK_EQ(int(guest->is_server()), 1);
    NETW_CHECK_EQ(int(guest->is_host()), 0);

    Node *mount = mount_under_root("EA1Host");
    Node *seen = mount_under_root("EA1Member");
    stand.mount(COORDINATOR, mount);
    stand.mount(TRANSPORT_SERVER, seen);
    NETW_CHECK_EQ(int(guest->embed_settle()), int(OK));

    Node *level = a_declared_level();
    const ObjectID bare = netw::gd::instance_id(level);
    mount->add_child(level);

    host->embed_offer_bare_level(level);
    NETW_CHECK_EQ(int(host->embed_settle()), int(OK));
    stand.step_ticks(6);

    NETW_CHECK_EQ(int(netw::gd::instance_from_id(bare) == nullptr), 1);
    NETW_CHECK_EQ(mount->get_child_count(), 1);

    NETW_CHECK_EQ(int(host->scene_list().size()), 1);
    NETW_CHECK_EQ(
        int(host->scene_watch(first_scene(host), TRANSPORT_SERVER)),
        int(OK)
    );
    stand.step_ticks(4);
    NETW_CHECK_EQ(seen->get_child_count(), 1);

    drop(seen);
    drop(mount);
}

TEST_CASE(
    "[Networked][Session][SceneTree] EA2 a peer holding no scene authority "
    "drops its declared level and spawns nothing, because the world it "
    "presents is the one the coordinator sends"
) {
    MeshStand stand;
    stand.seat_coordinator(COORDINATOR);
    stand.seat_member(MEMBER);
    stand.wire(COORDINATOR, MEMBER);
    stand.pump(4);

    NetwMultiplayer *guest = stand.session_of(MEMBER);
    REQUIRE(guest != nullptr);
    NETW_CHECK_EQ(int(guest->is_host()), 0);

    Node *mount = mount_under_root("EA2Member");
    stand.mount(MEMBER, mount);
    Node *level = a_declared_level();
    const ObjectID bare = netw::gd::instance_id(level);
    mount->add_child(level);

    guest->embed_offer_bare_level(level);
    NETW_CHECK_EQ(int(guest->embed_settle()), int(OK));

    NETW_CHECK_EQ(int(netw::gd::instance_from_id(bare) == nullptr), 1);
    NETW_CHECK_EQ(mount->get_child_count(), 0);

    drop(mount);
}

TEST_CASE(
    "[Networked][Session][SceneTree] EA3 a declared level the coordinator "
    "cannot respawn is kept and reported, because deleting it would leave "
    "the session presenting nothing at all"
) {
    MeshStand stand;
    stand.seat_coordinator(COORDINATOR);
    stand.pump(2);

    NetwMultiplayer *host = stand.session_of(COORDINATOR);
    REQUIRE(host != nullptr);

    Node *mount = mount_under_root("EA3Host");
    stand.mount(COORDINATOR, mount);
    Node *level = a_declared_level();
    const ObjectID bare = netw::gd::instance_id(level);
    level->set_scene_file_path(String());
    mount->add_child(level);

    host->embed_offer_bare_level(level);
    NETW_CHECK_EQ(int(host->embed_settle()), int(OK));

    NETW_CHECK_EQ(int(netw::gd::instance_from_id(bare) != nullptr), 1);
    NETW_CHECK_EQ(mount->get_child_count(), 1);

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
    "[Networked][Session][SceneTree] EA4 coordinator 7 startup gives its "
    "joined member at 9 one scene and the one body seated for that "
    "player, so the authoritative world arrives whole"
) {
    MeshStand stand;
    stand.seat_coordinator(COORDINATOR);
    stand.seat_member(MEMBER);
    stand.wire(COORDINATOR, MEMBER);
    stand.pump(4);

    NetwMultiplayer *host = stand.session_of(COORDINATOR);
    NetwMultiplayer *guest = stand.session_of(MEMBER);
    REQUIRE(host != nullptr);
    REQUIRE(guest != nullptr);

    Node *mount = mount_under_root("EA4Host");
    Node *seen = mount_under_root("EA4Member");
    stand.mount(COORDINATOR, mount);
    stand.mount(MEMBER, seen);
    NETW_CHECK_EQ(int(guest->embed_settle()), int(OK));

    Node *level = a_declared_level();
    mount->add_child(level);
    host->embed_offer_bare_level(level);
    NETW_CHECK_EQ(int(host->embed_settle()), int(OK));
    stand.step_ticks(6);

    NETW_CHECK_EQ(mount->get_child_count(), 1);

    REQUIRE(stand.join(COORDINATOR, StringName("seven")).is_valid());
    REQUIRE(stand.join(MEMBER, StringName("nine")).is_valid());

    const RID opened = first_scene(host);
    NETW_CHECK_EQ(int(host->scene_list().size()), 1);
    const Ref<NetwSceneHandle> world = host->scene_handle_of(opened);
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
