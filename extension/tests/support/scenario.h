#pragma once

#include "world_decl.h"

#include "godot/templates.hpp"
#include "godot/variant.hpp"
#include "netw/api/loopback.hpp"

namespace netw_test {

struct Scenario {
    struct Step {
        int tick = 0;
        godot::StringName verb;
        godot::StringName subject;
        godot::Variant value;
    };

    godot::String label;
    WorldDecl world;
    int clients = 1;
    int warmup_ticks = 0;
    int run_ticks = 0;
    double epsilon = 0.0;
    godot::Ref<netw::LocalLinkConditions> link_conditions;
    godot::Ref<netw::LocalLinkConditions> inbound_conditions;
    godot::PackedInt32Array frame_ticks;
    godot::PackedInt32Array authority_frame_ticks;
    godot::Vector<Step> steps;

    Scenario &hold_input(
        int p_tick,
        const godot::StringName &p_who,
        const godot::Variant &p_value
    ) {
        return step(p_tick, "hold_input", p_who, p_value);
    }

    Scenario &release_input(int p_tick, const godot::StringName &p_who) {
        return step(p_tick, "release_input", p_who, godot::Variant());
    }

    Scenario &input_at(
        int p_tick,
        const godot::StringName &p_who,
        const godot::Variant &p_value
    ) {
        return step(p_tick, "input_at", p_who, p_value);
    }

    Scenario &perturb(
        int p_tick,
        const godot::StringName &p_who,
        const godot::Variant &p_value
    ) {
        return step(p_tick, "perturb", p_who, p_value);
    }

    Scenario &undeclare(int p_tick, const godot::StringName &p_who) {
        return step(p_tick, "undeclare", p_who, godot::Variant());
    }

    Scenario &sample_at(int p_tick, const godot::StringName &p_who) {
        return step(p_tick, "sample_at", p_who, godot::Variant());
    }

    Scenario &rewind_at(int p_tick, const godot::StringName &p_who) {
        return step(p_tick, "rewind_at", p_who, godot::Variant());
    }

    Scenario &admit(
        int p_tick,
        const godot::StringName &p_scene,
        int p_client
    ) {
        return step(p_tick, "admit", p_scene, p_client);
    }

    Scenario &release(
        int p_tick,
        const godot::StringName &p_scene,
        int p_client
    ) {
        return step(p_tick, "release", p_scene, p_client);
    }

    Scenario &seat(
        int p_tick,
        const godot::StringName &p_who,
        const godot::StringName &p_destination
    ) {
        return step(p_tick, "seat", p_who, p_destination);
    }

    Scenario &move(
        int p_tick,
        const godot::StringName &p_who,
        const godot::StringName &p_destination
    ) {
        return step(p_tick, "move", p_who, p_destination);
    }

    Scenario &until(int p_tick) {
        run_ticks = p_tick;
        return *this;
    }

    Scenario &conditions(
        const godot::Ref<netw::LocalLinkConditions> &p_conditions
    ) {
        link_conditions = p_conditions;
        return *this;
    }

    Scenario &inbound(
        const godot::Ref<netw::LocalLinkConditions> &p_conditions
    ) {
        inbound_conditions = p_conditions;
        return *this;
    }

    Scenario &schedule(const godot::PackedInt32Array &p_ticks) {
        frame_ticks = p_ticks;
        return *this;
    }

    Scenario &authority_schedule(const godot::PackedInt32Array &p_ticks) {
        authority_frame_ticks = p_ticks;
        return *this;
    }

    bool declares(const godot::StringName &p_verb) const {
        for (int index = 0; index < steps.size(); ++index) {
            if (steps[index].verb == p_verb) {
                return true;
            }
        }
        return false;
    }

private:
    Scenario &step(
        int p_tick,
        const godot::StringName &p_verb,
        const godot::StringName &p_who,
        const godot::Variant &p_value
    ) {
        steps.push_back(Step{p_tick, p_verb, p_who, p_value});
        return *this;
    }
};

} // namespace netw_test
