#include "netw/api/netw_multiplayer.hpp"

#include "godot/callable.hpp"
#include "godot/node.hpp"
#include "godot/object.hpp"
#include "netw/api/entity.hpp"
#include "netw/log.hpp"
#include "netw/sim/body.hpp"
#include "netw/sim/row.hpp"
#include "netw/subsystems.hpp"

using namespace godot;

namespace netw {

namespace {

const StringName &network_tick_name() {
    static const StringName name("_network_tick");
    return name;
}

} // namespace

void NetwMultiplayer::sim_run(
    sim::Schedule p_phase,
    double p_delta,
    int64_t p_tick
) {
    sim_select_unpredicted(p_tick, p_phase == sim::Schedule::TICK);
    sim_rows.unpredicted(sim_stepping);
    for (const RID &entity : sim_stepping) {
        sim_step(entity, p_phase, p_delta, p_tick);
    }
}

sim::Schedule NetwMultiplayer::sim_schedule_of(sim::Row &r_row, Node *p_owner) {
    const bool has_body = r_row.bodies.recorded
        ? !r_row.bodies.held.is_empty()
        : sim::is_solver_body(p_owner) || !r_row.declaration.bodies.is_empty();
    if (r_row.declaration.schedule == sim::Schedule::STEPPED
        && !r_row.stepped_refused) {
        r_row.stepped_refused = true;
        NETW_ERROR(
            sys::PREDICTION,
            "%s declares SCHEDULE_STEPPED without prediction, and only a "
            "predicted member drives a stepped space, so it steps as "
            "SCHEDULE_FRAME",
            String(p_owner->get_name()).utf8().get_data()
        );
    }
    return sim::resolved_schedule(r_row.declaration.schedule, has_body);
}

void NetwMultiplayer::sim_step(
    const RID &p_entity,
    sim::Schedule p_phase,
    double p_delta,
    int64_t p_tick
) {
    if (predict_engine_for(p_entity) != nullptr) {
        return;
    }
    const Ref<NetwEntity> entity = entity_get_view(p_entity);
    if (entity.is_null()) {
        sim_release_row(p_entity);
        return;
    }
    Node *owner = entity->get_owner();
    sim::Row *row = sim_rows.find(p_entity);
    if (row == nullptr || owner == nullptr || !owner->is_inside_tree()) {
        return;
    }
    const sim::Mode mode = row->bodies.recorded
        ? row->mode
        : sim_rows.resolve(p_entity, sim_body_facts(entity));
    sim_announce(p_entity);
    if (!sim::executes(mode) || sim_schedule_of(*row, owner) != p_phase) {
        return;
    }
    if (p_phase == sim::Schedule::FRAME) {
        if (p_tick <= row->last_frame_tick) {
            return;
        }
        row->last_frame_tick = p_tick;
    }
    Callable step = row->declaration.step;
    if (!step.is_valid()) {
        if (!owner->has_method(network_tick_name())) {
            return;
        }
        step = Callable(owner, network_tick_name());
    }
    step.call(p_delta, p_tick, sim::leads(mode));
}

} // namespace netw
