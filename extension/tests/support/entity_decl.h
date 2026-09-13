#pragma once

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
    bool decl_mounted = false;
    bool decl_carries = false;
    double decl_carry_gain = 1.0;
    double decl_teleport_at = 0.0;
    godot::Vector<godot::StringName> decl_synced;
    godot::Variant decl_initial_pose;

public:
    EntityDecl &named(const godot::StringName &p_name) {
        decl_name = p_name;
        return *this;
    }

    EntityDecl &on_route(int p_route) {
        decl_route = p_route;
        return *this;
    }

    EntityDecl &controlled_by(int p_peer) {
        decl_controller = p_peer;
        return *this;
    }

    EntityDecl &synced(const godot::StringName &p_column) {
        decl_synced.push_back(p_column);
        return *this;
    }

    EntityDecl &on_schema(const godot::StringName &p_schema) {
        decl_schema = p_schema;
        return *this;
    }

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

    EntityDecl &buffered(int p_depth) {
        decl_replay_buffer_depth = p_depth;
        return *this;
    }

    EntityDecl &consume_lag(int p_ticks) {
        decl_consume_lag = p_ticks;
        return *this;
    }

    EntityDecl &mounted() {
        decl_mounted = true;
        return *this;
    }

    bool is_mounted() const {
        return decl_mounted;
    }

    EntityDecl &carried(double p_gain, double p_teleport_at) {
        decl_carries = true;
        decl_carry_gain = p_gain;
        decl_teleport_at = p_teleport_at;
        return *this;
    }

    bool carries() const {
        return decl_carries;
    }

    double carry_gain() const {
        return decl_carry_gain;
    }

    double teleport_at() const {
        return decl_teleport_at;
    }

    EntityDecl &scheduled(netw::Schedule p_schedule) {
        decl_schedule = p_schedule;
        return *this;
    }

    EntityDecl &corrected_by(netw::CorrectionMode p_correction) {
        decl_correction = p_correction;
        return *this;
    }

    netw::CorrectionMode correction() const {
        return decl_correction;
    }

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
