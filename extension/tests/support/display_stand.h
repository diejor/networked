#pragma once

#include "netw_test.h"

#if defined(NETW_TIER_HOSTED)

#include "godot/node.hpp"
#include "godot/scene_tree.hpp"
#include "godot/spatial_node.hpp"
#include "netw/api/clock_config.hpp"
#include "netw/api/context.hpp"
#include "netw/api/display_handle.hpp"
#include "netw/api/entity.hpp"
#include "netw/api/interpolate.hpp"
#include "netw/api/netw_multiplayer.hpp"
#include "netw/api/tests.hpp"
#include "netw/script/model.hpp"

namespace netw_test {

struct DisplaySubject {
    godot::Node2D *body = nullptr;
    godot::Node2D *first = nullptr;
    godot::Node2D *second = nullptr;
    godot::Ref<netw::NetwEntity> entity;
    int64_t route = 0;
};

inline void install_clock(netw::NetwMultiplayer *p_core) {
    godot::Ref<netw::NetwClockConfig> clock_config;
    clock_config.instantiate();
    clock_config->set_tickrate(30);
    clock_config->set_display_offset(0);
    p_core->clock_initialize(clock_config);
    p_core->clock_engine().set_manual_tick(true);
}

inline godot::Ref<netw::NetwMultiplayer> clocked_session() {
    godot::Ref<netw::NetwMultiplayer> core;
    core.instantiate();
    godot::Ref<netw::NetwClockConfig> clock_config;
    clock_config.instantiate();
    clock_config->set_tickrate(30);
    clock_config->set_display_offset(0);
    core->clock_initialize(clock_config);
    core->clock_engine().set_manual_tick(true);
    return core;
}

inline DisplaySubject stand_subject(
    const godot::Ref<netw::NetwMultiplayer> &p_core,
    int p_index,
    godot::Node *p_parent = nullptr
) {
    DisplaySubject subject;
    subject.route = 7 + p_index;
    subject.body = memnew(godot::Node2D);
    subject.body->set_name(godot::vformat("Subject%d", p_index));
    subject.first = memnew(godot::Node2D);
    subject.first->set_name("First");
    subject.body->add_child(subject.first);
    subject.second = memnew(godot::Node2D);
    subject.second->set_name("Second");
    subject.body->add_child(subject.second);

    subject.entity = netw::NetwEntity::ensure(subject.body);
    const godot::Ref<netw::NetwDisplayHandle> handle
        = subject.entity->get_interpolation();
    handle->set_enable_smart_dilation(false);
    handle->set_display_role(netw::NetwMultiplayer::DISPLAY_ROLE_REMOTE);
    handle->set_visual_root(godot::NodePath("First"));
    godot::Node *parent
        = p_parent != nullptr ? p_parent : netw::gd::scene_root();
    parent->add_child(subject.body);

    godot::Ref<netw::NetwInterpolate> spec;
    spec.instantiate();
    netw::Netw::configure_property(
        subject.body,
        godot::StringName("position"),
        true
    )
        ->interpolate(
            netw::gd::array_of(
                spec->lerp()->smooth(0.0)->to(godot::StringName("position"))
            )
        );
    p_core->liveness_bind_route(subject.route, subject.entity.ptr());
    return subject;
}

inline void retire_subject(const DisplaySubject &p_subject) {
    godot::Node *parent = p_subject.body->get_parent();
    if (parent != nullptr) {
        parent->remove_child(p_subject.body);
    }
    memdelete(p_subject.body);
}

inline void record_sample(
    const godot::Ref<netw::NetwMultiplayer> &p_core,
    godot::Node *p_node,
    const godot::Vector2 &p_value,
    int64_t p_tick,
    bool p_authoring_tick = false
) {
    const godot::Ref<netw::NetwInterpolate> spec
        = netw::script::model::get_node_property_interpolator(
            p_node,
            godot::StringName("position")
        );
    netw::NetwNativeTests::display_record(
        p_core.ptr(),
        p_node,
        godot::StringName("position"),
        p_value,
        p_tick,
        spec,
        p_authoring_tick
    );
}

inline void pump_at(
    const godot::Ref<netw::NetwMultiplayer> &p_core,
    int64_t p_tick,
    int p_offset,
    double p_factor
) {
    netw::ClockEngine &clock = p_core->clock_engine();
    clock.set_tick(int(p_tick));
    clock.set_display_offset(p_offset);
    clock.set_tick_factor_override(p_factor);
    netw::NetwNativeTests::display_pump(p_core.ptr(), 0.0);
}

} // namespace netw_test

#endif
