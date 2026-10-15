#include "support/fault_log.h"
#include "support/netw_test.h"

#include "godot/node.hpp"
#include "netw/api/entity.hpp"
#include "netw/api/entity_record.hpp"
#include "netw/api/netw_multiplayer.hpp"

namespace TestSceneWatchAuthorityLaws {

using namespace godot;
using netw::NetwMultiplayer;

struct DeclaredScene {
    Node *root = nullptr;
    Ref<netw::NetwEntity> entity;
    RID handle;
};

Ref<NetwMultiplayer> a_session_at(int p_unique_id) {
    Ref<NetwMultiplayer> session;
    session.instantiate();
    session->session_peer_assigned(true, p_unique_id);
    return session;
}

DeclaredScene declare_scene(
    const Ref<NetwMultiplayer> &p_core,
    const char *p_stem
) {
    DeclaredScene made;
    made.entity.instantiate();
    made.root = memnew(Node);
    made.root->set_name(p_stem);
    made.entity->attach_to(made.root);
    made.handle = p_core->get_liveness_core()->entity_create();
    made.entity->get_record()->adopt_handle(made.handle);
    REQUIRE(p_core->entity_of(made.root) == made.handle);
    return made;
}

TEST_CASE(
    "[Networked][Scene][Hosted] L1 scene_watch asks is_host, so the server "
    "seats a viewer and a client session is refused"
) {
    netw_test::FaultWatch expected_faults;
    expected_faults.expect_error(
        "[error][scene] Condition \"!is_host()\" is true. Returning: "
        "ERR_UNAUTHORIZED. NetwMultiplayer.scene_watch runs only on"
        " the server, and was called for peer * on a client.",
        1
    );

    Ref<NetwMultiplayer> owning = a_session_at(1);
    REQUIRE(owning->is_host());
    const DeclaredScene arena = declare_scene(owning, "Arena");

    NETW_CHECK_EQ(int(owning->scene_watch(arena.handle, 9)), int(OK));
    CHECK(owning->scene_subscribes(arena.handle, 9));

    Ref<NetwMultiplayer> ordinary = a_session_at(7);
    REQUIRE_FALSE(ordinary->is_host());
    const DeclaredScene annex = declare_scene(ordinary, "Annex");

    NETW_CHECK_EQ(
        int(ordinary->scene_watch(annex.handle, 9)),
        int(ERR_UNAUTHORIZED)
    );
    CHECK_FALSE(ordinary->scene_subscribes(annex.handle, 9));

    memdelete(arena.root);
    memdelete(annex.root);
}

TEST_CASE(
    "[Networked][Scene][Hosted] L2 scene_unwatch asks is_host, so a released "
    "seat settles once on the server and a client session cannot release "
    "anything"
) {
    netw_test::FaultWatch expected_faults;
    expected_faults.expect_error(
        "[error][scene] Condition \"!is_host()\" is true. Returning: "
        "false. NetwMultiplayer.scene_unwatch runs only on the "
        "server, and was called for peer * on a client.",
        1
    );

    Ref<NetwMultiplayer> owning = a_session_at(1);
    const DeclaredScene arena = declare_scene(owning, "Arena");
    REQUIRE(int(owning->scene_watch(arena.handle, 9)) == int(OK));
    REQUIRE(owning->scene_subscribes(arena.handle, 9));

    CHECK(owning->scene_unwatch(arena.handle, 9));
    CHECK_FALSE(owning->scene_subscribes(arena.handle, 9));
    CHECK_FALSE(owning->scene_release_peer(arena.handle, 9));

    Ref<NetwMultiplayer> ordinary = a_session_at(7);
    const DeclaredScene annex = declare_scene(ordinary, "Annex");

    CHECK_FALSE(ordinary->scene_unwatch(annex.handle, 9));

    memdelete(arena.root);
    memdelete(annex.root);
}

} // namespace TestSceneWatchAuthorityLaws
