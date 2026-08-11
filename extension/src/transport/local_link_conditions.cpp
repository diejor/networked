#include "netw/transport/loopback.hpp"

#include <algorithm>

#include "godot/class_db.hpp"

using namespace godot;

namespace netw {

Ref<LocalLinkConditions> LocalLinkConditions::create(int64_t p_seed) {
    Ref<LocalLinkConditions> conditions;
    conditions.instantiate();
    conditions->seed = p_seed;
    return conditions;
}

Ref<LocalLinkConditions> LocalLinkConditions::perfect() {
    return create();
}

Ref<LocalLinkConditions> LocalLinkConditions::wifi() {
    Ref<LocalLinkConditions> conditions = create(1);
    conditions->latency_ms = 35.0;
    conditions->jitter_ms = 8.0;
    conditions->packet_loss = 0.01;
    conditions->reorder = 0.005;
    conditions->duplicate = 0.001;
    return conditions;
}

Ref<LocalLinkConditions> LocalLinkConditions::mobile_4g() {
    Ref<LocalLinkConditions> conditions = create(2);
    conditions->latency_ms = 85.0;
    conditions->jitter_ms = 25.0;
    conditions->packet_loss = 0.03;
    conditions->reorder = 0.02;
    conditions->duplicate = 0.002;
    conditions->throttle = 0.01;
    conditions->throttle_ms = 80.0;
    return conditions;
}

Ref<LocalLinkConditions> LocalLinkConditions::poor_3g() {
    Ref<LocalLinkConditions> conditions = create(3);
    conditions->latency_ms = 220.0;
    conditions->jitter_ms = 90.0;
    conditions->packet_loss = 0.08;
    conditions->reorder = 0.05;
    conditions->duplicate = 0.005;
    conditions->throttle = 0.04;
    conditions->throttle_ms = 250.0;
    return conditions;
}

Ref<LocalLinkConditions> LocalLinkConditions::satellite() {
    Ref<LocalLinkConditions> conditions = create(4);
    conditions->latency_ms = 650.0;
    conditions->jitter_ms = 120.0;
    conditions->packet_loss = 0.04;
    conditions->reorder = 0.03;
    conditions->duplicate = 0.003;
    conditions->throttle = 0.02;
    conditions->throttle_ms = 400.0;
    return conditions;
}

Ref<LocalLinkConditions> LocalLinkConditions::polls(
    int p_count,
    double p_period_ms
) {
    Ref<LocalLinkConditions> result = create();
    result->set_latency_ms(double(p_count) * p_period_ms);
    return result;
}

Ref<LocalLinkConditions> LocalLinkConditions::clone() const {
    Ref<LocalLinkConditions> copy = create(seed);
    copy->latency_ms = latency_ms;
    copy->jitter_ms = jitter_ms;
    copy->packet_loss = packet_loss;
    copy->reorder = reorder;
    copy->duplicate = duplicate;
    copy->throttle = throttle;
    copy->throttle_ms = throttle_ms;
    copy->retransmit_ms = retransmit_ms;
    return copy;
}

double LocalLinkConditions::effective_latency_ms() const {
    return std::max(0.0, latency_ms);
}

double LocalLinkConditions::effective_retransmit_ms() const {
    if (retransmit_ms >= 0.0) {
        return retransmit_ms;
    }
    return std::max(DEFAULT_RETRANSMIT_MS, 2.0 * std::max(0.0, latency_ms));
}

void LocalLinkConditions::set_latency_ms(double p_value) {
    latency_ms = p_value;
}

double LocalLinkConditions::get_latency_ms() const {
    return latency_ms;
}

void LocalLinkConditions::set_jitter_ms(double p_value) {
    jitter_ms = p_value;
}

double LocalLinkConditions::get_jitter_ms() const {
    return jitter_ms;
}

void LocalLinkConditions::set_packet_loss(double p_value) {
    packet_loss = p_value;
}

double LocalLinkConditions::get_packet_loss() const {
    return packet_loss;
}

void LocalLinkConditions::set_reorder(double p_value) {
    reorder = p_value;
}

double LocalLinkConditions::get_reorder() const {
    return reorder;
}

void LocalLinkConditions::set_duplicate(double p_value) {
    duplicate = p_value;
}

double LocalLinkConditions::get_duplicate() const {
    return duplicate;
}

void LocalLinkConditions::set_throttle(double p_value) {
    throttle = p_value;
}

double LocalLinkConditions::get_throttle() const {
    return throttle;
}

void LocalLinkConditions::set_throttle_ms(double p_value) {
    throttle_ms = p_value;
}

double LocalLinkConditions::get_throttle_ms() const {
    return throttle_ms;
}

void LocalLinkConditions::set_retransmit_ms(double p_value) {
    retransmit_ms = p_value;
}

double LocalLinkConditions::get_retransmit_ms() const {
    return retransmit_ms;
}

void LocalLinkConditions::set_seed(int64_t p_value) {
    seed = p_value;
}

int64_t LocalLinkConditions::get_seed() const {
    return seed;
}

void LocalLinkConditions::_bind_methods() {
    ClassDB::bind_static_method(
        "LocalLinkConditions",
        D_METHOD("create", "seed"),
        &LocalLinkConditions::create,
        DEFVAL(0)
    );
    ClassDB::bind_static_method(
        "LocalLinkConditions",
        D_METHOD("perfect"),
        &LocalLinkConditions::perfect
    );
    ClassDB::bind_static_method(
        "LocalLinkConditions",
        D_METHOD("wifi"),
        &LocalLinkConditions::wifi
    );
    ClassDB::bind_static_method(
        "LocalLinkConditions",
        D_METHOD("mobile_4g"),
        &LocalLinkConditions::mobile_4g
    );
    ClassDB::bind_static_method(
        "LocalLinkConditions",
        D_METHOD("poor_3g"),
        &LocalLinkConditions::poor_3g
    );
    ClassDB::bind_static_method(
        "LocalLinkConditions",
        D_METHOD("satellite"),
        &LocalLinkConditions::satellite
    );

    ClassDB::bind_method(D_METHOD("clone"), &LocalLinkConditions::clone);
    ClassDB::bind_method(
        D_METHOD("effective_latency_ms"),
        &LocalLinkConditions::effective_latency_ms
    );
    ClassDB::bind_method(
        D_METHOD("effective_retransmit_ms"),
        &LocalLinkConditions::effective_retransmit_ms
    );

    ClassDB::bind_method(
        D_METHOD("set_latency_ms", "value"),
        &LocalLinkConditions::set_latency_ms
    );
    ClassDB::bind_method(
        D_METHOD("get_latency_ms"),
        &LocalLinkConditions::get_latency_ms
    );
    ClassDB::bind_method(
        D_METHOD("set_jitter_ms", "value"),
        &LocalLinkConditions::set_jitter_ms
    );
    ClassDB::bind_method(
        D_METHOD("get_jitter_ms"),
        &LocalLinkConditions::get_jitter_ms
    );
    ClassDB::bind_method(
        D_METHOD("set_packet_loss", "value"),
        &LocalLinkConditions::set_packet_loss
    );
    ClassDB::bind_method(
        D_METHOD("get_packet_loss"),
        &LocalLinkConditions::get_packet_loss
    );
    ClassDB::bind_method(
        D_METHOD("set_reorder", "value"),
        &LocalLinkConditions::set_reorder
    );
    ClassDB::bind_method(
        D_METHOD("get_reorder"),
        &LocalLinkConditions::get_reorder
    );
    ClassDB::bind_method(
        D_METHOD("set_duplicate", "value"),
        &LocalLinkConditions::set_duplicate
    );
    ClassDB::bind_method(
        D_METHOD("get_duplicate"),
        &LocalLinkConditions::get_duplicate
    );
    ClassDB::bind_method(
        D_METHOD("set_throttle", "value"),
        &LocalLinkConditions::set_throttle
    );
    ClassDB::bind_method(
        D_METHOD("get_throttle"),
        &LocalLinkConditions::get_throttle
    );
    ClassDB::bind_method(
        D_METHOD("set_throttle_ms", "value"),
        &LocalLinkConditions::set_throttle_ms
    );
    ClassDB::bind_method(
        D_METHOD("get_throttle_ms"),
        &LocalLinkConditions::get_throttle_ms
    );
    ClassDB::bind_method(
        D_METHOD("set_retransmit_ms", "value"),
        &LocalLinkConditions::set_retransmit_ms
    );
    ClassDB::bind_method(
        D_METHOD("get_retransmit_ms"),
        &LocalLinkConditions::get_retransmit_ms
    );
    ClassDB::bind_method(
        D_METHOD("set_seed", "value"),
        &LocalLinkConditions::set_seed
    );
    ClassDB::bind_method(D_METHOD("get_seed"), &LocalLinkConditions::get_seed);

    ADD_PROPERTY(
        PropertyInfo(Variant::FLOAT, "latency_ms"),
        "set_latency_ms",
        "get_latency_ms"
    );
    ADD_PROPERTY(
        PropertyInfo(Variant::FLOAT, "jitter_ms"),
        "set_jitter_ms",
        "get_jitter_ms"
    );
    ADD_PROPERTY(
        PropertyInfo(Variant::FLOAT, "packet_loss"),
        "set_packet_loss",
        "get_packet_loss"
    );
    ADD_PROPERTY(
        PropertyInfo(Variant::FLOAT, "reorder"),
        "set_reorder",
        "get_reorder"
    );
    ADD_PROPERTY(
        PropertyInfo(Variant::FLOAT, "duplicate"),
        "set_duplicate",
        "get_duplicate"
    );
    ADD_PROPERTY(
        PropertyInfo(Variant::FLOAT, "throttle"),
        "set_throttle",
        "get_throttle"
    );
    ADD_PROPERTY(
        PropertyInfo(Variant::FLOAT, "throttle_ms"),
        "set_throttle_ms",
        "get_throttle_ms"
    );
    ADD_PROPERTY(
        PropertyInfo(Variant::FLOAT, "retransmit_ms"),
        "set_retransmit_ms",
        "get_retransmit_ms"
    );
    ADD_PROPERTY(PropertyInfo(Variant::INT, "seed"), "set_seed", "get_seed");
}

} // namespace netw
