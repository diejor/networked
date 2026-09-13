#include "support/netw_test.h"

#include "godot/node.hpp"
#include "netw/api/entity.hpp"
#include "netw/api/interest_handle.hpp"
#include "netw/api/interest_layer.hpp"
#include "netw/api/netw_multiplayer.hpp"
#include "support/entity_decl.h"
#include "support/loopback_rig.h"
#include "support/netw_call_log.h"

namespace TestNetwInterestLayerBook {

using namespace godot;
using netw::NetwEntity;
using netw::NetwInterestLayer;
using netw::NetwMultiplayer;
using netw_test::CallLog;
using netw_test::EntityDecl;
using netw_test::LoopbackRig;

Ref<NetwEntity> subject(LoopbackRig &p_rig, const char *p_name, int p_route) {
    const RID handle
        = p_rig.declare_entity(EntityDecl().named(p_name).on_route(p_route));
    return p_rig.server()->entity_get_view(handle);
}

bool ordered_as(
    const Vector<StringName> &p_seen,
    std::initializer_list<const char *> p_expected
) {
    if (p_seen.size() != int(p_expected.size())) {
        return false;
    }
    int at = 0;
    for (const char *tag : p_expected) {
        if (p_seen[at] != StringName(tag)) {
            return false;
        }
        ++at;
    }
    return true;
}

String spelled(const Vector<StringName> &p_seen) {
    String out;
    for (int at = 0; at < p_seen.size(); ++at) {
        out += at == 0 ? String(p_seen[at]) : String(",") + String(p_seen[at]);
    }
    return out;
}

TEST_CASE(
    "[Networked][Interest] IB1 a mutation the layer's book already "
    "holds is refused and announced to nobody, so a caller that enrols the "
    "same viewer twice cannot make a watcher believe two peers arrived"
) {
    LoopbackRig rig;
    const Ref<NetwEntity> entity = subject(rig, "BookSubject", 61);
    const Ref<NetwInterestLayer> layer
        = rig.server()->interest_layer(StringName("sight"));
    REQUIRE(layer.is_valid());
    const CallLog heard;
    layer->connect(StringName("viewer_added"), heard.callable("viewer"));
    layer->connect(StringName("entity_added"), heard.callable("member"));
    layer->connect(StringName("viewer_removed"), heard.callable("unviewer"));
    layer->connect(StringName("entity_removed"), heard.callable("unmember"));

    CHECK(layer->add_viewer(7));
    CHECK_FALSE(layer->add_viewer(7));
    CHECK(layer->add_entity(entity));
    CHECK_FALSE(layer->add_entity(entity));

    NETW_CHECK_EQ(heard.count("viewer"), 1);
    NETW_CHECK_EQ(heard.count("member"), 1);

    CHECK(layer->remove_viewer(7));
    CHECK_FALSE(layer->remove_viewer(7));
    CHECK(layer->remove_entity(entity));
    CHECK_FALSE(layer->remove_entity(entity));

    NETW_CHECK_EQ(heard.count("unviewer"), 1);
    NETW_CHECK_EQ(heard.count("unmember"), 1);
}

TEST_CASE(
    "[Networked][Interest] IB2 a client-side admission tells the "
    "entity it was admitted before it tells the layer's watchers the copy is "
    "visible, so a watcher that reads the entity's own state during the "
    "visible edge reads the state the admission already wrote"
) {
    LoopbackRig rig;
    const Ref<NetwEntity> entity = subject(rig, "AdmitSubject", 62);
    NetwMultiplayer *core = rig.server();
    const Ref<NetwInterestLayer> layer
        = core->interest_layer(StringName("sight"));
    REQUIRE(layer.is_valid());
    CallLog heard;
    entity->get_interest()->on_enter(
        heard.callable("enter"),
        StringName("sight")
    );
    entity->get_interest()->on_leave(
        heard.callable("leave"),
        StringName("sight")
    );
    layer->connect(StringName("entity_visible"), heard.callable("visible"));
    layer->connect(StringName("entity_hidden"), heard.callable("hidden"));
    layer->connect(StringName("entity_removed"), heard.callable("removed"));

    layer->client_admit(entity);

    CHECK(layer->has_entity(entity));
    CHECK_MESSAGE(
        ordered_as(heard.order(), {"enter", "visible"}),
        "admission announced ",
        spelled(heard.order())
    );

    layer->client_revoke(entity);

    CHECK_FALSE(layer->has_entity(entity));
    CHECK_MESSAGE(
        ordered_as(heard.order(), {"enter", "visible", "leave", "hidden"}),
        "revocation announced ",
        spelled(heard.order())
    );

    layer->client_admit(entity);
    heard.clear();
    layer->client_untrack_entity(entity);

    CHECK_FALSE(layer->has_entity(entity));
    CHECK_MESSAGE(
        ordered_as(heard.order(), {"removed", "leave", "hidden"}),
        "untracking announced ",
        spelled(heard.order())
    );
}

TEST_CASE(
    "[Networked][Interest] IB3 a layer's snapshot separates what was "
    "asked for from what was committed: viewers and members count the moment "
    "they are stated, while edges and transitions count only what a flush "
    "actually published"
) {
    LoopbackRig rig;
    const Ref<NetwEntity> entity = subject(rig, "SnapshotSubject", 63);
    NetwMultiplayer *core = rig.server();
    const Ref<NetwInterestLayer> layer
        = core->interest_layer(StringName("sight"));
    REQUIRE(layer.is_valid());

    layer->add_viewer(rig.peer_id(0));
    layer->add_entity(entity);

    const Dictionary asked = layer->monitor_snapshot();
    NETW_CHECK_EQ(int(asked[StringName("viewers")]), 1);
    NETW_CHECK_EQ(int(asked[StringName("entities")]), 1);
    NETW_CHECK_EQ(int(asked[StringName("visible_edges")]), 0);
    NETW_CHECK_EQ(int(asked[StringName("transitions_total")]), 0);

    rig.flush_interest();

    const Dictionary committed = layer->monitor_snapshot();
    NETW_CHECK_EQ(int(committed[StringName("visible_edges")]), 1);
    NETW_CHECK_EQ(int(committed[StringName("transitions_total")]), 1);

    layer->remove_entity(entity);
    rig.flush_interest();

    const Dictionary forgotten = layer->monitor_snapshot();
    NETW_CHECK_EQ(int(forgotten[StringName("visible_edges")]), 0);
    NETW_CHECK_EQ(int(forgotten[StringName("transitions_total")]), 2);
}

TEST_CASE(
    "[Networked][Interest] IB4 the plane's snapshot counts the "
    "entities the committed row filters rather than the entities the session "
    "holds, and its edge total is the one the layer reports"
) {
    LoopbackRig rig;
    const Ref<NetwEntity> filtered = subject(rig, "FilteredSubject", 64);
    subject(rig, "OpenSubject", 65);
    NetwMultiplayer *core = rig.server();
    const Ref<NetwInterestLayer> layer
        = core->interest_layer(StringName("sight"));
    REQUIRE(layer.is_valid());

    layer->add_viewer(rig.peer_id(0));
    layer->add_entity(filtered);
    rig.flush_interest();

    const Dictionary plane = core->interest_monitor_snapshot();
    NETW_CHECK_EQ(int(plane[StringName("entities_filtered")]), 1);
    NETW_CHECK_EQ(
        int(plane[StringName("visible_edges")]),
        int(layer->monitor_snapshot()[StringName("visible_edges")])
    );
    CHECK(int(plane[StringName("layers")]) >= 1);
    CHECK(int(plane[StringName("transitions_total")]) >= 1);
}

} // namespace TestNetwInterestLayerBook
