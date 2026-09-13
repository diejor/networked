#include "godot/class_db.hpp"
#include "godot/engine.hpp"
#include "godot/object.hpp"
#include "netw/api/entity.hpp"
#include "netw/api/netw_multiplayer.hpp"
#include "netw/colors.hpp"
#include "netw/display/build.hpp"
#include "netw/display/channel.hpp"
#include "netw/display/history.hpp"
#include "netw/display/pump.hpp"
#include "netw/display/roles.hpp"
#include "netw/display/timing.hpp"
#include "netw/log.hpp"
#include "netw/profile.hpp"
#include "netw/subsystems.hpp"

using namespace godot;

namespace netw {

display::Decl NetwMultiplayer::display_config_for(
    const Ref<NetwEntity> &p_entity
) {
    if (p_entity.is_null()) {
        return display::Decl();
    }
    const RID rid = p_entity->get_rid_handle();
    const display::Decl *standing = display_book->decl_ptr(rid);
    if (standing != nullptr) {
        return *standing;
    }
    display::Decl config;
    const Ref<NetwDisplayHandle> handle = p_entity->get_interpolation();
    if (handle.is_valid()) {
        const display::Decl *authored = handle->declaration();
        if (authored != nullptr) {
            config = *authored;
        }
    }
    if (rid.is_valid()) {
        display_book->set_decl(rid, config);
    }
    return config;
}

int64_t NetwMultiplayer::display_route_of(const Ref<NetwEntity> &p_entity) {
    if (p_entity.is_null()) {
        return 0;
    }
    const int64_t route = liveness_route_of(p_entity.ptr());
    return route > 0 ? route : p_entity->get_route();
}

display::Runtime *NetwMultiplayer::display_runtime_for(
    int64_t p_route,
    const Ref<NetwEntity> &p_entity
) {
    if (p_route <= 0 || p_entity.is_null()) {
        return nullptr;
    }
    display::Runtime *standing = display_book->runtime_at(p_route);
    if (standing != nullptr) {
        return standing;
    }
    display_book->enroll(p_entity->get_rid_handle(), p_route);
    display::Runtime *runtime
        = display_book->open_runtime(p_entity->get_rid_handle());
    runtime->set_route(p_route);
    runtime->bind(p_entity.ptr(), p_entity->get_owner());
    runtime->set_config(display_config_for(p_entity));
    return runtime;
}

void NetwMultiplayer::display_on_entity_live(
    int64_t p_route,
    Object *p_entity
) {
    const Ref<NetwEntity> entity
        = Ref<NetwEntity>(Object::cast_to<NetwEntity>(p_entity));
    if (entity.is_null()) {
        return;
    }
    const display::Decl config = display_config_for(entity);
    const bool declared_role = config.display_role != netw::display::ROLE_AUTO;
    if (!declared_role && !display_wants_runtime(entity->get_owner())) {
        return;
    }
    display::Runtime *runtime = display_runtime_for(p_route, entity);
    if (runtime == nullptr) {
        return;
    }
    if (Array(runtime->get_entity_hooks()).is_empty()) {
        Array hooks;
        const Callable on_control
            = callable_mp(this, &NetwMultiplayer::display_on_control_changed)
                  .bind(p_route);
        entity->connect("control_changed", on_control);
        hooks.append(on_control);
        runtime->set_entity_hooks(hooks);
    }
    display_rebuild_runtime(runtime);
    display_resolve_role(runtime);
    runtime->reset(
        clock_engine().get_display_offset(),
        clock_engine().recommended_display_offset()
    );
}

void NetwMultiplayer::display_release_hooks(display::Runtime *p_runtime) {
    const Ref<NetwEntity> entity = p_runtime->entity();
    const Array hooks = p_runtime->get_entity_hooks();
    if (entity.is_valid()) {
        for (int at = 0; at < hooks.size(); ++at) {
            const Callable hook = hooks[at];
            if (entity->is_connected("control_changed", hook)) {
                entity->disconnect("control_changed", hook);
            }
        }
    }
    p_runtime->set_entity_hooks(Array());
    display_hooks.chase_hook(p_runtime->entity_rid(), false);
}

void NetwMultiplayer::display_release_route(int64_t p_route) {
    display::Runtime *runtime = display_book->runtime_at(p_route);
    if (runtime != nullptr) {
        display_release_hooks(runtime);
        display::apply_body_freeze(runtime, netw::display::ROLE_DISABLED);
    }
    display_book->drop_route(p_route);
}

void NetwMultiplayer::display_clear_runtimes() {
    const LocalVector<display::Runtime *> runtimes = display_book->runtimes();
    for (int at = 0; at < runtimes.size(); ++at) {
        display::Runtime *runtime = runtimes[at];
        display_release_hooks(runtime);
        display::apply_body_freeze(runtime, netw::display::ROLE_DISABLED);
    }
    display_book->clear();
}

void NetwMultiplayer::display_on_control_changed(
    int64_t p_previous,
    int64_t p_peer,
    int64_t p_route
) {
    display::Runtime *runtime = display_book->runtime_at(p_route);
    if (runtime != nullptr) {
        display_resolve_role(runtime);
    }
}

void NetwMultiplayer::display_refresh_moved(int64_t p_route) {
    display::Runtime *runtime = display_book->runtime_at(p_route);
    if (runtime == nullptr) {
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

void NetwMultiplayer::display_mark_role_dirty(const RID &p_entity) {
    display::Runtime *runtime = display_book->runtime_of(p_entity);
    if (runtime == nullptr) {
        display_book->mark_dirty(p_entity, netw::display::DIRT_RUNTIME);
        return;
    }
    display_resolve_role(runtime);
}

void NetwMultiplayer::display_drain_dirty() {
    const TypedArray<RID> stale = display_book->take_dirty();
    for (int at = 0; at < stale.size(); ++at) {
        display::Runtime *runtime = display_book->runtime_of(stale[at]);
        if (runtime != nullptr) {
            display_rebuild_runtime(runtime);
            display_resolve_role(runtime);
            runtime->reset(
                clock_engine().get_display_offset(),
                clock_engine().recommended_display_offset()
            );
        }
    }
}

void NetwMultiplayer::display_on_book_dirty(
    const RID &p_entity,
    netw::display::Dirt p_dirt
) {
    display::Runtime *runtime = display_book->runtime_of(p_entity);
    if (runtime == nullptr) {
        display_book->clear_dirty(p_entity);
        const Ref<NetwEntity> wrapper = Ref<NetwEntity>(
            Object::cast_to<NetwEntity>(wrapper_for_id(p_entity.get_id()).ptr())
        );
        if (wrapper.is_valid()) {
            display_on_entity_live(display_route_of(wrapper), wrapper.ptr());
        }
        return;
    }
    if (p_dirt == netw::display::DIRT_ROLE) {
        display_resolve_role(runtime);
        return;
    }
    session_defer(
        callable_mp(this, &NetwMultiplayer::display_drain_dirty),
        StringName("display-rebuild")
    );
}

void NetwMultiplayer::display_record(
    Node *p_node,
    const StringName &p_target_property,
    const Variant &p_value,
    int64_t p_tick,
    const Ref<NetwInterpolate> &p_spec,
    bool p_authoring_tick
) {
    if (p_node == nullptr || p_spec.is_null() || p_target_property.is_empty()) {
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
    display::Runtime *runtime = display_runtime_for(route, entity);
    if (runtime == nullptr) {
        return;
    }
    display_resolve_role(runtime);
    if (runtime->get_pump_mode() == netw::display::PUMP_BRACKETED) {
        return;
    }

    const int64_t at = runtime->display_tracks().by_key(
        display::state_key_for(p_node, p_target_property)
    );
    display::Channel *state = at >= 0 ? runtime->channels()[at] : nullptr;
    if (state == nullptr) {
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
    if (state == nullptr) {
        return;
    }
    if (p_authoring_tick) {
        state->set_authoring_ticks(true);
    }
    state->display_history().record(p_tick, p_value, p_authoring_tick);

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

void NetwMultiplayer::display_on_clock_tick(double p_delta, int64_t p_tick) {
    NETW_ZONE_NC("Display record tick", colors::INTERP);
    const LocalVector<display::Runtime *> runtimes = display_book->runtimes();
    for (int at = 0; at < runtimes.size(); ++at) {
        display::Runtime *runtime = runtimes[at];
        if (runtime->get_disabled()
            || runtime->get_pump_mode() != netw::display::PUMP_BRACKETED) {
            continue;
        }
        for (display::Channel *state : runtime->channels()) {
            const Variant source = state->get_source_obj();
            Object *from = source;
            if (from == nullptr) {
                continue;
            }
            state->display_history()
                .record(p_tick, from->get(state->get_source_prop()), false);
        }
    }
}

Error NetwMultiplayer::display_pump(double p_delta) {
    NETW_ZONE_NC("Display pump frame", colors::INTERP);
    const int64_t frame
        = int64_t(Engine::get_singleton()->get_process_frames());
    if (p_delta > 0.0 && frame == display_last_frame) {
        return OK;
    }
    display_last_frame = frame;
    if (!clock_engine().get_configured()) {
        return OK;
    }

    const display::Timing timing
        = display::capture_timing(&clock_engine(), p_delta);
    display::PumpStats &stats = display_book->get_stats();
    stats.reset();

    const LocalVector<display::Runtime *> runtimes = display_book->runtimes();
    for (int at = 0; at < runtimes.size(); ++at) {
        display::Runtime *runtime = runtimes[at];
        if (runtime->get_disabled()) {
            if (runtime->get_disable_until_tick() >= 0
                && timing.display_tick >= runtime->get_disable_until_tick()) {
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
            detail["tick"] = timing.display_tick;
            fact.detail = detail;
            plane.emit(fact);
        }
    }
    return OK;
}

void NetwMultiplayer::display_bind_session() {
    if (display_session_bound) {
        return;
    }
    display_session_bound = true;
    connect(
        "entity_live",
        callable_mp(this, &NetwMultiplayer::display_on_entity_live)
    );
    display_book->set_went_dirty(
        callable_mp(this, &NetwMultiplayer::display_on_book_dirty)
    );
    connect(
        "clock_after_tick",
        callable_mp(this, &NetwMultiplayer::display_on_clock_tick)
    );
}

void NetwMultiplayer::display_mark_dirty(
    const RID &p_entity,
    netw::display::Dirt p_dirt
) {
    display_book->mark_dirty(p_entity, p_dirt);
}

} // namespace netw
