#include "godot/class_db.hpp"
#include "godot/display_server.hpp"
#include "godot/engine.hpp"
#include "godot/object.hpp"
#include "godot/os.hpp"
#include "godot/project_settings.hpp"
#include "netw/api/display.hpp"
#include "netw/api/entity.hpp"
#include "netw/api/entity_record.hpp"
#include "netw/api/netw_multiplayer.hpp"
#include "netw/api/sync_pipeline.hpp"
#include "netw/colors.hpp"
#include "netw/display/build.hpp"
#include "netw/display/channel.hpp"
#include "netw/display/history.hpp"
#include "netw/display/pump.hpp"
#include "netw/display/roles.hpp"
#include "netw/display/timing.hpp"
#include "netw/log.hpp"
#include "netw/profile.hpp"
#include "netw/stage.hpp"
#include "netw/store/validate.hpp"
#include "netw/subsystems.hpp"

using namespace godot;

namespace netw {

namespace {

bool display_renders_here() {
    ProjectSettings *settings = ProjectSettings::get_singleton();
    if (settings != nullptr
        && bool(settings->get_setting(
            NetwMultiplayer::SMOOTH_WHEN_HEADLESS_SETTING,
            false
        ))) {
        return true;
    }
    OS *os = OS::get_singleton();
    if (os != nullptr && os->has_feature("dedicated_server")) {
        return false;
    }
    DisplayServer *display = DisplayServer::get_singleton();
    return display != nullptr && display->get_name() != String("headless");
}

} // namespace

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
    const display::Decl *authored = p_entity->get_record() != nullptr
        ? p_entity->get_record()->find_display_declaration()
        : nullptr;
    if (authored != nullptr) {
        config = *authored;
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
    display_hook_control(runtime, p_entity, p_route);
    return runtime;
}

void NetwMultiplayer::display_hook_control(
    display::Runtime *p_runtime,
    const Ref<NetwEntity> &p_entity,
    int64_t p_route
) {
    if (!Array(p_runtime->get_entity_hooks()).is_empty()) {
        return;
    }
    NETW_ZONE_NC("display connect hooks", colors::INTERP);
    Array hooks;
    const Callable on_control
        = callable_mp(this, &NetwMultiplayer::display_on_control_changed)
              .bind(p_route);
    p_entity->connect("control_changed", on_control);
    hooks.append(on_control);
    p_runtime->set_entity_hooks(hooks);
}

void NetwMultiplayer::display_on_entity_live(
    int64_t p_route,
    Object *p_entity
) {
    NETW_ZONE_NC("display on entity live", colors::INTERP);
    const Ref<NetwEntity> entity
        = Ref<NetwEntity>(Object::cast_to<NetwEntity>(p_entity));
    if (entity.is_null()) {
        return;
    }
    bool wanted = false;
    {
        NETW_ZONE_NC("display config and wants", colors::INTERP);
        const display::Decl config = display_config_for(entity);
        wanted = config.display_role != netw::display::ROLE_AUTO
            || display_wants_runtime(entity->get_owner());
    }
    if (!wanted) {
        return;
    }
    display::Runtime *runtime = nullptr;
    {
        NETW_ZONE_NC("display runtime for", colors::INTERP);
        runtime = display_runtime_for(p_route, entity);
    }
    if (runtime == nullptr) {
        return;
    }
    display_hook_control(runtime, entity, p_route);
    {
        NETW_ZONE_NC("display rebuild runtime", colors::INTERP);
        display_rebuild_runtime(runtime);
    }
    {
        NETW_ZONE_NC("display resolve role", colors::INTERP);
        display_resolve_role(runtime);
    }
    {
        NETW_ZONE_NC("display reset runtime", colors::INTERP);
        runtime->reset(
            clock_engine().get_display_offset(),
            clock_engine().recommended_display_offset()
        );
    }
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
        sim_release_body(runtime->entity_rid(), true);
    }
    display_book->drop_route(p_route);
    display_book->drop(liveness_core->rid_from_route(int(p_route)));
}

void NetwMultiplayer::display_retire_route(int64_t p_route) {
    display::Runtime *runtime = display_book->runtime_at(p_route);
    if (runtime != nullptr) {
        display_release_hooks(runtime);
        sim_release_body(runtime->entity_rid(), false);
    }
    display_book->drop_route(p_route);
    display_book->drop(liveness_core->rid_from_route(int(p_route)));
}

void NetwMultiplayer::display_clear_runtimes() {
    const LocalVector<display::Runtime *> runtimes = display_book->runtimes();
    for (int at = 0; at < runtimes.size(); ++at) {
        display::Runtime *runtime = runtimes[at];
        display_release_hooks(runtime);
        sim_release_body(runtime->entity_rid(), true);
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
    bool p_authoring_tick,
    uint64_t p_owner,
    uint32_t p_lane,
    uint32_t p_column
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
    if (runtime->get_pump_mode() == netw::display::PUMP_UNRESOLVED) {
        display_resolve_role(runtime);
    }
    if (runtime->get_pump_mode() == netw::display::PUMP_BRACKETED) {
        return;
    }

    display::Channel *state = p_owner != 0
        ? runtime->channel_following(p_owner, p_lane, p_column)
        : nullptr;
    if (state == nullptr) {
        const int64_t at = runtime->display_tracks().by_key(
            display::state_key_for(p_node, p_target_property)
        );
        state = at >= 0 ? runtime->channels()[at] : nullptr;
    }
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
    display_land(
        runtime,
        state,
        route,
        p_target_property,
        p_value,
        p_tick,
        p_authoring_tick,
        p_owner,
        p_lane,
        p_column
    );
}

bool NetwMultiplayer::display_record_followed(
    int64_t p_route,
    const StringName &p_target_property,
    int64_t p_tick,
    bool p_authoring_tick,
    uint64_t p_owner,
    uint32_t p_lane,
    uint32_t p_column
) {
    display::Runtime *runtime = display_book->runtime_at(p_route);
    if (runtime == nullptr
        || runtime->get_pump_mode() == netw::display::PUMP_UNRESOLVED) {
        return false;
    }
    if (runtime->get_pump_mode() == netw::display::PUMP_BRACKETED) {
        return true;
    }
    display::Channel *state
        = runtime->channel_following(p_owner, p_lane, p_column);
    if (state == nullptr) {
        return false;
    }
    display_land(
        runtime,
        state,
        p_route,
        p_target_property,
        Variant(),
        p_tick,
        p_authoring_tick,
        p_owner,
        p_lane,
        p_column
    );
    return true;
}

void NetwMultiplayer::display_land(
    display::Runtime *p_runtime,
    display::Channel *p_state,
    int64_t p_route,
    const StringName &p_target_property,
    const Variant &p_value,
    int64_t p_tick,
    bool p_authoring_tick,
    uint64_t p_owner,
    uint32_t p_lane,
    uint32_t p_column
) {
    if (p_authoring_tick) {
        p_state->set_authoring_ticks(true);
    }
    display::History &history = p_state->display_history();
    if (p_owner != 0) {
        history.follow(&store_world, p_owner, p_lane, p_column);
    }
    history.record(p_tick, p_value, p_authoring_tick);
    display_book->wake(p_runtime);

    if (plane.wants(EventPlane::DISPLAY_RECORD, p_route)) {
        EventPlane::Emission fact(
            EventPlane::DISPLAY_RECORD,
            EventPlane::AFTER,
            clock_engine().get_tick()
        );
        fact.route = p_route;
        Dictionary detail;
        detail["track"] = p_target_property;
        detail["tick"] = p_tick;
        fact.detail = detail;
        plane.emit(fact);
    }
}

bool NetwMultiplayer::display_table_of(
    Node *p_node,
    const StringName &p_property,
    uint64_t &r_owner,
    uint32_t &r_lane,
    uint32_t &r_column
) {
    const LocalVector<uint64_t> *bound = p_node != nullptr
        ? binding_book.on_node(uint64_t(p_node->get_instance_id()))
        : nullptr;
    if (bound == nullptr) {
        return false;
    }
    for (const uint64_t entry : *bound) {
        repl::PropertyBinding *const binding = binding_book.live(entry);
        if (binding == nullptr || binding->get_set() == nullptr
            || binding->get_set()->get_audience()
                != repl::PropertySet::AUDIENCE_PUBLIC) {
            continue;
        }
        const uint64_t owner = binding->id;
        for (uint32_t lane = 0; lane < 2; ++lane) {
            const store::RowAddress at = store_world.row_of(owner, lane);
            if (!at.valid()) {
                continue;
            }
            const store::Table &table = store_world.table(at.table);
            for (int column = 0; column < table.schema.column_count();
                 ++column) {
                if (table.schema.at(column)->key == p_property) {
                    r_owner = owner;
                    r_lane = lane;
                    r_column = uint32_t(column);
                    return true;
                }
            }
        }
    }
    return false;
}

void NetwMultiplayer::display_follow_binding(
    const repl::PropertyBindingPtr &p_binding
) {
    Node *node = p_binding != nullptr ? p_binding->node() : nullptr;
    if (node == nullptr || p_binding->get_set() == nullptr
        || p_binding->get_set()->get_audience()
            != repl::PropertySet::AUDIENCE_PUBLIC) {
        return;
    }
    const Ref<NetwEntity> entity = NetwEntity::of(node);
    display::Runtime *runtime = entity.is_valid()
        ? display_book->runtime_of(entity->get_rid_handle())
        : nullptr;
    if (runtime == nullptr) {
        return;
    }
    const uint64_t owner = p_binding->id;
    for (uint32_t lane = 0; lane < 2; ++lane) {
        const store::RowAddress at = store_world.row_of(owner, lane);
        if (!at.valid()) {
            continue;
        }
        const store::Table &table = store_world.table(at.table);
        for (display::Channel *state : runtime->channels()) {
            const StringName prop = state->get_source_prop();
            if (prop.is_empty()
                || Object::cast_to<Node>(state->get_source_obj()) != node) {
                continue;
            }
            for (int column = 0; column < table.schema.column_count();
                 ++column) {
                if (table.schema.at(column)->key == prop) {
                    state->display_history()
                        .follow(&store_world, owner, lane, uint32_t(column));
                }
            }
        }
    }
    display_book->wake(runtime);
}

void NetwMultiplayer::display_follow_tables(display::Runtime *p_runtime) {
    if (p_runtime == nullptr) {
        return;
    }
    for (display::Channel *state : p_runtime->channels()) {
        const StringName prop = state->get_source_prop();
        if (prop.is_empty()) {
            continue;
        }
        Node *node = Object::cast_to<Node>(state->get_source_obj());
        uint64_t owner = 0;
        uint32_t lane = 0;
        uint32_t column = 0;
        if (node != nullptr
            && display_table_of(node, prop, owner, lane, column)) {
            state->display_history().follow(&store_world, owner, lane, column);
        }
    }
    display_book->wake(p_runtime);
}

void NetwMultiplayer::display_wake_entity(const RID &p_entity) {
    display_book->wake(display_book->runtime_of(p_entity));
}

void NetwMultiplayer::display_drain_touched() {
    for (uint32_t at = 0; at < store_world.table_count(); ++at) {
        store::Table &table = store_world.table(at);
        if (table.touched_overflow) {
            table.touched_overflow = false;
            display_book->wake_all();
        }
        for (const int64_t route : table.touched_routes) {
            display_book->wake_route(route);
        }
        table.touched_routes.clear();
    }
}

bool NetwMultiplayer::display_record_runtime(
    display::Runtime *p_runtime,
    int64_t p_tick
) {
    if (p_runtime->get_disabled()
        || p_runtime->get_pump_mode() != netw::display::PUMP_BRACKETED) {
        return false;
    }
    bool busy = false;
    for (display::Channel *state : p_runtime->channels()) {
        display::History &history = state->display_history();
        if (history.follows_table()) {
            if (history.table_pulls()) {
                continue;
            }
            history.unfollow();
        }
        const Variant source = state->get_source_obj();
        Object *from = source;
        if (from == nullptr) {
            continue;
        }
        const StringName prop = state->get_source_prop();
        const bool rests = port_rests(from, prop);
        if (rests && history.holds_rested_source()) {
            continue;
        }
#if defined(NETW_TESTS)
        idle_work.sampled += 1;
#endif
        history.record_every_tick(p_tick, port_get(from, prop));
        history.set_source_rested(rests);
        display_book->wake(p_runtime);
        busy = busy || !rests;
    }
    return busy;
}

void NetwMultiplayer::display_on_clock_tick(double p_delta, int64_t p_tick) {
    NETW_STAGE(DISPLAY, "Display record", colors::INTERP);
#if defined(NETW_TESTS)
    store::AllocationTally recording(idle_work.display_allocations);
#endif
    LocalVector<display::Runtime *> &listed = display_book->record_set();
    if (!display_renders_here()) {
        for (display::Runtime *runtime : listed) {
            if (runtime != nullptr) {
                runtime->set_record_listed(false);
            }
        }
        listed.clear();
        return;
    }
    const uint32_t count = listed.size();
    NETW_ZONE_VALUE(count);
    uint32_t kept = 0;
    for (uint32_t at = 0; at < count && at < listed.size(); ++at) {
        display::Runtime *runtime = listed[at];
        if (runtime == nullptr) {
            continue;
        }
#if defined(NETW_TESTS)
        idle_work.recorders += 1;
#endif
        NETW_ZONE_LEAF_F(
            "Display record entity",
            colors::INTERP,
            profile::SUBSYSTEM_INTERPOLATION,
            "entity %lld",
            static_cast<long long>(runtime->get_route())
        );
        const bool busy = display_record_runtime(runtime, p_tick);
        if (at >= listed.size()) {
            break;
        }
        if (listed[at] != runtime) {
            continue;
        }
        if (busy) {
            listed[kept++] = runtime;
        } else {
            runtime->set_record_listed(false);
        }
    }
    for (uint32_t at = count; at < listed.size(); ++at) {
        if (listed[at] != nullptr) {
            listed[kept++] = listed[at];
        }
    }
    if (kept < listed.size()) {
        listed.resize(kept);
    }
}

bool NetwMultiplayer::display_pump_listed(
    display::Runtime *p_runtime,
    const display::Timing &p_timing,
    display::PumpStats &p_stats
) {
    if (p_runtime->get_disabled()) {
        const int64_t until = p_runtime->get_disable_until_tick();
        if (until < 0) {
            return false;
        }
        if (p_timing.display_tick < until) {
            return true;
        }
        p_runtime->set_disabled(false);
        p_runtime->set_disable_until_tick(-1);
    }
    if (!display_renders) {
        return false;
    }
    const Ref<NetwEntity> entity = p_runtime->entity();
    if (entity.is_null()) {
        return false;
    }
    bool busy
        = display::pump_runtime(p_runtime, p_timing, p_stats, display_hooks);
    const int64_t route = p_runtime->get_route();
    if (plane.wants(EventPlane::DISPLAY_PUMP, route)) {
        busy = true;
        EventPlane::Emission fact(
            EventPlane::DISPLAY_PUMP,
            EventPlane::AFTER,
            clock_engine().get_tick()
        );
        fact.route = route;
        Dictionary detail;
        detail["tick"] = p_timing.display_tick;
        fact.detail = detail;
        plane.emit(fact);
    }
    return busy;
}

Error NetwMultiplayer::display_pump(double p_delta) {
    NETW_STAGE(DISPLAY, "Display pump", colors::INTERP);
#if defined(NETW_TESTS)
    store::AllocationTally pumping(idle_work.display_allocations);
#endif
    display_drain_touched();
    const int64_t frame
        = int64_t(Engine::get_singleton()->get_process_frames());
    if (p_delta > 0.0 && frame == display_last_frame) {
        return OK;
    }
    display_last_frame = frame;
    if (!clock_engine().get_configured()) {
        return OK;
    }
    display_renders = display_renders_here();

    const display::Timing timing
        = display::capture_timing(&clock_engine(), p_delta);
    display::PumpStats &stats = display_book->get_stats();
    stats.reset();

    LocalVector<display::Runtime *> &listed = display_book->pump_set();
    const uint32_t count = listed.size();
    NETW_ZONE_VALUE(count);
    uint32_t kept = 0;
    for (uint32_t at = 0; at < count && at < listed.size(); ++at) {
        display::Runtime *runtime = listed[at];
        if (runtime == nullptr) {
            continue;
        }
#if defined(NETW_TESTS)
        idle_work.pumped += 1;
#endif
        const bool busy = display_pump_listed(runtime, timing, stats);
        if (at >= listed.size()) {
            break;
        }
        if (listed[at] != runtime) {
            continue;
        }
        if (busy) {
            listed[kept++] = runtime;
        } else {
            runtime->set_pump_listed(false);
        }
    }
    for (uint32_t at = count; at < listed.size(); ++at) {
        if (listed[at] != nullptr) {
            listed[kept++] = listed[at];
        }
    }
    if (kept < listed.size()) {
        listed.resize(kept);
    }
    return OK;
}

void NetwMultiplayer::display_bind_session() {
    if (display_session_bound) {
        return;
    }
    display_session_bound = true;
    display_book->set_went_dirty(
        callable_mp(this, &NetwMultiplayer::display_on_book_dirty)
    );
}

void NetwMultiplayer::display_mark_dirty(
    const RID &p_entity,
    netw::display::Dirt p_dirt
) {
    display_book->mark_dirty(p_entity, p_dirt);
}

} // namespace netw
