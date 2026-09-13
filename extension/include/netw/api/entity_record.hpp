#pragma once

#include <cstdint>

#include "godot/callable.hpp"
#include "godot/node.hpp"
#include "godot/object.hpp"
#include "godot/ref_counted.hpp"
#include "godot/rid.hpp"
#include "godot/variant.hpp"
#include "netw/api/entity_options.hpp"
#include "netw/comp_table.hpp"
#include "netw/display/decl.hpp"
#include "netw/entity/control.hpp"
#include "netw/entity/stage.hpp"
#include "netw/interest/decl.hpp"

namespace netw {

class NetwEntityRecord {
public:
    enum Part {
        PART_SCENE = 0,
        PART_INTEREST = 1,
        PART_PREDICTION = 2,
        PART_DISPLAY = 3,
        PART_MAX = 4,
    };

    struct MoveReport {
        godot::StringName reason;
    };

private:
    MoveReport move_report;
    godot::RID handle;
    godot::StringName entity_id;
    int64_t peer_id = 0;
    int64_t route = 0;
    bool declares_scene = false;
    int64_t stage = int(entity::Stage::UNBOUND);
    entity::Control control;
    godot::Variant parts[PART_MAX];
    godot::Ref<NetwDespawnOpts> despawning_opts;
    interest::Facet interest_facet;
    NetwCompTable comp_table;
    godot::LocalVector<godot::ObjectID> comp_registered;
    display::Decl display_decl;
    godot::StringName scene_label;
    int64_t scene_isolation = -1;

public:
    NetwEntityRecord();
    ~NetwEntityRecord();

    NetwEntityRecord(const NetwEntityRecord &) = delete;
    NetwEntityRecord &operator=(const NetwEntityRecord &) = delete;

    godot::RID get_handle() const {
        return handle;
    }

    bool adopt_handle(const godot::RID &p_handle);

    godot::StringName get_entity_id() const {
        return entity_id;
    }
    void set_entity_id(const godot::StringName &p_entity_id) {
        entity_id = p_entity_id;
    }

    int64_t get_peer_id() const {
        return peer_id;
    }
    void set_peer_id(int64_t p_peer_id) {
        peer_id = p_peer_id;
    }

    int64_t get_route() const {
        return route;
    }
    void set_route(int64_t p_route) {
        route = p_route;
    }

    godot::StringName scene_label_of(godot::Node *p_owner);
    int64_t scene_isolation_of(godot::Node *p_owner);
    godot::StringName get_scene_label() const {
        return scene_label;
    }
    void set_scene_label(const godot::StringName &p_label) {
        scene_label = p_label;
    }
    int64_t get_scene_isolation() const {
        return scene_isolation;
    }
    void set_scene_isolation(int64_t p_isolation) {
        scene_isolation = p_isolation;
    }

    bool get_declares_scene() const {
        return declares_scene;
    }
    void set_declares_scene(bool p_declares) {
        declares_scene = p_declares;
    }

    int64_t get_stage() const {
        return stage;
    }

    bool advance(int64_t p_stage);

    entity::Control *get_control() {
        return &control;
    }
    const entity::Control *get_control() const {
        return &control;
    }

    bool transition(int64_t p_stage, godot::Node *p_owner);

    void deactivate(godot::Node *p_owner);

    bool mark_template(godot::Node *p_owner);

    void apply_control(
        godot::Object *p_wrapper,
        godot::Node *p_owner,
        bool p_is_authority
    );

    static bool control_recurses(
        bool p_is_authority,
        bool p_inside_tree,
        bool p_node_ready
    );

    void hydrate_identity(godot::Node *p_owner);

    bool set_controller(godot::Object *p_wrapper, int64_t p_peer);

    int64_t admit_control_request(
        godot::Object *p_wrapper,
        int64_t p_requester
    );

    bool begin_despawn(
        godot::Object *p_wrapper,
        godot::Node *p_owner,
        const godot::Ref<NetwDespawnOpts> &p_opts
    );

    godot::Ref<NetwDespawnOpts> get_active_despawn_opts() const {
        return despawning_opts;
    }

    bool finish_teardown(godot::Object *p_wrapper);
    bool complete_departure(godot::Object *p_wrapper);

    bool classify_activation(godot::Node *p_owner);

    static bool declares_template(godot::Node *p_owner);
    static godot::StringName template_meta();

    const MoveReport &get_move_report() const {
        return move_report;
    }
    void set_move_report(const MoveReport &p_report) {
        move_report = p_report;
    }

    godot::Variant part(int64_t p_part, godot::Object *p_wrapper);

    interest::Facet &get_interest_facet() {
        return interest_facet;
    }
    const interest::Facet &get_interest_facet() const {
        return interest_facet;
    }

    NetwCompTable &get_comp_table() {
        return comp_table;
    }
    const NetwCompTable &get_comp_table() const {
        return comp_table;
    }

    godot::LocalVector<godot::ObjectID> &get_comp_registered() {
        return comp_registered;
    }
    const godot::LocalVector<godot::ObjectID> &get_comp_registered() const {
        return comp_registered;
    }

    display::Decl &display_declaration() {
        return display_decl;
    }

    static void set_part_factory(
        int64_t p_part,
        const godot::Callable &p_factory
    );
    static bool has_part_factory(int64_t p_part);
    static godot::Callable part_factory(int64_t p_part);
    static void clear_part_factories();
};

} // namespace netw
