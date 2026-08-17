#pragma once

/* A declared world plus a tick-keyed schedule of what happens to it.
 *
 * A scenario is data. Nothing in it touches an API, which is what lets one
 * scenario be driven at more than one altitude and lets a law read the run
 * rather than the setup that produced it.
 *
 * Arguments are tick-first and there is no cursor, so a scenario diffs as a
 * table. The label is required rather than conventional: it is half of every
 * cell coordinate and half of every failure's prose, and an unlabeled scenario
 * has no name to fail under.
 *
 * [codeblock]
 * Scenario s;
 * s.label = "perturbed-lane";
 * s.world.entity(EntityDecl().named("P").synced("position"));
 * s.warmup_ticks = 8;
 * s.hold_input(1, "P", Vector2(1, 0));
 * s.perturb(30, "P", Vector2(60, -40));
 * s.until(100);
 * [/codeblock]
 */

#include "world_decl.h"

#include "godot/templates.hpp"
#include "godot/variant.hpp"
#include "netw/transport/loopback.hpp"

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
    // The tolerance the lanes of this scenario are judged at. A scenario that
    // leaves it zero is asking for bit-equality, which for a closed-form body
    // is answerable and for a perturbed one is not.
    double epsilon = 0.0;
    godot::Ref<netw::LocalLinkConditions> link_conditions;
    // What reaches the OWNER, left alone by `conditions`. A lane whose
    // acknowledgements never come back is stated here rather than by silencing
    // the link both ways, which would also stop what the owner sends.
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

    // A disturbance authority sees and the predictor does not, which is the
    // only way a lane can diverge without either side being wrong.
    Scenario &perturb(
        int p_tick,
        const godot::StringName &p_who,
        const godot::Variant &p_value
    ) {
        return step(p_tick, "perturb", p_who, p_value);
    }

    // Drops the lane's authoritative history from this tick on, so what the
    // run proves afterwards is what an unrecorded entity can still answer.
    Scenario &undeclare(int p_tick, const godot::StringName &p_who) {
        return step(p_tick, "undeclare", p_who, godot::Variant());
    }

    // The past the run is read at. A scenario that names no sample tick is
    // asking about the present, which its live position already answers.
    Scenario &sample_at(int p_tick, const godot::StringName &p_who) {
        return step(p_tick, "sample_at", p_who, godot::Variant());
    }

    // Rewinds the lane to this tick after the run and reads what its body saw
    // of itself while it was there.
    Scenario &rewind_at(int p_tick, const godot::StringName &p_who) {
        return step(p_tick, "rewind_at", p_who, godot::Variant());
    }

    // Admits the client at [param p_client] to [param p_scene]. The value is
    // the client index rather than a peer id, because ids are drawn at
    // bring-up and a scenario is data written before any run exists.
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

    // Seats [param p_who] into [param p_destination], which is the whole of
    // what membership is: an entity belongs to the scene above it.
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

    // What AUTHORITY steps each frame, when that differs from what the owner
    // steps. A zero is a frame authority never reached at all rather than one
    // it reached and held, because a FRAME-scheduled authority consumes on the
    // frame hook and a frame it ran is a transition it took. A scenario that
    // leaves this empty grants both sides the same clock.
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
