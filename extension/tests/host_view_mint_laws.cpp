#include "support/netw_test.h"

#include "godot/callable.hpp"
#include "godot/scene_tree.hpp"
#include "godot/viewport.hpp"
#include "netw/api/entity_record.hpp"
#include "netw/api/netw_multiplayer.hpp"
#include "netw/api/nodes/view/host_scene_view.hpp"
#include "netw/scene_core.hpp"

namespace TestHostViewMintLaws {

using namespace godot;
using netw::HostSceneView;
using netw::NetwMultiplayer;
using netw::ParticipantView;

Ref<netw::NetwSessionConfig> authoring(int64_t p_role) {
    Ref<netw::NetwSessionConfig> settings;
    settings.instantiate();
    settings->set_desired_role(p_role);
    return settings;
}

Node *declared_root = nullptr;

Node *read_declared_root() {
    return declared_root;
}

Node *declines_to_mint(Node *) {
    return nullptr;
}

Node *mints_a_plain_node(Node *) {
    return memnew(Node);
}

struct Session {
    Ref<NetwMultiplayer> core;
    Node *root = nullptr;

    Session() {
        core.instantiate();
        core->session_set_role(NetwMultiplayer::ROLE_LISTEN_SERVER);
        root = memnew(Node);
        root->set_name("SessionRoot");
        netw::gd::scene_root()->add_child(root);
        declared_root = root;
        core->session_set_root(callable_mp_static(&read_declared_root));
    }

    Node *mount(const StringName &p_stem, Node *p_world) const {
        Node *owner = memnew(Node);
        owner->set_name(p_stem);
        if (p_world != nullptr) {
            p_world->set_meta(NetwMultiplayer::scene_container_meta(), true);
            root->add_child(p_world);
            p_world->add_child(owner);
        } else {
            root->add_child(owner);
        }
        Ref<netw::NetwEntity> wrapper;
        wrapper.instantiate();
        const RID handle = core->get_liveness_core()->entity_create();
        wrapper->set_rid_handle(handle);
        netw::NetwEntityRecord *record = wrapper->get_record();
        record->set_declares_scene(true);
        const int64_t route = core->get_liveness_core()->reserve_route();
        REQUIRE(core->liveness_bind(handle, route, wrapper, record, owner));
        wrapper->set_owner(owner);
        owner->set_meta(NetwMultiplayer::wrapper_meta(), wrapper);
        core->get_scene_core()->scene_enter(handle, p_stem, true);
        return owner;
    }

    Node *isolate(const StringName &p_stem) const {
        return mount(p_stem, memnew(SubViewport));
    }

    Node *shared(const StringName &p_stem) const {
        return mount(p_stem, nullptr);
    }

    int views() const {
        int seen = 0;
        for (int at = 0; at < root->get_child_count(); ++at) {
            if (Object::cast_to<ParticipantView>(root->get_child(at))
                != nullptr) {
                seen += 1;
            }
        }
        return seen;
    }

    ~Session() {
        declared_root = nullptr;
        netw::gd::scene_root()->remove_child(root);
        memdelete(root);
    }
};

TEST_CASE(
    "[Networked][View][Hosted][SceneTree] HM1 an offscreen world is what a "
    "host view exists to draw, so a host running only a scene that shares "
    "the session's world mints none and a host that opens an isolated one "
    "mints exactly one"
) {
    Session shared_world;
    shared_world.shared(StringName("Lobby"));
    shared_world.core->scene_ensure_host_view();
    NETW_CHECK_EQ(shared_world.views(), 0);

    Session isolated;
    NETW_CHECK_EQ(isolated.views(), 0);
    isolated.isolate(StringName("Arena"));
    isolated.core->scene_ensure_host_view();
    NETW_CHECK_EQ(isolated.views(), 1);
    NETW_CHECK_EQ(
        int(Object::cast_to<HostSceneView>(
                isolated.root->get_node_or_null(NodePath("HostSceneView"))
            )
            != nullptr),
        1
    );
}

TEST_CASE(
    "[Networked][View][Hosted][SceneTree] HM2 the mint reads what a session "
    "was AUTHORED to be rather than what it has resolved to, so a peer "
    "declared a client mints nothing over an isolated world it still holds, "
    "and a peer declared a host mints before its role resolves at all"
) {
    Session client;
    client.core->session_initialize(authoring(NetwMultiplayer::ROLE_CLIENT));
    client.core->session_set_role(NetwMultiplayer::ROLE_CLIENT);
    client.isolate(StringName("Arena"));
    client.core->scene_ensure_host_view();
    NETW_CHECK_EQ(client.views(), 0);

    Session dedicated;
    dedicated.core->session_initialize(
        authoring(NetwMultiplayer::ROLE_DEDICATED_SERVER)
    );
    dedicated.core->session_set_role(NetwMultiplayer::ROLE_DEDICATED_SERVER);
    dedicated.isolate(StringName("Arena"));
    dedicated.core->scene_ensure_host_view();
    NETW_CHECK_EQ(dedicated.views(), 0);

    Session unresolved;
    unresolved.core->session_set_role(NetwMultiplayer::ROLE_NONE);
    unresolved.isolate(StringName("Arena"));
    unresolved.core->scene_ensure_host_view();
    NETW_CHECK_EQ(unresolved.views(), 1);
}

TEST_CASE(
    "[Networked][View][Hosted][SceneTree] HM3 a display already owned is not "
    "given a second, so minting again is inert and a view the author placed "
    "themselves keeps the display rather than being doubled"
) {
    Session minted;
    minted.isolate(StringName("Arena"));
    minted.core->scene_ensure_host_view();
    minted.core->scene_ensure_host_view();
    minted.core->scene_ensure_host_view();
    NETW_CHECK_EQ(minted.views(), 1);

    Session placed;
    placed.isolate(StringName("Arena"));
    ParticipantView *own = memnew(ParticipantView);
    own->set_name("MyOwnView");
    placed.root->add_child(own);
    placed.core->scene_ensure_host_view();
    NETW_CHECK_EQ(placed.views(), 1);
    NETW_CHECK_EQ(
        int(placed.root->get_node_or_null(NodePath("MyOwnView")) == own),
        1
    );
}

TEST_CASE(
    "[Networked][View][Hosted][SceneTree] HM4 a session frees only the view "
    "it built, so releasing takes the minted one away, releasing again is "
    "inert, and a view the author placed survives every release"
) {
    Session session;
    session.isolate(StringName("Arena"));
    session.core->scene_ensure_host_view();
    NETW_CHECK_EQ(session.views(), 1);

    session.core->scene_release_host_view();
    NETW_CHECK_EQ(session.views(), 0);
    session.core->scene_release_host_view();
    NETW_CHECK_EQ(session.views(), 0);

    ParticipantView *own = memnew(ParticipantView);
    own->set_name("MyOwnView");
    session.root->add_child(own);
    session.core->scene_release_host_view();
    NETW_CHECK_EQ(session.views(), 1);
}

TEST_CASE(
    "[Networked][View][Hosted][SceneTree] HM5 an installed factory replaces "
    "the mint outright rather than filtering it, so the session parents "
    "whatever node the factory answers, mints nothing of its own when the "
    "factory declines, and stops asking once the factory is taken away"
) {
    Session declining;
    declining.isolate(StringName("Arena"));
    NetwMultiplayer::scene_set_host_view_factory(
        callable_mp_static(&declines_to_mint)
    );
    declining.core->scene_ensure_host_view();
    NETW_CHECK_EQ(declining.views(), 0);
    NETW_CHECK_EQ(
        int(declining.root->get_node_or_null(NodePath("HostSceneView"))
            != nullptr),
        0
    );

    Session custom;
    custom.isolate(StringName("Arena"));
    NetwMultiplayer::scene_set_host_view_factory(
        callable_mp_static(&mints_a_plain_node)
    );
    custom.core->scene_ensure_host_view();
    NETW_CHECK_EQ(custom.views(), 0);
    Node *made = custom.root->get_node_or_null(NodePath("HostSceneView"));
    NETW_CHECK_EQ(int(made != nullptr), 1);
    NETW_CHECK_EQ(int(Object::cast_to<HostSceneView>(made) != nullptr), 0);

    NetwMultiplayer::scene_set_host_view_factory(Callable());

    Session stock;
    stock.isolate(StringName("Arena"));
    stock.core->scene_ensure_host_view();
    NETW_CHECK_EQ(
        int(Object::cast_to<HostSceneView>(
                stock.root->get_node_or_null(NodePath("HostSceneView"))
            )
            != nullptr),
        1
    );
}

} // namespace TestHostViewMintLaws
