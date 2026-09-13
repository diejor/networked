#include "netw/sync_authoring.hpp"

#include "godot/node.hpp"
#include "godot/object.hpp"
#include "godot/variant.hpp"
#include "netw/api/context.hpp"
#include "netw/api/display_handle.hpp"
#include "netw/api/entity.hpp"
#include "netw/api/interpolate.hpp"
#include "netw/api/member_config.hpp"
#include "netw/api/netw_multiplayer.hpp"
#include "netw/api/predict.hpp"
#include "netw/api/prediction_handle.hpp"
#include "netw/api/property_config.hpp"
#include "netw/display/decl.hpp"
#include "netw/synchronizers.hpp"

using namespace godot;

namespace netw {
namespace authoring {

namespace {

const char *INTERPOLATE_KEY = "netw_interpolate";

const char *KEY_VISUAL_ROOT = "netw_visual_root";
const char *KEY_DISPLAY_ROLE = "netw_display_role";
const char *KEY_TIMELINE_MODE = "netw_timeline_mode";
const char *KEY_PREDICTED_MODE = "netw_predicted_mode";
const char *KEY_PREDICTED_SMOOTH_TIME = "netw_predicted_smooth_time";
const char *KEY_SMART_DILATION = "netw_enable_smart_dilation";
const char *KEY_MAX_EXTRA_DILATION = "netw_max_extra_dilation";
const char *KEY_LAG_ADAPT_RATE = "netw_lag_adapt_rate";
const char *KEY_STARVATION_GROWTH = "netw_starvation_growth";
const char *KEY_FLOOR_SMOOTHING = "netw_floor_smoothing";
const char *KEY_STARVATION_GRACE = "netw_starvation_grace_frames";
const char *KEY_TRACE_INTERVAL = "netw_trace_interval";

const char *KEY_ARCHETYPE = "netw_archetype";
const char *KEY_SCHEDULE = "netw_schedule";
const char *KEY_CORRECTION_MODE = "netw_correction_mode";
const char *KEY_SNAP_RESTORE = "netw_snap_restore";
const char *KEY_MISSING_POLICY = "netw_missing_policy";
const char *KEY_MAX_RESTORE_TICKS = "netw_max_restore_ticks";
const char *KEY_DIVERGENCE_EPSILON = "netw_divergence_epsilon";
const char *KEY_TELEPORT_THRESHOLD = "netw_teleport_threshold";
const char *KEY_COLLISION_COOLDOWN = "netw_collision_cooldown_ticks";
const char *KEY_MAX_CONSUME_PER_TICK = "netw_max_consume_per_tick";
const char *KEY_MAX_CONSUME_LAG_TICKS = "netw_max_consume_lag_ticks";
const char *KEY_CONSUME_BUFFER_TICKS = "netw_consume_buffer_ticks";
const char *KEY_REPLAY_BUFFER_DEPTH = "netw_replay_buffer_depth";

const char *KEY_INITIAL_CONTROLLER = "netw_initial_controller";
const char *KEY_TRANSFER = "netw_control_transfer";
const char *KEY_ON_DISCONNECT = "netw_on_controller_disconnect";

const char *KEY_DESPAWN_HOOK = "netw_despawn_hook";
const char *KEY_DESPAWN_LINGER = "netw_despawn_linger";

const char *KEY_DECLARES_SCENE = "netw_declares_scene";
const char *KEY_SCENE_LABEL = "netw_scene_label";
const char *KEY_SCENE_ISOLATION = "netw_scene_isolation";

bool declared(Object *p_sync, const char *p_key) {
    return p_sync->has_meta(StringName(p_key));
}

int64_t whole(Object *p_sync, const char *p_key) {
    return int64_t(p_sync->get_meta(StringName(p_key)));
}

double real(Object *p_sync, const char *p_key) {
    return double(p_sync->get_meta(StringName(p_key)));
}

void apply_display(Object *p_sync, const Ref<NetwEntity> &p_entity) {
    const Ref<NetwDisplayHandle> display = p_entity->get_interpolation();
    if (display.is_null()) {
        return;
    }
    if (declared(p_sync, KEY_VISUAL_ROOT)) {
        display->set_visual_root(
            NodePath(p_sync->get_meta(StringName(KEY_VISUAL_ROOT)))
        );
    }
    if (declared(p_sync, KEY_DISPLAY_ROLE)) {
        display->set_display_role(whole(p_sync, KEY_DISPLAY_ROLE));
    }
    if (declared(p_sync, KEY_PREDICTED_MODE)) {
        display->set_predicted_mode(whole(p_sync, KEY_PREDICTED_MODE));
    }
    if (declared(p_sync, KEY_PREDICTED_SMOOTH_TIME)) {
        display->set_predicted_smooth_time(
            real(p_sync, KEY_PREDICTED_SMOOTH_TIME)
        );
    }
    if (declared(p_sync, KEY_SMART_DILATION)) {
        display->set_enable_smart_dilation(
            bool(p_sync->get_meta(StringName(KEY_SMART_DILATION)))
        );
    }
    if (declared(p_sync, KEY_MAX_EXTRA_DILATION)) {
        display->set_max_extra_dilation(real(p_sync, KEY_MAX_EXTRA_DILATION));
    }
    if (declared(p_sync, KEY_LAG_ADAPT_RATE)) {
        display->set_lag_adapt_rate(real(p_sync, KEY_LAG_ADAPT_RATE));
    }
    if (declared(p_sync, KEY_STARVATION_GROWTH)) {
        display->set_starvation_growth(real(p_sync, KEY_STARVATION_GROWTH));
    }
    if (declared(p_sync, KEY_FLOOR_SMOOTHING)) {
        display->set_floor_smoothing(real(p_sync, KEY_FLOOR_SMOOTHING));
    }
    if (declared(p_sync, KEY_STARVATION_GRACE)) {
        display->set_starvation_grace_frames(
            whole(p_sync, KEY_STARVATION_GRACE)
        );
    }
    if (declared(p_sync, KEY_TRACE_INTERVAL)) {
        display->set_trace_interval(whole(p_sync, KEY_TRACE_INTERVAL));
    }
}

void apply_timeline(
    Object *p_sync,
    Node *p_owner,
    const Ref<NetwEntity> &p_entity
) {
    if (!declared(p_sync, KEY_TIMELINE_MODE)) {
        return;
    }
    const Ref<NetwMultiplayer> core = NetwMultiplayer::core_of(p_owner);
    if (core.is_null()) {
        return;
    }
    core->display_set_param(
        p_entity->get_rid_handle(),
        NetwMultiplayer::DISPLAY_PARAM_TIMELINE_MODE,
        int(whole(p_sync, KEY_TIMELINE_MODE))
    );
}

const char *PREDICTION_KEYS[] = {
    KEY_ARCHETYPE,
    KEY_SCHEDULE,
    KEY_CORRECTION_MODE,
    KEY_SNAP_RESTORE,
    KEY_MISSING_POLICY,
    KEY_MAX_RESTORE_TICKS,
    KEY_DIVERGENCE_EPSILON,
    KEY_TELEPORT_THRESHOLD,
    KEY_COLLISION_COOLDOWN,
    KEY_MAX_CONSUME_PER_TICK,
    KEY_MAX_CONSUME_LAG_TICKS,
    KEY_CONSUME_BUFFER_TICKS,
    KEY_REPLAY_BUFFER_DEPTH,
};

bool prediction_declared(Object *p_sync) {
    for (const char *key : PREDICTION_KEYS) {
        if (declared(p_sync, key)) {
            return true;
        }
    }
    return false;
}

void apply_prediction(Object *p_sync, const Ref<NetwEntity> &p_entity) {
    const Ref<NetwPredictionHandle> predict = p_entity->get_prediction();
    if (predict.is_null()) {
        return;
    }
    if (declared(p_sync, KEY_MAX_CONSUME_PER_TICK)) {
        predict->set_max_consume_per_tick(
            int(whole(p_sync, KEY_MAX_CONSUME_PER_TICK))
        );
    }
    if (declared(p_sync, KEY_MAX_CONSUME_LAG_TICKS)) {
        predict->set_max_consume_lag_ticks(
            int(whole(p_sync, KEY_MAX_CONSUME_LAG_TICKS))
        );
    }
    if (declared(p_sync, KEY_CONSUME_BUFFER_TICKS)) {
        predict->set_consume_buffer_ticks(
            int(whole(p_sync, KEY_CONSUME_BUFFER_TICKS))
        );
    }
    if (declared(p_sync, KEY_REPLAY_BUFFER_DEPTH)) {
        predict->set_replay_buffer_depth(
            int(whole(p_sync, KEY_REPLAY_BUFFER_DEPTH))
        );
    }
    if (declared(p_sync, KEY_ARCHETYPE)) {
        predict->set_archetype(
            static_cast<NetwPredict::Archetype>(
                int(whole(p_sync, KEY_ARCHETYPE))
            )
        );
    }
    if (declared(p_sync, KEY_SCHEDULE)) {
        predict->set_schedule(
            static_cast<NetwPredict::Schedule>(int(whole(p_sync, KEY_SCHEDULE)))
        );
    }
    if (declared(p_sync, KEY_CORRECTION_MODE)) {
        predict->set_recovery_policy(-1);
        predict->set_correction_mode(int(whole(p_sync, KEY_CORRECTION_MODE)));
    }
    if (declared(p_sync, KEY_SNAP_RESTORE)) {
        predict->set_snap_restore(int(whole(p_sync, KEY_SNAP_RESTORE)));
    }
    if (declared(p_sync, KEY_MISSING_POLICY)) {
        predict->set_missing_policy(int(whole(p_sync, KEY_MISSING_POLICY)));
    }
    if (declared(p_sync, KEY_MAX_RESTORE_TICKS)) {
        predict->set_max_restore_ticks(
            int(whole(p_sync, KEY_MAX_RESTORE_TICKS))
        );
    }
    if (declared(p_sync, KEY_DIVERGENCE_EPSILON)) {
        predict->set_divergence_epsilon(real(p_sync, KEY_DIVERGENCE_EPSILON));
    }
    if (declared(p_sync, KEY_TELEPORT_THRESHOLD)) {
        predict->set_teleport_threshold(real(p_sync, KEY_TELEPORT_THRESHOLD));
    }
    if (declared(p_sync, KEY_COLLISION_COOLDOWN)) {
        predict->set_collision_cooldown_ticks(
            int(whole(p_sync, KEY_COLLISION_COOLDOWN))
        );
    }
}

void apply_interpolators(Object *p_sync, Node *p_owner) {
    if (!declared(p_sync, INTERPOLATE_KEY)) {
        return;
    }
    const Dictionary table = p_sync->get_meta(StringName(INTERPOLATE_KEY));
    const Array names = table.keys();
    for (int at = 0; at < names.size(); ++at) {
        const StringName property = names[at];
        const int mode = int(table[names[at]]);
        if (mode == NetwInterpolate::MODE_NONE) {
            continue;
        }
        Ref<NetwInterpolate> spec;
        spec.instantiate();
        spec->set_mode(NetwInterpolate::Mode(mode));
        const Ref<NetwPropertyConfig> config
            = Netw::configure_property(p_owner, property, false);
        if (config.is_valid()) {
            config->interpolate(gd::array_of(spec));
        }
    }
}

void apply_control(Object *p_spawner, const Ref<NetwEntity> &p_entity) {
    if (declared(p_spawner, KEY_INITIAL_CONTROLLER)) {
        p_entity->set_initial_controller(
            whole(p_spawner, KEY_INITIAL_CONTROLLER)
        );
    }
    if (declared(p_spawner, KEY_TRANSFER)) {
        p_entity->set_transfer(whole(p_spawner, KEY_TRANSFER));
    }
    if (declared(p_spawner, KEY_ON_DISCONNECT)) {
        p_entity->set_on_controller_disconnect(
            whole(p_spawner, KEY_ON_DISCONNECT)
        );
    }
}

void apply_despawn(Object *p_spawner, Node *p_node) {
    const bool wants_hook = declared(p_spawner, KEY_DESPAWN_HOOK);
    const bool wants_linger = declared(p_spawner, KEY_DESPAWN_LINGER);
    if (!wants_hook && !wants_linger) {
        return;
    }
    if (p_node->get_script().get_type() == Variant::NIL) {
        return;
    }
    const Ref<NetwDespawnConfig> config = Netw::configure_despawn(p_node);
    if (config.is_null()) {
        return;
    }
    if (wants_hook) {
        config->set_hook_method(StringName(
            String(p_spawner->get_meta(StringName(KEY_DESPAWN_HOOK)))
        ));
    }
    if (wants_linger) {
        config->set_linger_seconds(real(p_spawner, KEY_DESPAWN_LINGER));
    }
}

void apply_scene_mark(Object *p_spawner, const Ref<NetwEntity> &p_entity) {
    if (declared(p_spawner, KEY_DECLARES_SCENE)) {
        p_entity->set_declares_scene(
            bool(p_spawner->get_meta(StringName(KEY_DECLARES_SCENE)))
        );
    }
    if (declared(p_spawner, KEY_SCENE_LABEL)) {
        p_entity->set_scene_label(
            StringName(String(p_spawner->get_meta(StringName(KEY_SCENE_LABEL))))
        );
    }
    if (declared(p_spawner, KEY_SCENE_ISOLATION)) {
        p_entity->set_scene_isolation(whole(p_spawner, KEY_SCENE_ISOLATION));
    }
}

} // namespace

void apply(Node *p_root, Object *p_sync) {
    if (p_root == nullptr || p_sync == nullptr) {
        return;
    }
    const Ref<NetwEntity> entity = NetwEntity::of(p_root);
    if (entity.is_null()) {
        return;
    }
    Node *owner = entity->get_owner();
    if (owner == nullptr) {
        return;
    }
    apply_display(p_sync, entity);
    apply_timeline(p_sync, owner, entity);
    apply_prediction(p_sync, entity);
    apply_interpolators(p_sync, owner);
    const Ref<NetwMultiplayer> core = NetwMultiplayer::core_of(owner);
    if (core.is_valid() && prediction_declared(p_sync)) {
        core->predict_declare(core->entity_of(owner));
    }
    if (core.is_valid()) {
        core->display_mark_dirty(
            entity->get_rid_handle(),
            netw::display::DIRT_RUNTIME
        );
    }
}

bool declares_prediction(Node *p_root) {
    if (p_root == nullptr) {
        return false;
    }
    const TypedArray<MultiplayerSynchronizer> syncs
        = synchronizers::of_node(p_root);
    for (int at = 0; at < syncs.size(); ++at) {
        Object *sync = syncs[at];
        if (sync != nullptr && prediction_declared(sync)) {
            return true;
        }
    }
    return false;
}

void apply_spawner(Node *p_node, Object *p_spawner) {
    if (p_node == nullptr || p_spawner == nullptr) {
        return;
    }
    const Ref<NetwEntity> entity = NetwEntity::of(p_node);
    if (entity.is_null()) {
        return;
    }
    apply_control(p_spawner, entity);
    apply_scene_mark(p_spawner, entity);
    apply_despawn(p_spawner, p_node);
}

} // namespace authoring
} // namespace netw
