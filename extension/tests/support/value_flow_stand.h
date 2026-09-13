#pragma once

#include "minted_script.h"

#include "netw_test.h"

#if defined(NETW_TIER_HOSTED)

#include "loopback_rig.h"
#include "world_decl.h"

#include "godot/node.hpp"
#include "godot/script.hpp"
#include "godot/spatial_node.hpp"
#include "godot/variant.hpp"
#include "netw/api/entity.hpp"
#include "netw/api/loopback.hpp"
#include "netw/api/netw_multiplayer.hpp"
#include "netw/api/property_set.hpp"
#include "netw/api/property_set_binding.hpp"

namespace netw_test {

struct FlowPair {
    godot::Node2D *authored = nullptr;
    godot::Vector<godot::Node2D *> mirrors;
    int64_t route = 0;

    godot::Node2D *mirror(int p_client) const {
        REQUIRE_MESSAGE(p_client >= 0, "a mirror is a client's copy");
        REQUIRE_MESSAGE(p_client < mirrors.size(), "no such mirror");
        return mirrors[p_client];
    }
};

inline netw::NetwMultiplayer *flow_core(godot::Object *p_core) {
    netw::NetwMultiplayer *core
        = godot::Object::cast_to<netw::NetwMultiplayer>(p_core);
    REQUIRE_MESSAGE(core != nullptr, "the session has no native core");
    return core;
}

inline godot::Vector2 authored_value(int64_t p_tick) {
    return godot::Vector2(double(p_tick), -double(p_tick));
}

inline godot::Node2D *flow_body(
    const char *p_script_path,
    const godot::String &p_name
) {
    const godot::Ref<godot::Script> script
        = netw_test::script_from(p_script_path);
    REQUIRE_MESSAGE(script.is_valid(), "the body script did not load");
    godot::Node2D *node
        = godot::Object::cast_to<godot::Node2D>(script->call("new"));
    REQUIRE_MESSAGE(node != nullptr, "the body script built no Node2D");
    node->set_name(p_name);
    netw::NetwEntity::ensure(node);
    return node;
}

inline void flow_clocks(
    LoopbackRig &p_rig,
    int p_tickrate,
    int p_display_offset = 3
) {
    p_rig.declare_world(WorldDecl().clocked(p_tickrate, p_display_offset));
}

enum MirrorBinding {
    BIND_EVERY_MIRROR,
    BIND_THE_FIRST_MIRROR,
    BIND_NO_MIRROR,
};

inline bool binds_mirror(MirrorBinding p_binding, int p_client) {
    if (p_binding == BIND_NO_MIRROR) {
        return false;
    }
    return p_binding == BIND_EVERY_MIRROR || p_client == 0;
}

inline FlowPair stand_flow_pair(
    LoopbackRig &p_rig,
    const char *p_script_path,
    const godot::String &p_name,
    MirrorBinding p_binding = BIND_EVERY_MIRROR
) {
    FlowPair pair;
    pair.authored = flow_body(p_script_path, p_name);
    p_rig.branch(-1)->add_child(pair.authored);
    for (int index = 0; index < p_rig.count(); ++index) {
        godot::Node2D *mirror = flow_body(p_script_path, p_name);
        p_rig.branch(index)->add_child(mirror);
        pair.mirrors.push_back(mirror);
    }

    netw::NetwMultiplayer *server = flow_core(p_rig.server());
    const godot::Ref<netw::NetwEntity> authored
        = netw::NetwEntity::of(pair.authored);
    REQUIRE_MESSAGE(authored.is_valid(), "the authored body has no record");
    pair.route = server->liveness_allocate_route(authored.ptr());
    REQUIRE_MESSAGE(pair.route > 0, "the session minted no route");
    server->liveness_bind_route(pair.route, authored.ptr());
    for (int index = 0; index < pair.mirrors.size(); ++index) {
        if (!binds_mirror(p_binding, index)) {
            continue;
        }
        const godot::Ref<netw::NetwEntity> mirror
            = netw::NetwEntity::of(pair.mirrors[index]);
        REQUIRE_MESSAGE(mirror.is_valid(), "a mirror body has no record");
        flow_core(p_rig.client(index))
            ->liveness_bind_route(pair.route, mirror.ptr());
    }
    p_rig.pump();
    return pair;
}

inline void steer(const FlowPair &p_pair, int p_controller) {
    netw::NetwEntity::of(p_pair.authored)->set_controller(p_controller);
    for (int index = 0; index < p_pair.mirrors.size(); ++index) {
        netw::NetwEntity::of(p_pair.mirrors[index])
            ->set_controller(p_controller);
    }
}

inline godot::Ref<netw::NetwPropertySetBinding> binding_of(
    godot::Node *p_node,
    netw::NetwPropertySet::Record p_record
) {
    const godot::Ref<netw::NetwEntity> entity = netw::NetwEntity::of(p_node);
    REQUIRE_MESSAGE(entity.is_valid(), "the body has no record");
    if (p_record == netw::NetwPropertySet::RECORD_INPUT) {
        return entity->get_input_binding();
    }
    if (p_record == netw::NetwPropertySet::RECORD_BROADCAST) {
        return entity->get_broadcast_binding();
    }
    return entity->get_state_binding();
}

inline void author_at(
    godot::Node2D *p_node,
    const godot::StringName &p_property,
    const godot::Variant &p_value,
    netw::NetwPropertySet::Record p_record,
    int64_t p_tick
) {
    p_node->set(p_property, p_value);
    const godot::Ref<netw::NetwPropertySetBinding> binding
        = binding_of(p_node, p_record);
    if (binding.is_valid()) {
        binding->authored_tick = p_tick;
    }
}

inline int64_t clock_tick(LoopbackRig &p_rig, int p_client) {
    return p_rig
        .clock_of(p_client < 0 ? p_rig.server() : p_rig.client(p_client))
        .get_tick();
}

inline godot::Ref<netw::LocalLinkConditions> impairment(
    int64_t p_seed,
    double p_period_ms
) {
    const godot::Ref<netw::LocalLinkConditions> conditions
        = netw::LocalLinkConditions::create(p_seed);
    conditions->set_latency_ms(2.0 * p_period_ms);
    conditions->set_jitter_ms(6.0 * p_period_ms);
    conditions->set_reorder(1.0);
    conditions->set_duplicate(0.3);
    conditions->set_packet_loss(0.05);
    return conditions;
}

struct FlowCapture {
    netw::ClockEngine *receiver = nullptr;
    bool measuring = true;
    int64_t applied = 0;
    int64_t first_tick = -1;
    int64_t last_tick = -1;
    int64_t regressions = 0;
    int64_t repeats = 0;
    int64_t torn = 0;
    int64_t recv_tick = -1;
    int64_t latency_low = 0;
    int64_t latency_high = 0;
    int64_t latency_sum = 0;
    int64_t latency_rows = 0;

    void reset() {
        *this = FlowCapture();
    }

    void note(
        const godot::Dictionary &p_header,
        const godot::StringName &p_field
    ) {
        const int64_t tick = int64_t(p_header.get("tick", -1));
        if (tick < 0) {
            return;
        }
        applied += 1;
        if (first_tick < 0) {
            first_tick = tick;
        }
        if (last_tick >= 0 && tick < last_tick) {
            regressions += 1;
        }
        if (tick == last_tick) {
            repeats += 1;
        }
        last_tick = tick;
        const godot::Dictionary payload
            = p_header.get("payload", godot::Dictionary());
        const godot::Vector2 held = payload.get(p_field, godot::Vector2());
        if (!held.is_equal_approx(authored_value(tick))) {
            torn += 1;
        }
        if (receiver != nullptr) {
            recv_tick = receiver->get_tick();
        }
        if (recv_tick >= 0 && measuring) {
            const int64_t flight = recv_tick - tick;
            if (latency_rows == 0 || flight < latency_low) {
                latency_low = flight;
            }
            if (latency_rows == 0 || flight > latency_high) {
                latency_high = flight;
            }
            latency_sum += flight;
            latency_rows += 1;
        }
    }

    int64_t spread() const {
        return latency_rows == 0 ? 0 : latency_high - latency_low;
    }

    double mean() const {
        return latency_rows == 0 ? 0.0
                                 : double(latency_sum) / double(latency_rows);
    }
};

} // namespace netw_test

#endif
