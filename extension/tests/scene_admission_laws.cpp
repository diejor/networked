#include "support/netw_test.h"

#include "godot/node.hpp"
#include "netw/api/entity.hpp"
#include "netw/api/entity_record.hpp"
#include "netw/api/netw_multiplayer.hpp"
#include "support/netw_call_log.h"

namespace TestNetwSceneAdmissionLaws {

using namespace godot;
using netw::NetwMultiplayerCore;
using netw_test::CallLog;

struct DeclaredScene {
    Node *container = nullptr;
    Ref<netw::NetwEntity> entity;
    RID handle;
};

DeclaredScene declare_scene(
    const Ref<NetwMultiplayerCore> &p_core,
    const char *p_stem
) {
    DeclaredScene made;
    made.container = memnew(Node);
    if (p_stem != nullptr) {
        Node *level = memnew(Node);
        level->set_name(p_stem);
        made.container->add_child(level);
    }
    made.entity.instantiate();
    made.entity->attach_to(made.container);
    made.handle = p_core->get_liveness_core()->entity_create();
    made.entity->get_record()->adopt_handle(made.handle);
    REQUIRE(p_core->entity_of(made.container) == made.handle);
    return made;
}

TEST_CASE(
    "[Networked][Scene][Hosted] SA1 admitting a peer writes the boundary and "
    "requests the flush that publishes it, and admitting the same peer again "
    "writes nothing and requests nothing"
) {
    Ref<NetwMultiplayerCore> core;
    core.instantiate();
    const CallLog flushed;
    core->set_interest_flush(flushed.callable("flush"));
    const DeclaredScene arena = declare_scene(core, "Arena");
    netw::InterestEngine &engine = core->interest_plane();

    REQUIRE(core->scene_layer_id(arena.handle) == StringName("scene:Arena"));
    CHECK_FALSE(core->interest_flush_pending());

    CHECK(core->scene_admit_peer(arena.handle, 7));
    CHECK(engine.layer_has_viewer(StringName("scene:Arena"), 7));
    CHECK(core->interest_flush_pending());

    core->settle_drain();

    NETW_CHECK_EQ(flushed.count("flush"), 1);
    CHECK_FALSE(core->interest_flush_pending());

    CHECK_FALSE(core->scene_admit_peer(arena.handle, 7));
    CHECK_FALSE(core->interest_flush_pending());

    memdelete(arena.container);
}

TEST_CASE(
    "[Networked][Scene][Hosted] SA2 releasing a peer clears the boundary and "
    "requests the flush in the same act, so a release nothing follows cannot "
    "leave a departed peer admitted"
) {
    Ref<NetwMultiplayerCore> core;
    core.instantiate();
    const CallLog flushed;
    core->set_interest_flush(flushed.callable("flush"));
    const DeclaredScene arena = declare_scene(core, "Arena");
    netw::InterestEngine &engine = core->interest_plane();

    REQUIRE(core->scene_admit_peer(arena.handle, 7));
    core->settle_drain();
    REQUIRE_FALSE(core->interest_flush_pending());

    CHECK(core->scene_release_peer(arena.handle, 7));
    CHECK_FALSE(engine.layer_has_viewer(StringName("scene:Arena"), 7));
    CHECK(core->interest_flush_pending());

    core->settle_drain();

    NETW_CHECK_EQ(flushed.count("flush"), 2);
    CHECK_FALSE(core->scene_release_peer(arena.handle, 7));
    CHECK_FALSE(core->interest_flush_pending());

    memdelete(arena.container);
}

TEST_CASE(
    "[Networked][Scene][Hosted] SA3 the admission itself mints the boundary, "
    "so a scene armed this frame admits rather than dropping the admission"
) {
    Ref<NetwMultiplayerCore> core;
    core.instantiate();
    const CallLog flushed;
    core->set_interest_flush(flushed.callable("flush"));
    const DeclaredScene arena = declare_scene(core, "Arena");
    netw::InterestEngine &engine = core->interest_plane();

    CHECK_FALSE(engine.has_layer(StringName("scene:Arena")));

    CHECK(core->scene_admit_peer(arena.handle, 7));

    CHECK(engine.has_layer(StringName("scene:Arena")));

    const DeclaredScene annex = declare_scene(core, "Annex");

    CHECK_FALSE(core->scene_release_peer(annex.handle, 7));
    CHECK_FALSE(engine.has_layer(StringName("scene:Annex")));

    memdelete(annex.container);
    memdelete(arena.container);
}

TEST_CASE(
    "[Networked][Scene][Hosted] SA4 a scene naming no boundary and a peer of "
    "zero admit nobody and request no flush, so a refusal costs no recompute"
) {
    Ref<NetwMultiplayerCore> core;
    core.instantiate();
    const CallLog flushed;
    core->set_interest_flush(flushed.callable("flush"));
    const DeclaredScene hollow = declare_scene(core, nullptr);
    const DeclaredScene arena = declare_scene(core, "Arena");

    REQUIRE(core->scene_layer_id(hollow.handle) == StringName());

    CHECK_FALSE(core->scene_admit_peer(hollow.handle, 7));
    CHECK_FALSE(core->scene_release_peer(hollow.handle, 7));
    CHECK_FALSE(core->scene_admit_peer(RID(), 7));
    CHECK_FALSE(core->scene_admit_peer(arena.handle, 0));
    CHECK_FALSE(core->scene_release_peer(arena.handle, 0));

    CHECK_FALSE(core->interest_flush_pending());
    NETW_CHECK_EQ(flushed.count("flush"), 0);

    memdelete(arena.container);
    memdelete(hollow.container);
}

TEST_CASE(
    "[Networked][Scene][Hosted] SA5 an admission made with no flush installed "
    "still writes the boundary and schedules nothing, so the rows it wrote "
    "stay uncommitted rather than the write being lost"
) {
    Ref<NetwMultiplayerCore> core;
    core.instantiate();
    const DeclaredScene arena = declare_scene(core, "Arena");
    netw::InterestEngine &engine = core->interest_plane();

    ERR_PRINT_OFF;
    CHECK(core->scene_admit_peer(arena.handle, 7));
    ERR_PRINT_ON;

    CHECK(engine.layer_has_viewer(StringName("scene:Arena"), 7));
    CHECK_FALSE(core->interest_flush_pending());
    NETW_CHECK_EQ(core->settle_pending(), 0);

    memdelete(arena.container);
}

TEST_CASE(
    "[Networked][Scene][Hosted] SA6 every admission edge in one pump coalesces "
    "under the session's own flush key, so a cascade settles once and settles "
    "after the whole cascade"
) {
    Ref<NetwMultiplayerCore> core;
    core.instantiate();
    const CallLog flushed;
    core->set_interest_flush(flushed.callable("flush"));
    const DeclaredScene arena = declare_scene(core, "Arena");
    const DeclaredScene annex = declare_scene(core, "Annex");

    CHECK(
        NetwMultiplayerCore::interest_flush_key()
        == StringName("interest_visibility")
    );

    REQUIRE(core->scene_admit_peer(arena.handle, 7));
    REQUIRE(core->scene_admit_peer(arena.handle, 8));
    REQUIRE(core->scene_admit_peer(annex.handle, 9));
    REQUIRE(core->scene_release_peer(arena.handle, 8));

    NETW_CHECK_EQ(core->settle_pending(), 1);

    core->settle_drain();

    NETW_CHECK_EQ(flushed.count("flush"), 1);

    memdelete(annex.container);
    memdelete(arena.container);
}

} // namespace TestNetwSceneAdmissionLaws
