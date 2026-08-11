#pragma once

/* The scenes and entities a case is about, declared together as one value.
 *
 * A world declared entity-at-a-time reads as setup; declared as one record it
 * reads as the thing the law is about, and the scene an entity belongs to is
 * stated beside the entity rather than threaded through a call order. Applying
 * it is the only step that touches a session, so the same world can be declared
 * once and stood up on more than one peer.
 *
 * [codeblock]
 * WorldDecl world;
 * world.scene("Arena")
 *      .entity(EntityDecl().named("Alice").on_route(31), "Arena")
 *      .entity(EntityDecl().named("Row").wrapperless());
 * rig.declare_world(world);
 * rig.entity("Alice");
 * [/codeblock]
 */

#include "entity_decl.h"

#include "godot/templates.hpp"
#include "netw/predict/joint.hpp"

namespace netw_test {

class WorldDecl {
public:
    // A scene is named by the case and archetyped by its stem, and the two
    // differ only when a world declares two instances of one level: stems are
    // not unique, so a second instance needs its own name to be asked for.
    struct SceneRow {
        godot::StringName name;
        godot::StringName stem;
    };

private:
    friend class LoopbackRig;

    struct Row {
        EntityDecl decl;
        godot::StringName scene;
        int player_client = -1;
        bool hosted = false;
    };

    // An island is a relation between declared entities rather than a property
    // of one, so it is named here and not on EntityDecl.
    struct IslandRow {
        godot::StringName owner;
        godot::Vector<godot::StringName> members;
        godot::Vector<godot::StringName> simulated;
        int reconcile = 0;
        int promotion = 0;
        int promotion_count = 0;
        double promotion_meters = 0.0;
    };

    godot::Vector<SceneRow> scenes;
    godot::Vector<Row> rows;
    godot::Vector<IslandRow> islands;
    bool wants_clock = false;
    bool wants_lagcomp = false;
    int clock_tickrate = 30;
    int clock_display_offset = 3;

public:
    WorldDecl &clocked(int p_tickrate = 30, int p_display_offset = 3) {
        wants_clock = true;
        clock_tickrate = p_tickrate;
        clock_display_offset = p_display_offset;
        return *this;
    }

    WorldDecl &lag_compensated() {
        wants_lagcomp = true;
        return *this;
    }

    WorldDecl &scene(
        const godot::StringName &p_name,
        const godot::StringName &p_stem = godot::StringName()
    ) {
        scenes.push_back(SceneRow{p_name, p_stem.is_empty() ? p_name : p_stem});
        return *this;
    }

    // An entity naming no scene is declared outside every one of them, which is
    // what an entity the session routes but no scene owns actually is.
    WorldDecl &entity(
        const EntityDecl &p_decl,
        const godot::StringName &p_scene = godot::StringName()
    ) {
        rows.push_back(Row{p_decl, p_scene, -1, false});
        return *this;
    }

    WorldDecl &player(const godot::StringName &p_name, int p_client) {
        return player(EntityDecl().named(p_name), p_client);
    }

    WorldDecl &player(const EntityDecl &p_decl, int p_client) {
        rows.push_back(Row{p_decl, {}, p_client, false});
        return *this;
    }

    // An entity the listen-server host both controls and holds authority over.
    // It is one peer's row rather than two, so it has no mirror to declare and
    // resolves HOST_LOCAL where a player row resolves PREDICT and CONSUME.
    WorldDecl &hosted(const EntityDecl &p_decl) {
        rows.push_back(Row{p_decl, {}, -1, true});
        return *this;
    }

    // NetwPredict.Reconcile, which has no native twin because only the shell
    // admits a group.
    enum Reconcile {
        INDEPENDENT = 0,
        JOINT = 1,
    };

    // Seats [param members] in [param owner]'s island and names how the group
    // reconciles. A JOINT group replays its members together from one floor,
    // which is what makes the owner's simulation of them reproducible.
    WorldDecl &island(
        const godot::StringName &p_owner,
        const godot::Vector<godot::StringName> &p_members,
        Reconcile p_reconcile = JOINT
    ) {
        IslandRow row;
        row.owner = p_owner;
        row.members = p_members;
        row.reconcile = int(p_reconcile);
        islands.push_back(row);
        return *this;
    }

    // Draws nearby entities into the last declared island. Promotion is what
    // turns a candidate into a member the group steps.
    WorldDecl &promoting(
        netw::predict::Promotion p_promotion,
        int p_count = 0,
        double p_meters = 0.0
    ) {
        REQUIRE_MESSAGE(!islands.is_empty(), "promotion needs an island");
        IslandRow &row = islands.write[islands.size() - 1];
        row.promotion = int(p_promotion);
        row.promotion_count = p_count;
        row.promotion_meters = p_meters;
        return *this;
    }

    // Names a member the island steps rather than displays. Membership alone
    // seats a participant; fidelity is what puts it in the group the owner
    // replays.
    WorldDecl &simulating(const godot::StringName &p_member) {
        REQUIRE_MESSAGE(!islands.is_empty(), "fidelity needs an island");
        islands.write[islands.size() - 1].simulated.push_back(p_member);
        return *this;
    }

    int island_count() const {
        return islands.size();
    }

    const IslandRow &island_at(int p_index) const {
        return islands[p_index];
    }

    int scene_count() const {
        return scenes.size();
    }

    const SceneRow &scene_at(int p_index) const {
        return scenes[p_index];
    }

    int entity_count() const {
        return rows.size();
    }

    const EntityDecl &entity_at(int p_index) const {
        return rows[p_index].decl;
    }

    int player_client_at(int p_index) const {
        return rows[p_index].player_client;
    }

    bool is_hosted_at(int p_index) const {
        return rows[p_index].hosted;
    }

    bool is_clocked() const {
        return wants_clock;
    }

    bool has_lag_compensation() const {
        return wants_lagcomp;
    }

    int tickrate() const {
        return clock_tickrate;
    }

    int display_offset() const {
        return clock_display_offset;
    }
};

} // namespace netw_test
