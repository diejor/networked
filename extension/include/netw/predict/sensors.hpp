#pragma once

#include "godot/local_vector.hpp"
#include "godot/object.hpp"
#include "godot/variant.hpp"

namespace netw::predict {

enum WitnessClass : uint8_t {
    SENSOR_WITNESS_NONE = 0,
    SENSOR_WITNESS_SUPPORT = 1,
    SENSOR_WITNESS_STATIC = 2,
    SENSOR_WITNESS_DYNAMIC_ENTITY = 4,
};

enum class ContactClass : int {
    NONE = 0,
    DECLARED_SUPPORT = 1,
    OTHER_STATIC = 2,
    PREDICTED_DYNAMIC = 3,
    UNPREDICTED_DYNAMIC = 4,
    KINEMATIC_PROXY = 5,
};

struct WitnessContact {
    godot::String identity;
    int witness_class = SENSOR_WITNESS_NONE;
    int realization = 0;
    bool outside_boundary = false;
};

struct WitnessSummary {
    int32_t fingerprint = 0;
    uint8_t class_bits = 0;
    uint8_t realization_bits = 0;
    int contact_count = 0;
    bool breach = false;
    bool sleeping = false;
    bool valid = false;
};

WitnessSummary summarize_witness(
    const godot::LocalVector<WitnessContact> &p_contacts,
    bool p_sleeping
);

bool static_geometry(godot::Object *p_collider);

int witness_class(godot::Object *p_collider, bool p_declared_support);

int32_t environment_digest(int64_t p_epoch, const godot::Dictionary &p_samples);

int32_t topology_fingerprint(const godot::Dictionary &p_facts, int p_quantum);

} // namespace netw::predict
