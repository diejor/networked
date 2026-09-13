#include "support/netw_test.h"

#include "godot/node.hpp"
#include "godot/scene_tree.hpp"
#include "netw/api/entity.hpp"
#include "netw/api/entity_options.hpp"
#include "netw/api/entity_record.hpp"
#include "netw/api/event_plane.hpp"
#include "netw/api/netw_multiplayer.hpp"

namespace TestNetwEntityLifeReportLaws {

using namespace godot;
using netw::EventPlane;
using netw::NetwEntity;
using netw::NetwEntityRecord;
using netw::NetwMultiplayer;
using netw::NetwReparentOpts;

constexpr int64_t REPORTED_ROUTE = 11;

Ref<NetwMultiplayer> a_watching_session() {
    Ref<NetwMultiplayer> core;
    core.instantiate();
    core->session_set_role(NetwMultiplayer::ROLE_LISTEN_SERVER);
    core->event_arm(true);
    PackedInt64Array events;
    events.push_back(EventPlane::SPAWNING);
    events.push_back(EventPlane::REPARENTED);
    core->event_watch(
        events,
        Dictionary(),
        Dictionary(),
        Callable(),
        Dictionary()
    );
    return core;
}

Dictionary row_of(
    const Ref<NetwMultiplayer> &p_core,
    int64_t p_route,
    int64_t p_event
) {
    const Array rows = p_core->event_ring(p_route);
    for (int index = 0; index < rows.size(); index++) {
        const Dictionary row = rows[index];
        if (!row.is_empty()
            && int64_t(row[netw::event_key::event()]) == p_event) {
            return row;
        }
    }
    return Dictionary();
}

TEST_CASE(
    "[Networked][Entity][Hosted] EL1 a body that materializes reports it on "
    "its own route, so a watcher learns the entity that asked for the route "
    "the session already published as live"
) {
    Ref<NetwMultiplayer> core = a_watching_session();
    Node *owner = memnew(Node);
    owner->set_name("Born");
    const Ref<NetwEntity> wrapper = NetwEntity::ensure(owner);
    REQUIRE(wrapper.is_valid());
    NetwEntityRecord *const record = wrapper->get_record();
    REQUIRE(record != nullptr);
    record->set_entity_id(StringName("born"));
    record->set_peer_id(4);
    record->set_route(REPORTED_ROUTE);

    NetwMultiplayer::entity_enter_tree(
        wrapper.ptr(),
        owner,
        record,
        core.ptr(),
        true
    );

    const Dictionary materialized
        = row_of(core, REPORTED_ROUTE, EventPlane::SPAWNING);
    CHECK(!materialized.is_empty());
    if (!materialized.is_empty()) {
        const StringName entity_id = materialized[netw::event_key::entity_id()];
        CHECK(entity_id == record->get_entity_id());
        NETW_CHECK_EQ(
            int64_t(materialized[netw::event_key::peer()]),
            record->get_peer_id()
        );
    }

    memdelete(owner);
}

TEST_CASE(
    "[Networked][Entity][Hosted][SceneTree] EL2 a settled move reports the "
    "reason that asked for it, because a mover and a watcher agree on why a "
    "body changed parents only if the row carries the word the caller used"
) {
    Ref<NetwMultiplayer> core = a_watching_session();
    Node *root = netw::gd::scene_root();
    REQUIRE(root != nullptr);
    Node *owner = memnew(Node);
    owner->set_name("Moved");
    root->add_child(owner);

    const Ref<NetwEntity> wrapper = NetwEntity::ensure(owner);
    REQUIRE(wrapper.is_valid());
    NetwEntityRecord *const record = wrapper->get_record();
    REQUIRE(record != nullptr);
    record->set_route(REPORTED_ROUTE);

    NetwEntityRecord::MoveReport report;
    report.reason = StringName("law_move");

    core->entity_announce_reparented(wrapper, report);

    const Dictionary moved
        = row_of(core, REPORTED_ROUTE, EventPlane::REPARENTED);
    CHECK(!moved.is_empty());
    if (!moved.is_empty()) {
        const Dictionary detail = moved[netw::event_key::detail()];
        CHECK(
            StringName(detail.get("reason", StringName()))
            == StringName("law_move")
        );
        CHECK_FALSE(detail.has("moved"));
    }

    root->remove_child(owner);
    memdelete(owner);
}

} // namespace TestNetwEntityLifeReportLaws
