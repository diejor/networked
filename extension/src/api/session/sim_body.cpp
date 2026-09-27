#include "netw/api/netw_multiplayer.hpp"

#include "godot/callable.hpp"
#include "godot/node.hpp"
#include "godot/object.hpp"
#include "netw/api/entity.hpp"
#include "netw/api/property_set_binding.hpp"
#include "netw/api/replication_core.hpp"
#include "netw/display/book.hpp"
#include "netw/sim/body.hpp"
#include "netw/sim/row.hpp"
#include "netw/sync_authoring.hpp"

using namespace godot;

namespace netw {

namespace {

const StringName &control_changed_name() {
    static const StringName name("control_changed");
    return name;
}

const StringName &network_tick_name() {
    static const StringName name("_network_tick");
    return name;
}

} // namespace

sim::Facts NetwMultiplayer::sim_body_facts(
    const Ref<NetwEntity> &p_entity
) const {
    sim::Facts facts;
    facts.declared = true;
    facts.state_rows = p_entity->get_state_binding().is_valid();
    facts.session_authority_here = is_host();
    facts.controller_is_nobody = p_entity->get_controller() == 0;
    facts.controller_here
        = !facts.controller_is_nobody && p_entity->is_controller_here();
    facts.pending_claim_here = p_entity->is_claim_running_ahead();
    return facts;
}

void NetwMultiplayer::sim_follow_claim(const Ref<NetwEntity> &p_entity) {
    if (p_entity.is_null()) {
        return;
    }
    sim_settle_body(p_entity);
    display_mark_role_dirty(p_entity->get_rid_handle());
}

void NetwMultiplayer::sim_settle_body(const Ref<NetwEntity> &p_entity) {
    if (p_entity.is_null()) {
        return;
    }
    Node *owner = p_entity->get_owner();
    const RID entity = p_entity->get_rid_handle();
    if (owner == nullptr || !owner->is_inside_tree() || !entity.is_valid()) {
        return;
    }
    const sim::Row *standing = sim_rows.row_of(entity);
    const bool declares_bodies
        = standing != nullptr && !standing->declaration.bodies.is_empty();
    const bool implicit = sim::is_solver_body(owner)
        && display_book->runtime_of(entity) != nullptr;
    if (!declares_bodies && !implicit) {
        return;
    }
    const bool seated = standing != nullptr && standing->facts.predicted;
    sim::Mode mode = sim::Mode::NONE;
    if (seated) {
        mode = standing->mode;
    } else if (
        predict_engine_for(entity) != nullptr
        || authoring::declares_prediction(owner)
    ) {
        return;
    } else {
        mode = sim_rows.resolve(entity, sim_body_facts(p_entity));
    }
    sim::Row &row = sim_rows.ensure(entity);
    if (!row.bodies.recorded) {
        sim::record_authored(
            row.bodies,
            owner,
            row.declaration.bodies,
            display_config_for(p_entity).visual_root
        );
        row.owner_ticks = owner->has_method(network_tick_name());
        row.control_hook
            = callable_mp(this, &NetwMultiplayer::sim_on_body_control)
                  .bind(entity);
        p_entity->connect(control_changed_name(), row.control_hook);
        if (display::Runtime *runtime = display_book->runtime_of(entity)) {
            display_seat_bodies(runtime);
        }
    }
    if (sim::transition(row.bodies, mode) && !seated) {
        sim_install_newest(p_entity);
    }
    if (!seated) {
        sim_announce(entity);
    }
}

bool NetwMultiplayer::sim_draws_column(
    const RID &p_entity,
    Node *p_node,
    const StringName &p_key
) const {
    const sim::Row *row = sim_rows.row_of(p_entity);
    if (row == nullptr || row->bodies.applied != sim::Mode::PROXY) {
        return false;
    }
    display::Runtime *runtime = display_book->runtime_of(p_entity);
    if (runtime == nullptr
        || runtime->get_pump_mode() != display::PUMP_REMOTE) {
        return false;
    }
    const int64_t at = runtime->display_tracks().by_key(
        display::state_key_for(p_node, p_key)
    );
    if (at < 0) {
        return false;
    }
    Object *drawn = runtime->channels()[at]->get_target_obj();
    return runtime->holds_body(drawn);
}

void NetwMultiplayer::sim_settle_body_of(const RID &p_entity) {
    sim_settle_body(entity_get_view(p_entity));
}

void NetwMultiplayer::sim_on_body_control(
    int64_t,
    int64_t,
    const RID &p_entity
) {
    sim_settle_body_of(p_entity);
}

void NetwMultiplayer::sim_install_newest(const Ref<NetwEntity> &p_entity) {
    ReplicationCore *plane = get_replication_plane();
    if (plane == nullptr || p_entity.is_null()) {
        return;
    }
    const int64_t live = liveness_route_of(p_entity.ptr());
    const int64_t route = live > 0 ? live : p_entity->get_route();
    const TypedArray<NetwPropertySetBinding> bindings
        = plane->derived_group(route);
    for (int at = 0; at < bindings.size(); at++) {
        const Ref<NetwPropertySetBinding> binding = bindings[at];
        if (binding.is_valid()) {
            binding->reinstall_accepted();
        }
    }
}

void NetwMultiplayer::sim_release_body(const RID &p_entity, bool p_restore) {
    sim::Row *row = sim_rows.find(p_entity);
    if (row == nullptr) {
        return;
    }
    if (p_restore) {
        sim::release(row->bodies);
    } else {
        row->bodies = sim::Bodies();
    }
    const Ref<NetwEntity> entity = entity_get_view(p_entity);
    if (entity.is_valid() && row->control_hook.is_valid()
        && entity->is_connected(control_changed_name(), row->control_hook)) {
        entity->disconnect(control_changed_name(), row->control_hook);
    }
    row->control_hook = Callable();
    if (!row->facts.predicted) {
        sim_release_row(p_entity);
    }
}

void NetwMultiplayer::sim_hold_poses(
    const RID &p_space,
    const LocalVector<RID> &p_members,
    LocalVector<sim::HeldPose> &r_poses
) const {
    sim_rows.hold_poses(p_space, p_members, r_poses);
}

} // namespace netw
