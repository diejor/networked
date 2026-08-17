#pragma once

/* What a declaration says about an entity's fields, and what it compiles to.
 *
 * The compile happens once per rewire and every later read is an index, so a
 * field name is hashed in one place and a recovery reading eleven tables reads
 * eleven columns rather than eleven hash lookups per field per tick.
 */

#include <cstdint>

#include "godot/local_vector.hpp"
#include "godot/ref_counted.hpp"
#include "godot/variant.hpp"
#include "netw/quantize.hpp"

namespace netw {

using namespace godot;

namespace predict {

// The state family a field belongs to, which decides what a recovery may
// write. These are `_PredictionEngine.STATE_FAMILY_*` and cross as plain ints.
enum class StateFamily : int {
    POSE = 0,
    MOMENTUM = 1,
    CONTROLLER = 2,
};

// `NetwPropertySet.PropertyClass`, mirrored for the same reason the rest of
// the prediction vocabulary is.
enum class PropertyClass : int {
    CAUSAL = 0,
    DERIVED = 1,
    COSMETIC = 2,
};

/* One field as its declaration states it, before any table is compiled.
 *
 * Every value here is read off a property mark in the shell, so this record is
 * the whole of what a declaration says about a field and the compile below
 * reads nothing else. A negative `epsilon_override` or `teleport_at` is the
 * absence of the declaration rather than a value.
 */
struct FieldDecl {
    StringName key;
    StringName carry_channel;
    Ref<NetwQuantize> quantizer;
    int property_class = int(PropertyClass::CAUSAL);
    // The declaring node's property type, resolved once here rather than per
    // canonicalization, because a codec that reads the node is a codec the
    // pool cannot run.
    int type = int(Variant::NIL);
    double converge_stiffness = 0.0;
    double epsilon_override = -1.0;
    double teleport_at = -1.0;
    bool teleport_only = false;
    bool reconcile_only = false;
    bool angle = false;
};

/* The declared fields as the wire reads them: key, quantizer, type, in
 * declaration order.
 *
 * A predicting client and its authority never see the same float twice, one
 * reads a live property and the other reads what survived the wire, so the
 * form both can agree on is the round trip rather than either live value.
 * This is the whole of what producing that form needs.
 */
struct FieldCodec {
    LocalVector<StringName> keys;
    LocalVector<Ref<NetwQuantize>> quantizers;
    LocalVector<int> types;

    void append(
        const StringName &p_key,
        const Ref<NetwQuantize> &p_quantizer,
        int p_type
    );

    int count() const {
        return int(keys.size());
    }

    void clear();
};

// The bytes that ship, in declaration order, for the fields p_payload carries.
// Two payloads are comparable exactly when they name the same fields.
PackedByteArray canonical_bytes(
    const FieldCodec &p_codec,
    const Dictionary &p_payload
);

// p_payload with every declared value round-tripped through its own codec.
Dictionary canonicalize(
    const FieldCodec &p_codec,
    const Dictionary &p_payload
);

/* Every declared field at the zero of its own declared type.
 *
 * A field whose declaration carries no type is OMITTED rather than guessed:
 * the whole point of the row is that it commands nothing, and a value invented
 * for a type nobody declared is a command like any other.
 */
Dictionary zero_row(const FieldCodec &p_codec);

/* The declared fields in declaration order, and the only place a name is
 * hashed.
 *
 * The order is the entity's state set's column order, so a field's slot here
 * and its column on the wire are one number. Minting a second numbering would
 * make "which field is this" a question two subsystems answer differently.
 */
class FieldTable {
    LocalVector<StringName> names;
    HashMap<StringName, int> index;

public:
    void append(const StringName &p_key);
    int index_of(const StringName &p_key) const;
    const StringName &name_at(int p_slot) const;

    int count() const {
        return int(names.size());
    }

    void clear();
};

/* Every field-keyed table the recovery path reads, as dense columns.
 *
 * The GDScript record this replaces is eleven `Dictionary`s keyed by field
 * name, re-hashed on every read inside a per-tick loop. Here a lookup is one
 * index, resolved once at rewire, and a table is a column indexed by slot.
 *
 * `projection` holds the slot of the channel that advances a field, or -1.
 * `converge_rate`, `epsilon` and `teleport` hold -1.0 where the declaration
 * said nothing, which is the same absence the GDScript tables spell by not
 * holding the key.
 */
struct Wiring {
    FieldTable fields;
    FieldCodec codec;

    LocalVector<int> projection;
    LocalVector<int> state_family;
    LocalVector<double> converge_rate;
    LocalVector<double> epsilon;
    LocalVector<double> teleport;
    LocalVector<uint8_t> pose;
    LocalVector<uint8_t> withheld;
    LocalVector<uint8_t> trigger_exclude;
    LocalVector<uint8_t> vote_exclude;
    LocalVector<uint8_t> angle;
    LocalVector<uint8_t> causal;

    void resize(int p_count);

    int count() const {
        return fields.count();
    }
};

// Compiles a declaration into the tables a recovery reads. The one function
// this batch certifies against the GDScript engine, field for field.
Wiring compile(const LocalVector<FieldDecl> &p_declaration);

/* A point in the family's declared axis space, which the pool answers for.
 *
 * A configuration is asked about rather than assumed, so a caller outside the
 * supported set learns it before driving rather than after diverging.
 */
struct Config {
    int schedule = 0;
    int role = 0;
    int correction = 0;
    int restore = 0;
    int max_restore_ticks = 6;
    int island = 0;
    bool carry = false;
    bool witness = false;
};

} // namespace predict

} // namespace netw
