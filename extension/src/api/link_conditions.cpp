#include "netw/api/link_conditions.hpp"

#include <algorithm>

#include "godot/class_db.hpp"

using namespace godot;

namespace netw {

bool link_shaping_allowed() {
#if defined(DEBUG_ENABLED)
    return true;
#else
    return false;
#endif
}

Ref<NetwLinkConditions> NetwLinkConditions::wifi() {
    Ref<NetwLinkConditions> conditions;
    conditions.instantiate();
    conditions->seed = 1;
    conditions->latency_ms = 35.0;
    conditions->jitter_ms = 8.0;
    conditions->loss_chance = 0.01;
    conditions->reorder_chance = 0.005;
    conditions->duplicate_chance = 0.001;
    return conditions;
}

Ref<NetwLinkConditions> NetwLinkConditions::mobile_4g() {
    Ref<NetwLinkConditions> conditions;
    conditions.instantiate();
    conditions->seed = 2;
    conditions->latency_ms = 85.0;
    conditions->jitter_ms = 25.0;
    conditions->loss_chance = 0.03;
    conditions->reorder_chance = 0.02;
    conditions->duplicate_chance = 0.002;
    conditions->stall_chance = 0.01;
    conditions->stall_ms = 80.0;
    return conditions;
}

Ref<NetwLinkConditions> NetwLinkConditions::poor_3g() {
    Ref<NetwLinkConditions> conditions;
    conditions.instantiate();
    conditions->seed = 3;
    conditions->latency_ms = 220.0;
    conditions->jitter_ms = 90.0;
    conditions->loss_chance = 0.08;
    conditions->reorder_chance = 0.05;
    conditions->duplicate_chance = 0.005;
    conditions->stall_chance = 0.04;
    conditions->stall_ms = 250.0;
    return conditions;
}

Ref<NetwLinkConditions> NetwLinkConditions::satellite() {
    Ref<NetwLinkConditions> conditions;
    conditions.instantiate();
    conditions->seed = 4;
    conditions->latency_ms = 650.0;
    conditions->jitter_ms = 120.0;
    conditions->loss_chance = 0.04;
    conditions->reorder_chance = 0.03;
    conditions->duplicate_chance = 0.003;
    conditions->stall_chance = 0.02;
    conditions->stall_ms = 400.0;
    return conditions;
}

void NetwLinkConditions::set_latency_ms(double p_value) {
    latency_ms = std::max(0.0, p_value);
}

double NetwLinkConditions::get_latency_ms() const {
    return latency_ms;
}

void NetwLinkConditions::set_jitter_ms(double p_value) {
    jitter_ms = std::max(0.0, p_value);
}

double NetwLinkConditions::get_jitter_ms() const {
    return jitter_ms;
}

void NetwLinkConditions::set_loss_chance(double p_value) {
    loss_chance = std::clamp(p_value, 0.0, 1.0);
}

double NetwLinkConditions::get_loss_chance() const {
    return loss_chance;
}

void NetwLinkConditions::set_reorder_chance(double p_value) {
    reorder_chance = std::clamp(p_value, 0.0, 1.0);
}

double NetwLinkConditions::get_reorder_chance() const {
    return reorder_chance;
}

void NetwLinkConditions::set_duplicate_chance(double p_value) {
    duplicate_chance = std::clamp(p_value, 0.0, 1.0);
}

double NetwLinkConditions::get_duplicate_chance() const {
    return duplicate_chance;
}

void NetwLinkConditions::set_stall_chance(double p_value) {
    stall_chance = std::clamp(p_value, 0.0, 1.0);
}

double NetwLinkConditions::get_stall_chance() const {
    return stall_chance;
}

void NetwLinkConditions::set_stall_ms(double p_value) {
    stall_ms = std::max(0.0, p_value);
}

double NetwLinkConditions::get_stall_ms() const {
    return stall_ms;
}

void NetwLinkConditions::set_seed(int64_t p_value) {
    seed = p_value;
}

int64_t NetwLinkConditions::get_seed() const {
    return seed;
}

void NetwLinkConditions::_bind_methods() {
    ClassDB::bind_static_method(
        "NetwLinkConditions",
        D_METHOD("wifi"),
        &NetwLinkConditions::wifi
    );
    ClassDB::bind_static_method(
        "NetwLinkConditions",
        D_METHOD("mobile_4g"),
        &NetwLinkConditions::mobile_4g
    );
    ClassDB::bind_static_method(
        "NetwLinkConditions",
        D_METHOD("poor_3g"),
        &NetwLinkConditions::poor_3g
    );
    ClassDB::bind_static_method(
        "NetwLinkConditions",
        D_METHOD("satellite"),
        &NetwLinkConditions::satellite
    );
    ClassDB::bind_method(
        D_METHOD("set_latency_ms", "value"),
        &NetwLinkConditions::set_latency_ms
    );
    ClassDB::bind_method(
        D_METHOD("get_latency_ms"),
        &NetwLinkConditions::get_latency_ms
    );
    ADD_PROPERTY(
        PropertyInfo(Variant::FLOAT, "latency_ms"),
        "set_latency_ms",
        "get_latency_ms"
    );
    ClassDB::bind_method(
        D_METHOD("set_jitter_ms", "value"),
        &NetwLinkConditions::set_jitter_ms
    );
    ClassDB::bind_method(
        D_METHOD("get_jitter_ms"),
        &NetwLinkConditions::get_jitter_ms
    );
    ADD_PROPERTY(
        PropertyInfo(Variant::FLOAT, "jitter_ms"),
        "set_jitter_ms",
        "get_jitter_ms"
    );
    ClassDB::bind_method(
        D_METHOD("set_loss_chance", "value"),
        &NetwLinkConditions::set_loss_chance
    );
    ClassDB::bind_method(
        D_METHOD("get_loss_chance"),
        &NetwLinkConditions::get_loss_chance
    );
    ADD_PROPERTY(
        PropertyInfo(Variant::FLOAT, "loss_chance"),
        "set_loss_chance",
        "get_loss_chance"
    );
    ClassDB::bind_method(
        D_METHOD("set_reorder_chance", "value"),
        &NetwLinkConditions::set_reorder_chance
    );
    ClassDB::bind_method(
        D_METHOD("get_reorder_chance"),
        &NetwLinkConditions::get_reorder_chance
    );
    ADD_PROPERTY(
        PropertyInfo(Variant::FLOAT, "reorder_chance"),
        "set_reorder_chance",
        "get_reorder_chance"
    );
    ClassDB::bind_method(
        D_METHOD("set_duplicate_chance", "value"),
        &NetwLinkConditions::set_duplicate_chance
    );
    ClassDB::bind_method(
        D_METHOD("get_duplicate_chance"),
        &NetwLinkConditions::get_duplicate_chance
    );
    ADD_PROPERTY(
        PropertyInfo(Variant::FLOAT, "duplicate_chance"),
        "set_duplicate_chance",
        "get_duplicate_chance"
    );
    ClassDB::bind_method(
        D_METHOD("set_stall_chance", "value"),
        &NetwLinkConditions::set_stall_chance
    );
    ClassDB::bind_method(
        D_METHOD("get_stall_chance"),
        &NetwLinkConditions::get_stall_chance
    );
    ADD_PROPERTY(
        PropertyInfo(Variant::FLOAT, "stall_chance"),
        "set_stall_chance",
        "get_stall_chance"
    );
    ClassDB::bind_method(
        D_METHOD("set_stall_ms", "value"),
        &NetwLinkConditions::set_stall_ms
    );
    ClassDB::bind_method(
        D_METHOD("get_stall_ms"),
        &NetwLinkConditions::get_stall_ms
    );
    ADD_PROPERTY(
        PropertyInfo(Variant::FLOAT, "stall_ms"),
        "set_stall_ms",
        "get_stall_ms"
    );
    ClassDB::bind_method(
        D_METHOD("set_seed", "value"),
        &NetwLinkConditions::set_seed
    );
    ClassDB::bind_method(D_METHOD("get_seed"), &NetwLinkConditions::get_seed);
    ADD_PROPERTY(PropertyInfo(Variant::INT, "seed"), "set_seed", "get_seed");
}

} // namespace netw
