#pragma once

#include "entity_decl.h"

#include "godot/templates.hpp"
#include "netw/predict/joint.hpp"

namespace netw_test {

class WorldDecl {
public:
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

    WorldDecl &hosted(const EntityDecl &p_decl) {
        rows.push_back(Row{p_decl, {}, -1, true});
        return *this;
    }

    enum Reconcile {
        INDEPENDENT = 0,
        JOINT = 1,
    };

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
