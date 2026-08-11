#include "netw/predict/sensors.hpp"

#include "netw/colors.hpp"
#include "netw/log.hpp"
#include "netw/prediction_core.hpp"
#include "netw/profile.hpp"

namespace netw {

namespace predict {

namespace {

struct ContactOrder {
    bool operator()(
        const WitnessContact &p_left,
        const WitnessContact &p_right
    ) const {
        const String left = p_left.identity;
        const String right = p_right.identity;
        if (left == right) {
            return p_left.witness_class < p_right.witness_class;
        }
        return left < right;
    }
};

bool valid_class(int p_class) {
    return p_class == SENSOR_WITNESS_SUPPORT
        || p_class == SENSOR_WITNESS_STATIC
        || p_class == SENSOR_WITNESS_DYNAMIC_ENTITY;
}

} // namespace

WitnessSummary summarize_witness(
    const LocalVector<WitnessContact> &p_contacts,
    bool p_sleeping
) {
    NETW_ZONE_NC("NetwPredict witness", colors::PREDICTION);
    WitnessSummary out;
    out.sleeping = p_sleeping;
    LocalVector<WitnessContact> ordered;
    for (uint32_t at = 0; at < p_contacts.size(); ++at) {
        const WitnessContact &contact = p_contacts[at];
        if (contact.identity.is_empty() || !valid_class(contact.witness_class)
            || contact.realization < 0 || contact.realization > 5) {
            NETW_ERROR(
                "prediction",
                "Witness contact %d has an invalid identity or class.",
                int(at)
            );
            return out;
        }
        ordered.push_back(contact);
    }
    ordered.sort_custom<ContactOrder>();

    PackedStringArray compared;
    String previous;
    int previous_class = -1;
    for (uint32_t at = 0; at < ordered.size(); ++at) {
        const WitnessContact &contact = ordered[at];
        if (contact.identity != previous
            || contact.witness_class != previous_class) {
            compared.push_back(
                vformat("%s:%d", contact.identity, contact.witness_class)
            );
            previous = contact.identity;
            previous_class = contact.witness_class;
        }
        out.class_bits = uint8_t(out.class_bits | contact.witness_class);
        out.realization_bits = uint8_t(
            out.realization_bits | (1U << uint32_t(contact.realization))
        );
        out.breach = out.breach || contact.outside_boundary;
    }
    Dictionary facts;
    facts[StringName("contacts")] = compared;

    out.fingerprint
        = int32_t(NetwPredictionCore::fact_fingerprint(facts));
    out.contact_count = int(p_contacts.size());
    out.valid = true;
    NETW_TRACE(
        "prediction",
        "witness contacts=%d classes=%d breach=%d",
        out.contact_count,
        int(out.class_bits),
        int(out.breach)
    );
    return out;
}

int32_t environment_digest(int64_t p_epoch, const Dictionary &p_samples) {
    NETW_ZONE_NC("NetwPredict sensors", colors::PREDICTION);
    if (p_epoch == -1 && p_samples.is_empty()) {
        return 0;
    }
    return int32_t(NetwPredictionCore::environment_digest(p_epoch, p_samples));
}

int32_t topology_fingerprint(
    const Dictionary &p_facts,
    int p_quantum
) {
    NETW_ZONE_NC("NetwPredict topology", colors::PREDICTION);
    Dictionary facts = p_facts.duplicate();
    facts[StringName("quantum")] = p_quantum;
    return int32_t(NetwPredictionCore::fact_fingerprint(facts));
}

bool static_geometry(Object *p_collider) {
    if (p_collider == nullptr) {
        return false;
    }
    if (p_collider->is_class("AnimatableBody2D")
        || p_collider->is_class("AnimatableBody3D")) {
        return false;
    }
    return p_collider->is_class("StaticBody2D")
        || p_collider->is_class("StaticBody3D")
        || p_collider->is_class("GridMap")
        || p_collider->is_class("CSGShape3D")
        || p_collider->is_class("TileMap")
        || p_collider->is_class("TileMapLayer");
}

int witness_class(Object *p_collider, bool p_declared_support) {
    if (p_declared_support) {
        return SENSOR_WITNESS_SUPPORT;
    }
    return static_geometry(p_collider) ? SENSOR_WITNESS_STATIC
                                       : SENSOR_WITNESS_DYNAMIC_ENTITY;
}

} // namespace predict

} // namespace netw
