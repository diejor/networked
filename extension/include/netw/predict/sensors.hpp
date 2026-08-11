#pragma once

/* Deterministic evidence reduced from live environment and solve samples.
 *
 * The shell owns sampling because sensors and colliders are live tree objects.
 * This core owns the comparable result: identities and peer-invariant classes
 * are sorted before hashing, while local realization and boundary relation are
 * retained only as diagnostic and control facts.
 */

#include "godot/local_vector.hpp"
#include "godot/object.hpp"
#include "godot/variant.hpp"

namespace netw {

using namespace godot;

namespace predict {

enum WitnessClass : uint8_t {
    SENSOR_WITNESS_NONE = 0,
    SENSOR_WITNESS_SUPPORT = 1,
    SENSOR_WITNESS_STATIC = 2,
    SENSOR_WITNESS_DYNAMIC_ENTITY = 4,
};

// `NetwPredict.ContactClass`. Local rather than compared, and an ORDINAL
// rather than a bit, because a summary folds it into a bit per class.
enum class ContactClass : int {
    NONE = 0,
    DECLARED_SUPPORT = 1,
    OTHER_STATIC = 2,
    PREDICTED_DYNAMIC = 3,
    UNPREDICTED_DYNAMIC = 4,
    KINEMATIC_PROXY = 5,
};

struct WitnessContact {
    String identity;
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
    const LocalVector<WitnessContact> &p_contacts,
    bool p_sleeping
);

/* Fixed world geometry, which every peer holds identically and can compare.
 *
 * Asked by class NAME so the core needs neither physics nor tilemap headers
 * in either tier. An animatable body is a StaticBody subclass that moves, so
 * it is excluded before the rest is asked.
 */
bool static_geometry(Object *p_collider);

// Whether the collider IS the declared support is the caller's answer, since
// an identity is minted from an entity roster the pool does not hold.
int witness_class(Object *p_collider, bool p_declared_support);

/* Folds the declared world an entity ran against.
 *
 * An entity that declared no epoch and no sensors digests to zero, which is
 * the value an unwritten row already holds and the honest answer besides: it
 * declared nothing, so it can discover nothing.
 */
int32_t environment_digest(int64_t p_epoch, const Dictionary &p_samples);

int32_t topology_fingerprint(
    const Dictionary &p_facts,
    int p_quantum
);

} // namespace predict

} // namespace netw
