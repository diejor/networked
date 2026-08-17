#pragma once

#include <cstdint>

#include "godot/callable.hpp"
#include "godot/object.hpp"
#include "godot/ref_counted.hpp"
#include "godot/rid.hpp"
#include "godot/variant.hpp"
#include "netw/entity_control.hpp"
#include "netw/entity_options.hpp"
#include "netw/entity_stage.hpp"

namespace netw {

class NetwEntityRecord : public godot::RefCounted {
    GDCLASS(NetwEntityRecord, godot::RefCounted)

public:
    enum Part {
        PART_SCENE = 0,
        PART_INTEREST = 1,
        PART_PREDICTION = 2,
        PART_DISPLAY = 3,
        PART_COMPONENTS = 4,
        PART_MAX = 5,
    };

private:
    godot::RID handle;
    godot::StringName entity_id;
    int64_t peer_id = 0;
    int64_t route = 0;
    bool declares_scene = false;
    int64_t stage = int(EntityStage::UNBOUND);
    godot::Ref<NetwEntityControl> control;
    godot::Ref<godot::RefCounted> parts[PART_MAX];
    godot::Ref<NetwReparentOpts> reparenting;
    godot::Ref<NetwDespawnOpts> despawning_opts;
    godot::StringName scene_label;
    int64_t scene_isolation = -1;

protected:
    static void _bind_methods();

public:
    NetwEntityRecord();
    ~NetwEntityRecord();

    godot::RID get_handle() const { return handle; }

    bool adopt_handle(const godot::RID &p_handle);

    godot::StringName get_entity_id() const { return entity_id; }
    void set_entity_id(const godot::StringName &p_entity_id) {
        entity_id = p_entity_id;
    }

    int64_t get_peer_id() const { return peer_id; }
    void set_peer_id(int64_t p_peer_id) { peer_id = p_peer_id; }

    int64_t get_route() const { return route; }
    void set_route(int64_t p_route) { route = p_route; }

    godot::StringName scene_label_of(godot::Object *p_owner);
    int64_t scene_isolation_of(godot::Object *p_owner);
    godot::StringName get_scene_label() const { return scene_label; }
    void set_scene_label(const godot::StringName &p_label) {
        scene_label = p_label;
    }
    int64_t get_scene_isolation() const { return scene_isolation; }
    void set_scene_isolation(int64_t p_isolation) {
        scene_isolation = p_isolation;
    }

    static void set_scene_declaration_reader(const godot::Callable &p_reader);
    static godot::Callable scene_declaration_reader();

    bool get_declares_scene() const { return declares_scene; }
    void set_declares_scene(bool p_declares) { declares_scene = p_declares; }

    int64_t get_stage() const { return stage; }

    bool advance(int64_t p_stage);

    godot::Ref<NetwEntityControl> get_control() const { return control; }

    bool transition(int64_t p_stage, godot::Object *p_owner);

    void deactivate(godot::Object *p_owner);

    bool mark_template(godot::Object *p_owner);

    void apply_control(
        godot::Object *p_wrapper,
        godot::Object *p_owner,
        bool p_is_authority
    );

    static bool control_recurses(
        bool p_is_authority,
        bool p_inside_tree,
        bool p_node_ready
    );

    void hydrate_identity(godot::Object *p_owner);

    bool set_controller(godot::Object *p_wrapper, int64_t p_peer);

    int64_t admit_control_request(
        godot::Object *p_wrapper,
        int64_t p_requester
    );

    bool begin_despawn(
        godot::Object *p_wrapper,
        godot::Object *p_owner,
        const godot::Ref<NetwDespawnOpts> &p_opts
    );

    godot::Ref<NetwDespawnOpts> get_active_despawn_opts() const {
        return despawning_opts;
    }

    bool finish_teardown(godot::Object *p_wrapper);

    bool classify_activation(godot::Object *p_owner);

    static bool declares_template(godot::Object *p_owner);
    static godot::StringName template_meta();

    godot::Ref<NetwReparentOpts> get_reparenting() const { return reparenting; }
    void set_reparenting(const godot::Ref<NetwReparentOpts> &p_opts) {
        reparenting = p_opts;
    }

    godot::Ref<godot::RefCounted> part(
        int64_t p_part,
        godot::Object *p_wrapper
    );

    static void set_part_factory(
        int64_t p_part,
        const godot::Callable &p_factory
    );
    static bool has_part_factory(int64_t p_part);
    static godot::Callable part_factory(int64_t p_part);
    static void clear_part_factories();
};

} // namespace netw

VARIANT_ENUM_CAST(netw::NetwEntityRecord::Part);
