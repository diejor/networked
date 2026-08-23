#include "godot/class_db.hpp"
#include "godot/engine.hpp"
#include "godot/object.hpp"
#include "netw/display_build.hpp"
#include "netw/display_channel.hpp"
#include "netw/display_history.hpp"
#include "netw/display_pump.hpp"
#include "netw/display_roles.hpp"
#include "netw/display_timing.hpp"
#include "netw/api/entity.hpp"
#include "netw/log.hpp"
#include "netw/api/netw_multiplayer.hpp"
#include "netw/subsystems.hpp"

using namespace godot;

namespace netw {

Ref<NetwDisplayDecl> NetwMultiplayerCore::display_config_for(
    const Ref<NetwEntity> &p_entity
) {
    if (p_entity.is_null()) {
        return Ref<NetwDisplayDecl>();
    }
    const RID rid = p_entity->get_rid_handle();
    Ref<NetwDisplayDecl> config = display_book->decl_of(rid);
    if (config.is_valid()) {
        return config;
    }
    Object *handle = p_entity->get_interpolation();
    if (handle != nullptr) {
        config = handle->get("_decl");
    }
    if (config.is_null()) {
        config.instantiate();
    }
    if (rid.is_valid()) {
        display_book->set_decl(rid, config);
    }
    return config;
}

int64_t NetwMultiplayerCore::display_route_of(const Ref<NetwEntity> &p_entity) {
    if (p_entity.is_null()) {
        return 0;
    }
    const int64_t route = liveness_route_of(p_entity.ptr());
    return route > 0 ? route : p_entity->get_route();
}

Ref<NetwDisplayRuntime> NetwMultiplayerCore::display_runtime_for(
    int64_t p_route,
    const Ref<NetwEntity> &p_entity
) {
    if (p_route <= 0 || p_entity.is_null()) {
        return Ref<NetwDisplayRuntime>();
    }
    Ref<NetwDisplayRuntime> runtime = display_book->runtime_at(p_route);
    if (runtime.is_valid()) {
        return runtime;
    }
    runtime.instantiate();
    runtime->set_route(p_route);
    runtime->bind(p_entity.ptr(), p_entity->get_owner());
    runtime->set_config(display_config_for(p_entity));
    display_book->enroll(p_entity->get_rid_handle(), p_route);
    display_book->set_runtime(p_entity->get_rid_handle(), runtime);
    return runtime;
}

void NetwMultiplayerCore::display_on_entity_live(
    int64_t p_route,
    Object *p_entity
) {
    const Ref<NetwEntity> entity
        = Ref<NetwEntity>(Object::cast_to<NetwEntity>(p_entity));
    if (entity.is_null()) {
        return;
    }
    const Ref<NetwDisplayDecl> config = display_config_for(entity);
    const bool declared_role = config.is_valid()
        && config->get_display_role() != NetwDisplayDecl::ROLE_AUTO;
    if (!declared_role && !display_wants_runtime(entity->get_owner())) {
        return;
    }
    const Ref<NetwDisplayRuntime> runtime
        = display_runtime_for(p_route, entity);
    if (runtime.is_null()) {
        return;
    }
    if (Array(runtime->get_entity_hooks()).is_empty()) {
        Array hooks;
        const Callable on_control
            = Callable(this, "display_on_control_changed").bind(p_route);
        const Callable on_reparent
            = Callable(this, "display_on_reparented").bind(p_route);
        entity->connect("control_changed", on_control);
        entity->connect("reparented", on_reparent);
        hooks.append(on_control);
        hooks.append(on_reparent);
        runtime->set_entity_hooks(hooks);
    }
    display_rebuild_runtime(runtime);
    display_resolve_role(runtime);
    runtime->reset(
        clock_engine().get_display_offset(),
        clock_engine().recommended_display_offset()
    );
}

void NetwMultiplayerCore::display_release_hooks(
    const Ref<NetwDisplayRuntime> &p_runtime
) {
    const Ref<NetwEntity> entity = p_runtime->entity();
    const Array hooks = p_runtime->get_entity_hooks();
    if (entity.is_valid()) {
        for (int at = 0; at < hooks.size(); ++at) {
            const Callable hook = hooks[at];
            if (entity->is_connected("control_changed", hook)) {
                entity->disconnect("control_changed", hook);
            }
            if (entity->is_connected("reparented", hook)) {
                entity->disconnect("reparented", hook);
            }
        }
    }
    p_runtime->set_entity_hooks(Array());
    display_hooks.chase_hook(p_runtime, false);
}

void NetwMultiplayerCore::display_on_entity_dead(int64_t p_route) {
    const Ref<NetwDisplayRuntime> runtime = display_book->runtime_at(p_route);
    if (runtime.is_valid()) {
        display_release_hooks(runtime);
        display::apply_body_freeze(runtime, NetwDisplayDecl::ROLE_DISABLED);
    }
    display_book->drop_route(p_route);
}

void NetwMultiplayerCore::display_clear_runtimes() {
    const TypedArray<NetwDisplayRuntime> runtimes = display_book->runtimes();
    for (int at = 0; at < runtimes.size(); ++at) {
        const Ref<NetwDisplayRuntime> runtime = runtimes[at];
        display_release_hooks(runtime);
        display::apply_body_freeze(runtime, NetwDisplayDecl::ROLE_DISABLED);
    }
    display_book->clear();
}

void NetwMultiplayerCore::display_on_control_changed(
    int64_t p_previous,
    int64_t p_peer,
    int64_t p_route
) {
    const Ref<NetwDisplayRuntime> runtime = display_book->runtime_at(p_route);
    if (runtime.is_valid()) {
        display_resolve_role(runtime);
    }
}

void NetwMultiplayerCore::display_on_reparented(
    const Variant &p_opts,
    int64_t p_route
) {
    const Ref<NetwDisplayRuntime> runtime = display_book->runtime_at(p_route);
    if (runtime.is_null()) {
        return;
    }
    const Ref<NetwEntity> entity = runtime->entity();
    if (entity.is_null()) {
        return;
    }
    runtime->bind(entity.ptr(), entity->get_owner());
    display_rebuild_runtime(runtime);
    display_resolve_role(runtime);
    runtime->reset(
        clock_engine().get_display_offset(),
        clock_engine().recommended_display_offset()
    );
}

void NetwMultiplayerCore::display_mark_role_dirty(const RID &p_entity) {
    const Ref<NetwDisplayRuntime> runtime = display_book->runtime_of(p_entity);
    if (runtime.is_null()) {
        display_book->mark_dirty(p_entity, NetwDisplayDecl::DIRT_RUNTIME);
        return;
    }
    display_resolve_role(runtime);
}

void NetwMultiplayerCore::display_drain_dirty() {
    const TypedArray<RID> stale = display_book->take_dirty();
    for (int at = 0; at < stale.size(); ++at) {
        const Ref<NetwDisplayRuntime> runtime
            = display_book->runtime_of(stale[at]);
        if (runtime.is_valid()) {
            display_rebuild_runtime(runtime);
            display_resolve_role(runtime);
            runtime->reset(
                clock_engine().get_display_offset(),
                clock_engine().recommended_display_offset()
            );
        }
    }
}

void NetwMultiplayerCore::display_on_book_dirty(
    const RID &p_entity,
    int p_dirt
) {
    const Ref<NetwDisplayRuntime> runtime = display_book->runtime_of(p_entity);
    if (runtime.is_null()) {
        display_book->clear_dirty(p_entity);
        const Ref<NetwEntity> wrapper
            = Ref<NetwEntity>(Object::cast_to<NetwEntity>(
                wrapper_for_id(p_entity.get_id()).ptr()
            ));
        if (wrapper.is_valid()) {
            display_on_entity_live(display_route_of(wrapper), wrapper.ptr());
        }
        return;
    }
    if (p_dirt == NetwDisplayDecl::DIRT_ROLE) {
        display_resolve_role(runtime);
        return;
    }
    settle_schedule(
        Callable(this, "display_drain_dirty"),
        StringName("display-rebuild")
    );
}

void NetwMultiplayerCore::display_record(
    Node *p_node,
    const StringName &p_target_property,
    const Variant &p_value,
    int64_t p_tick,
    const Ref<NetwInterpolate> &p_spec,
    bool p_authoring_tick
) {
    if (p_node == nullptr || p_spec.is_null()
        || p_target_property.is_empty()) {
        return;
    }
    const Ref<NetwEntity> entity = NetwEntity::of(p_node);
    if (entity.is_null()) {
        return;
    }
    const int64_t route = display_route_of(entity);
    if (route <= 0) {
        return;
    }
    const Ref<NetwDisplayRuntime> runtime = display_runtime_for(route, entity);
    if (runtime.is_null()) {
        return;
    }
    display_resolve_role(runtime);
    if (runtime->get_pump_mode() == NetwDisplayDecl::PUMP_BRACKETED) {
        return;
    }

    const Ref<NetwDisplayTracks> tracks = runtime->get_tracks();
    const int64_t at = tracks->by_key(
        display::state_key_for(p_node, p_target_property)
    );
    const TypedArray<NetwDisplayChannel> states = runtime->get_states();
    Ref<NetwDisplayChannel> state = at >= 0
        ? Ref<NetwDisplayChannel>(states[at])
        : Ref<NetwDisplayChannel>();
    if (state.is_null()) {
        const bool is_property = gd::has_property(p_node, p_target_property);
        StringName target = p_target_property;
        if (is_property && !p_spec->get_target().is_empty()) {
            target = p_spec->get_target();
        }
        state = display_ensure_state(
            runtime,
            p_node,
            is_property ? p_target_property : StringName(),
            target,
            p_spec,
            p_authoring_tick
        );
    }
    if (state.is_null()) {
        return;
    }
    if (p_authoring_tick) {
        state->set_authoring_ticks(true);
    }
    state->get_history()->record(p_tick, p_value, p_authoring_tick);

    if (plane.wants(EventPlane::DISPLAY_RECORD, route)) {
        EventPlane::Emission fact(
            EventPlane::DISPLAY_RECORD,
            EventPlane::AFTER,
            clock_engine().get_tick()
        );
        fact.route = route;
        Dictionary detail;
        detail["track"] = p_target_property;
        detail["tick"] = p_tick;
        fact.detail = detail;
        plane.emit(fact);
    }
}

void NetwMultiplayerCore::display_on_clock_tick(double p_delta, int64_t p_tick) {
    const TypedArray<NetwDisplayRuntime> runtimes = display_book->runtimes();
    for (int at = 0; at < runtimes.size(); ++at) {
        const Ref<NetwDisplayRuntime> runtime = runtimes[at];
        if (runtime->get_disabled()
            || runtime->get_pump_mode() != NetwDisplayDecl::PUMP_BRACKETED) {
            continue;
        }
        const TypedArray<NetwDisplayChannel> states = runtime->get_states();
        for (int on = 0; on < states.size(); ++on) {
            const Ref<NetwDisplayChannel> state = states[on];
            const Variant source = state->get_source_obj();
            Object *from = source;
            if (from == nullptr) {
                continue;
            }
            state->get_history()->record(
                p_tick,
                from->get(state->get_source_prop()),
                false
            );
        }
    }
}

Error NetwMultiplayerCore::display_pump(double p_delta) {
    const int64_t frame = int64_t(Engine::get_singleton()->get_process_frames());
    if (p_delta > 0.0 && frame == display_last_frame) {
        return OK;
    }
    display_last_frame = frame;
    if (!clock_engine().get_configured()) {
        return OK;
    }

    const Ref<NetwDisplayTiming> timing
        = NetwDisplayTiming::capture(clock_handle, p_delta);
    const Ref<NetwPumpStats> stats = display_book->get_stats();
    stats->reset();

    const TypedArray<NetwDisplayRuntime> runtimes = display_book->runtimes();
    for (int at = 0; at < runtimes.size(); ++at) {
        const Ref<NetwDisplayRuntime> runtime = runtimes[at];
        if (runtime->get_disabled()) {
            if (runtime->get_disable_until_tick() >= 0
                && timing->get_display_tick()
                    >= runtime->get_disable_until_tick()) {
                runtime->set_disabled(false);
                runtime->set_disable_until_tick(-1);
            } else {
                continue;
            }
        }
        const Ref<NetwEntity> entity = runtime->entity();
        if (entity.is_null()) {
            continue;
        }
        display_pump_runtime(runtime, timing, stats);
        const int64_t route = runtime->get_route();
        if (plane.wants(EventPlane::DISPLAY_PUMP, route)) {
            EventPlane::Emission fact(
                EventPlane::DISPLAY_PUMP,
                EventPlane::AFTER,
                clock_engine().get_tick()
            );
            fact.route = route;
            Dictionary detail;
            detail["tick"] = timing->get_display_tick();
            fact.detail = detail;
            plane.emit(fact);
        }
    }
    return OK;
}

void NetwMultiplayerCore::display_bind_session() {
    if (display_session_bound) {
        return;
    }
    display_session_bound = true;
    connect("entity_live", Callable(this, "display_on_entity_live"));
    connect("entity_dead", Callable(this, "display_on_entity_dead"));
    display_book->connect(
        "went_dirty",
        Callable(this, "display_on_book_dirty")
    );
    connect("after_tick", Callable(this, "display_on_clock_tick"));
}

} // namespace netw
