#pragma once

#include <cstdint>

#include "godot/local_vector.hpp"
#include "godot/ref_counted.hpp"
#include "godot/rid.hpp"
#include "godot/variant.hpp"

namespace netw {

// Packed peer-bit operations over 63-bit words. The sign bit is never touched,
// so every row is a positive integer on both sides of the language boundary,
// where GDScript has no unsigned int to receive it in.
//
// Rows are bare `PackedInt64Array` by contract rather than by convenience: they
// cross to worker tasks by value, and an Object or a per-row wrapper is what
// would stop them.
class NetwInterestBitSet : public godot::RefCounted {
    GDCLASS(NetwInterestBitSet, godot::RefCounted)

protected:
    static void _bind_methods();

public:
    static constexpr int BITS_PER_WORD = 63;
    static constexpr int64_t WORD_MASK = 0x7FFFFFFFFFFFFFFF;

    static int word_count(int bit_count);
    static godot::PackedInt64Array empty(int bit_count);
    static godot::PackedInt64Array full(int bit_count);
    static godot::PackedInt64Array resized(
        const godot::PackedInt64Array &row,
        int words
    );
    static godot::PackedInt64Array with_bit(
        const godot::PackedInt64Array &row,
        int bit,
        bool value
    );
    static bool test(const godot::PackedInt64Array &row, int bit);
    static godot::PackedInt64Array union_of(
        const godot::PackedInt64Array &left,
        const godot::PackedInt64Array &right
    );
    static godot::PackedInt64Array intersect(
        const godot::PackedInt64Array &left,
        const godot::PackedInt64Array &right
    );
    static godot::PackedInt64Array subtract(
        const godot::PackedInt64Array &left,
        const godot::PackedInt64Array &right
    );
    static godot::PackedInt64Array symmetric_difference(
        const godot::PackedInt64Array &left,
        const godot::PackedInt64Array &right
    );
    static bool equals(
        const godot::PackedInt64Array &left,
        const godot::PackedInt64Array &right
    );
    static godot::PackedInt32Array bits(const godot::PackedInt64Array &row);
    static int popcount(const godot::PackedInt64Array &row);
};

// Plain counters describing one recompute. They belong to the pass that
// produced them, so a pass that is computed and never committed carries its own
// counters away with it.
class NetwInterestStats : public godot::RefCounted {
    GDCLASS(NetwInterestStats, godot::RefCounted)

protected:
    static void _bind_methods();

public:
    int32_t layers_recomputed = 0;
    int32_t entities_recomputed = 0;
    int32_t edges = 0;
    int32_t shows = 0;
    int32_t hides = 0;
    int32_t words_per_row = 0;
    int32_t vanished_dirty_skips = 0;

    int get_layers_recomputed() const {
        return layers_recomputed;
    }
    int get_entities_recomputed() const {
        return entities_recomputed;
    }
    int get_edges() const {
        return edges;
    }
    int get_shows() const {
        return shows;
    }
    int get_hides() const {
        return hides;
    }
    int get_words_per_row() const {
        return words_per_row;
    }
    int get_vanished_dirty_skips() const {
        return vanished_dirty_skips;
    }

    godot::PackedInt32Array to_array() const;
};

// What one recompute produced: the rows that move, the ordered per-peer
// transitions, the same transitions attributed to the layer that owes them, and
// the counters.
//
// It is a value handed out by `recompute` and handed back to `commit`, and
// between those two calls the engine's read matrix is untouched, which is what
// lets a caller decide what a transition means before acting on it. The
// snapshots `commit` needs are carried inside it and are nobody else's
// business, so they are not on the bound surface.
//
// [codeblock]
// keys ┄ old_rows ┄ new_rows ┄ order_keys   parallel, one per moved row
// shows ┄┄┄┄┄┄┄┄┄┄┄ [entity, bit]           ordered parent-first
// hides ┄┄┄┄┄┄┄┄┄┄┄ [entity, bit]           ordered child-first
// layer_shows/hides [layer, entity, bit]    the same edges, attributed
// removed_keys ┄┄┄┄ entities that left      their last row is a hide
// [/codeblock]
class NetwInterestDelta : public godot::RefCounted {
    GDCLASS(NetwInterestDelta, godot::RefCounted)

protected:
    static void _bind_methods();

public:
    godot::PackedInt64Array keys;
    godot::Array old_rows;
    godot::Array new_rows;
    godot::Array order_keys;
    godot::Array shows;
    godot::Array hides;
    godot::Array layer_shows;
    godot::Array layer_hides;
    godot::PackedInt64Array removed_keys;
    godot::Ref<NetwInterestStats> stats;

    // What `commit` advances the read matrix with. Not bound: a caller reading
    // a transition never needs the snapshot the engine will replace its own
    // state with.
    godot::HashMap<godot::StringName, godot::PackedInt64Array> layer_rows;
    godot::HashMap<int64_t, godot::LocalVector<godot::StringName>> memberships;
    int64_t commit_revision = 0;

    NetwInterestDelta();

    bool is_empty() const {
        return keys.is_empty();
    }

    godot::PackedInt64Array get_keys() const {
        return keys;
    }
    godot::Array get_old_rows() const {
        return old_rows;
    }
    godot::Array get_new_rows() const {
        return new_rows;
    }
    godot::Array get_order_keys() const {
        return order_keys;
    }
    godot::Array get_shows() const {
        return shows;
    }
    godot::Array get_hides() const {
        return hides;
    }
    godot::Array get_layer_shows() const {
        return layer_shows;
    }
    godot::Array get_layer_hides() const {
        return layer_hides;
    }
    godot::PackedInt64Array get_removed_keys() const {
        return removed_keys;
    }
    godot::Ref<NetwInterestStats> get_stats() const {
        return stats;
    }

    godot::Array to_array() const;
};

// The interest verdict core: mask algebra from plain state to a peer-row
// matrix.
//
// Entity keys are opaque `int64` slots minted above this class and never
// dereferenced by it, which is what lets one entity outlive whichever handle,
// route or node is standing in for it. Peer bits are likewise assigned outside
// and stay stable for the session. Nothing here reaches an `Object`, reads a
// clock, or emits, so a verdict is a function of state and of nothing else.
//
// [codeblock]
// set_layer / set_membership / set_parent ...   mutations change intake state
// recompute()  -> NetwInterestDelta             computes, changes nothing
// commit(delta)                                 advances the read matrix
// row_of / test / rows / explain                read the committed matrix
// [/codeblock]
//
// A row is granted by the union of its layers (or by every live peer when it
// has none), intersected with synchronizer intent, intersected with its
// parent's row, intersected with the live peers. The parent clamp is the
// uniform-visibility invariant: a peer never holds a child whose parent it
// cannot see.
class NetwInterestEngine : public godot::RefCounted {
    GDCLASS(NetwInterestEngine, godot::RefCounted)

public:
    enum Policy {
        HIDE_FROM_OUTSIDERS = 0,
        HIDE_FROM_INSIDERS = 1,
    };

private:
    struct Layer {
        godot::PackedInt64Array viewers;
        Policy policy = HIDE_FROM_OUTSIDERS;
    };

    struct Record {
        godot::LocalVector<godot::StringName> layers;
        int64_t parent = 0;
        godot::PackedInt64Array intent;
        bool intent_all = true;
        int32_t depth = 0;
        int32_t route = 0;
    };

    // A (depth, route) pair. Hides run deeper-first and shows shallower-first
    // over this order, and route breaks the tie so the order is total.
    struct Order {
        int32_t depth = 0;
        int32_t route = 0;

        bool precedes(const Order &other, bool deeper_first) const;
        bool operator==(const Order &other) const {
            return depth == other.depth && route == other.route;
        }
    };

    struct Transition {
        int64_t key = 0;
        int32_t bit = 0;
        Order order;
    };

    struct LayerTransition {
        godot::StringName layer;
        int64_t key = 0;
        int32_t bit = 0;
        Order order;
    };

    godot::HashMap<godot::StringName, Layer> layers;
    godot::HashMap<int64_t, Record> entities;
    godot::HashMap<godot::StringName, godot::PackedInt64Array> layer_rows;
    godot::HashMap<int64_t, godot::PackedInt64Array> committed;
    godot::HashMap<int64_t, godot::LocalVector<godot::StringName>>
        committed_memberships;
    godot::HashSet<godot::StringName> dirty_layers;
    godot::HashSet<int64_t> dirty_entities;
    godot::HashMap<int64_t, Order> removed;
    godot::PackedInt64Array live_peers;
    godot::Ref<NetwInterestStats> last_stats;
    int64_t revision = 0;

    Record &record_for(int64_t key);
    void ensure_layer(const godot::StringName &id);
    void mark_layer_members_dirty(const godot::StringName &id);
    void mark_entity_tree_dirty(int64_t root);
    bool layer_admits_bit(const godot::StringName &id, int bit) const;
    Order order_of(int64_t key) const;

    godot::PackedInt64Array compute_entity_row(
        int64_t key,
        const godot::HashMap<godot::StringName, godot::PackedInt64Array>
            &rows_by_layer,
        const godot::HashMap<int64_t, godot::PackedInt64Array> &desired_rows
    ) const;

    void recompute_layers(
        godot::HashMap<godot::StringName, godot::PackedInt64Array>
            &rows_by_layer,
        const godot::Ref<NetwInterestStats> &out_stats
    ) const;

    void append_transition_bits(
        int64_t key,
        const godot::PackedInt64Array &old_row,
        const godot::PackedInt64Array &desired,
        const Order &order,
        godot::LocalVector<Transition> &shows,
        godot::LocalVector<Transition> &hides
    ) const;

    void append_layer_transition_bits(
        const godot::StringName &id,
        int64_t key,
        const godot::PackedInt64Array &old_row,
        const godot::PackedInt64Array &desired,
        const Order &order,
        godot::LocalVector<LayerTransition> &shows,
        godot::LocalVector<LayerTransition> &hides
    ) const;

    godot::LocalVector<int64_t> ordered_dirty_keys() const;
    godot::LocalVector<int64_t> ordered_removed_keys() const;

    int edge_count_after(
        const godot::HashMap<int64_t, godot::PackedInt64Array> &changed,
        const godot::PackedInt64Array &removed_keys
    ) const;

protected:
    static void _bind_methods();

public:
    NetwInterestEngine();

    // Replaces one layer's viewer row and policy. Re-writing a layer with the
    // values it already holds is inert, so an idempotent caller costs nothing.
    void set_layer(
        const godot::StringName &id,
        const godot::PackedInt64Array &viewers,
        int policy
    );

    // Removes a layer. A membership naming a layer that is gone contributes
    // nothing to the union, which is not the same as the layer admitting
    // everyone.
    void remove_layer(const godot::StringName &id);

    // Whether a slot may name an entity. Zero is the null key, so an entity
    // registered there could never be anybody's parent, and every mutation
    // refuses it rather than storing a record no clamp can ever reach.
    static bool is_key(int64_t key) { return key > 0; }

    // Replaces the layer memberships for one entity. Naming a layer nobody has
    // configured creates it admitting nobody, so an unconfigured layer is safe
    // rather than transparent.
    void set_membership(int64_t key, const godot::Array &layer_ids);

    // Replaces the parent key. Zero means no parent, and a link that would
    // close a cycle is refused.
    void set_parent(int64_t key, int64_t parent_key);

    // Replaces the synchronizer intent row, which intersects whatever the
    // layers granted.
    void set_intent(int64_t key, const godot::PackedInt64Array &mask);

    // Makes intent admit every live peer. Not the same as an intent row holding
    // every live bit: this one survives a peer joining.
    void set_intent_all(int64_t key);

    // Replaces the deterministic (depth, route) order key.
    void set_order_key(int64_t key, int depth, int route);

    // Replaces the row of currently live peer bits. Every row is intersected
    // with it last, so a viewer bit for a peer that never joined cannot reach a
    // committed row through any layer or intent.
    void set_live_peers(const godot::PackedInt64Array &bits);

    // Removes an entity and makes its previous committed row hide on the next
    // recompute. Its children are orphaned rather than left pointing at a
    // record that is gone.
    void remove_entity(int64_t key);

    // Computes row changes without advancing the committed matrix.
    godot::Ref<NetwInterestDelta> recompute();

    // Advances the matrix that row readers see.
    //
    // A delta carries the revision it was computed against, and one that a
    // later mutation has overtaken still applies, but leaves the dirty set
    // alone so the mutation it did not see is still owed a pass.
    void commit(const godot::Ref<NetwInterestDelta> &delta);

    godot::PackedInt64Array row_of(int64_t key) const;

    // The row one entity will have once `delta` commits, answered while the old
    // matrix still answers every other question.
    godot::PackedInt64Array row_after(
        int64_t key,
        const godot::Ref<NetwInterestDelta> &delta
    ) const;

    bool test(int64_t key, int bit) const;
    godot::Dictionary rows() const;
    bool has_entity(int64_t key) const;

    // Committed admitted edges attributed to one layer, counted from the
    // committed membership snapshot rather than from live intake.
    int layer_edge_count(const godot::StringName &layer_id) const;

    // Names the first current term that denies one peer bit, walking intent and
    // layers before climbing to the parent. It is derived from the state the
    // verdict used, so it cannot disagree with the row.
    godot::String explain(int64_t key, int bit) const;

    godot::Ref<NetwInterestStats> stats() const {
        return last_stats;
    }

    // Session teardown: intake, the committed matrix and the counters.
    void clear();
};

} // namespace netw

VARIANT_ENUM_CAST(netw::NetwInterestEngine::Policy);
