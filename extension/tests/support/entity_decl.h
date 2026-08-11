#pragma once

/* One entity declared as a record, so a case says what it wants and never how
 * a session assembles it.
 *
 * An entity is a row first and a wrapper second: `entity_create` mints the row,
 * and an owner node is plumbing only the facets that need one demand. Declaring
 * it this way is what lets a case that only needs interest membership skip the
 * node entirely, and a case that needs a display track get one without ever
 * authoring it.
 *
 * [codeblock]
 * const RID alice = rig.declare_entity(
 *     EntityDecl().named("Alice").on_route(31).controlled_by(rig.peer_id(0))
 * );
 * const RID row = rig.declare_entity(EntityDecl().named("Row").wrapperless());
 * [/codeblock]
 *
 * The record names no session and stands nothing up, which is what lets a
 * driver with no session read it. `LoopbackRig::declare_entity` is where a
 * declaration meets an API, and it is hosted-only because the API it composes
 * is a script.
 */

#include "netw_test.h"

#include "godot/templates.hpp"
#include "godot/variant.hpp"
#include "netw/prediction_core.hpp"

namespace netw_test {

class EntityDecl {
    godot::StringName decl_name;
    godot::StringName decl_schema;
    int decl_route = 0;
    int decl_controller = 0;
    bool decl_wants_owner = true;
    bool decl_predicts = false;
    double decl_prediction_epsilon = 0.0;
    netw::Schedule decl_schedule = netw::Schedule::TICK;
    netw::MissingInput decl_missing_input = netw::MissingInput::STALL;
    netw::CorrectionMode decl_correction = netw::CorrectionMode::AUTO;
    int decl_replay_buffer_depth = 0;
    int decl_consume_lag = 0;
    godot::Vector<godot::StringName> decl_synced;
    godot::Variant decl_initial_pose;

public:
    // The name the owner node carries, which is what a failure message can be
    // read by and what a client looks the entity up under.
    EntityDecl &named(const godot::StringName &p_name) {
        decl_name = p_name;
        return *this;
    }

    // Pins the wire route rather than drawing one. A law that asserts over a
    // route has to name it; anything else lets the session allocate.
    EntityDecl &on_route(int p_route) {
        decl_route = p_route;
        return *this;
    }

    EntityDecl &controlled_by(int p_peer) {
        decl_controller = p_peer;
        return *this;
    }

    // Names a field the entity replicates, in declaration order. A predicting
    // driver compares exactly these fields, so a field left undeclared is one
    // no law about this entity can be broken by.
    EntityDecl &synced(const godot::StringName &p_column) {
        decl_synced.push_back(p_column);
        return *this;
    }

    EntityDecl &on_schema(const godot::StringName &p_schema) {
        decl_schema = p_schema;
        return *this;
    }

    // The value every synced field starts at. One value rather than one per
    // field, because a lane whose fields start apart is a scenario about the
    // starting spread and says so by perturbing instead.
    EntityDecl &placed_at(const godot::Variant &p_pose) {
        decl_initial_pose = p_pose;
        return *this;
    }

    EntityDecl &predicted(double p_epsilon = 0.0) {
        decl_predicts = true;
        decl_prediction_epsilon = p_epsilon;
        return *this;
    }

    EntityDecl &missing_input(netw::MissingInput p_policy) {
        decl_missing_input = p_policy;
        return *this;
    }

    // Transitions authority keeps standing behind the one it consumes, so an
    // arrival that lands late is covered by one that landed early. A depth of
    // zero consumes at the live edge and starves the moment nothing arrives.
    EntityDecl &buffered(int p_depth) {
        decl_replay_buffer_depth = p_depth;
        return *this;
    }

    // How far behind the live edge authority will walk before it gives up on
    // the transitions between and jumps. A lane that declares none never
    // strands, whatever its stream does.
    EntityDecl &consume_lag(int p_ticks) {
        decl_consume_lag = p_ticks;
        return *this;
    }

    EntityDecl &scheduled(netw::Schedule p_schedule) {
        decl_schedule = p_schedule;
        return *this;
    }

    // The correction axis, declared rather than left to AUTO. A carrier is no
    // solver body, so AUTO resolves it to REPLAY, and a lane about SNAP has to
    // say so.
    EntityDecl &corrected_by(netw::CorrectionMode p_correction) {
        decl_correction = p_correction;
        return *this;
    }

    netw::CorrectionMode correction() const {
        return decl_correction;
    }

    // Declares the row alone. The facets that need a node refuse afterwards,
    // which for a law about the record plane is the point rather than a limit.
    EntityDecl &wrapperless() {
        decl_wants_owner = false;
        return *this;
    }

    bool wants_owner() const {
        return decl_wants_owner;
    }

    const godot::StringName &name() const {
        return decl_name;
    }

    // Zero means the declaration draws no route and lets the session allocate.
    int route() const {
        return decl_route;
    }

    int controller() const {
        return decl_controller;
    }

    const godot::Vector<godot::StringName> &synced_columns() const {
        return decl_synced;
    }

    const godot::StringName &schema() const {
        return decl_schema;
    }

    const godot::Variant &initial_pose() const {
        return decl_initial_pose;
    }

    bool is_predicted() const {
        return decl_predicts;
    }

    double prediction_epsilon() const {
        return decl_prediction_epsilon;
    }

    int replay_buffer_depth() const {
        return decl_replay_buffer_depth;
    }

    int consume_lag_ticks() const {
        return decl_consume_lag;
    }

    netw::MissingInput missing_input_policy() const {
        return decl_missing_input;
    }

    netw::Schedule schedule() const {
        return decl_schedule;
    }
};

} // namespace netw_test
