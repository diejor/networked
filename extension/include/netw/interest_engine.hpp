#pragma once

#include <cstdint>

#include "godot/local_vector.hpp"
#include "godot/ref_counted.hpp"
#include "godot/rid.hpp"
#include "godot/variant.hpp"
#include "godot/callable.hpp"
#include "netw/interest_decl.hpp"

namespace netw {

class InterestBitSet {
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

struct InterestStats {
    int32_t layers_recomputed = 0;
    int32_t entities_recomputed = 0;
    int32_t edges = 0;
    int32_t shows = 0;
    int32_t hides = 0;
    int32_t words_per_row = 0;
    int32_t vanished_dirty_skips = 0;

    godot::PackedInt32Array to_array() const;
};

class InterestDelta {
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
    InterestStats stats;

    godot::HashMap<godot::StringName, godot::PackedInt64Array> layer_rows;
    godot::HashMap<int64_t, godot::LocalVector<godot::StringName>> memberships;
    godot::HashSet<int64_t> intents;
    int64_t commit_revision = 0;

    InterestDelta();

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
    godot::Array to_array() const;
};

class InterestEngine {
public:
    enum Policy {
        HIDE_FROM_OUTSIDERS = 0,
        HIDE_FROM_INSIDERS = 1,
    };

private:
    struct Layer {
        godot::PackedInt64Array viewers;
        Policy policy = HIDE_FROM_OUTSIDERS;
        int32_t leave_policy = 0;
        int32_t perception_policy = 0;
        godot::LocalVector<int64_t> members;
        int64_t transitions = 0;
    };

    struct Record {
        godot::LocalVector<godot::StringName> layers;
        int64_t parent = 0;
        godot::PackedInt64Array intent;
        bool intent_all = true;
        int32_t depth = 0;
        int32_t route = 0;
    };

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
    godot::HashSet<int64_t> committed_intents;
    godot::HashSet<godot::StringName> dirty_layers;
    godot::HashSet<int64_t> dirty_entities;
    godot::HashMap<int64_t, Order> removed;
    godot::HashMap<int64_t, int32_t> peer_bits;
    godot::LocalVector<int64_t> bit_peers;
    godot::HashMap<int64_t, godot::Callable> exit_handlers;
    godot::HashMap<int64_t, godot::StringName> scene_memberships;
    godot::HashMap<int64_t, int32_t> fallback_routes;
    int32_t next_fallback_route = 1;
    godot::PackedInt64Array live_peers;
    InterestStats last_stats;
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
        InterestStats &out_stats
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

public:
    InterestEngine();

    void set_layer(
        const godot::StringName &id,
        const godot::PackedInt64Array &viewers,
        int policy
    );

    void remove_layer(const godot::StringName &id);

    void declare_layer(const godot::StringName &id);

    bool has_layer(const godot::StringName &id) const;
    int layer_count() const { return layers.size(); }

    bool layer_add_viewer(const godot::StringName &id, int64_t peer_id);
    bool layer_remove_viewer(const godot::StringName &id, int64_t peer_id);
    bool layer_has_viewer(const godot::StringName &id, int64_t peer_id) const;
    bool layer_set_policy(const godot::StringName &id, int policy);
    int layer_policy(const godot::StringName &id) const;

    bool layer_admits(const godot::StringName &id, int64_t peer_id) const;
    godot::String layer_explain(const godot::StringName &id, int64_t peer_id)
        const;

    bool layer_set_leave_policy(const godot::StringName &id, int policy);
    int layer_leave_policy(const godot::StringName &id) const;
    bool layer_set_perception_policy(const godot::StringName &id, int policy);
    int layer_perception_policy(const godot::StringName &id) const;

    godot::PackedInt64Array layer_viewers(const godot::StringName &id) const;

    bool roster_add(const godot::StringName &id, int64_t key);
    bool roster_remove(const godot::StringName &id, int64_t key);
    bool roster_has(const godot::StringName &id, int64_t key) const;
    godot::PackedInt64Array roster(const godot::StringName &id) const;

    bool projection_admits(
        int64_t key,
        const godot::Ref<NetwInterestDecl> &decl
    ) const;

    void note_transition(const godot::StringName &id);
    int64_t transitions(const godot::StringName &id) const;

    int64_t transitions_total() const;

    godot::PackedInt64Array co_members(
        int64_t key,
        const godot::Array &layer_ids
    ) const;

    godot::PackedInt64Array viewer_peers() const;

    static bool is_key(int64_t key) { return key > 0; }

    void set_membership(int64_t key, const godot::Array &layer_ids);

    bool membership_add(int64_t key, const godot::StringName &layer_id);
    bool membership_remove(int64_t key, const godot::StringName &layer_id);

    godot::Array memberships(int64_t key) const;

    bool has_memberships(int64_t key) const;

    bool has_intent(int64_t key) const;

    bool had_committed_intent(int64_t key) const;

    godot::PackedInt64Array intent_keys() const;

    bool set_exit_handler(int64_t key, const godot::Callable &handler);
    godot::Callable exit_handler(int64_t key) const;
    godot::Callable take_exit_handler(int64_t key);

    godot::StringName scene_membership(int64_t key) const;

    bool set_scene_membership(int64_t key, const godot::StringName &id);

    godot::PackedInt64Array membership_keys() const;

    void set_parent(int64_t key, int64_t parent_key);

    void set_intent(int64_t key, const godot::PackedInt64Array &mask);

    void set_intent_for_peers(
        int64_t key,
        const godot::PackedInt64Array &peer_ids
    );

    void set_intent_all(int64_t key);

    void set_order_key(int64_t key, int depth, int route);

    int order_route_for(int64_t key);

    void set_live_peers(const godot::PackedInt64Array &bits);

    bool set_live_peer_ids(const godot::PackedInt64Array &peer_ids);

    int peer_bit_for(int64_t peer_id);

    int peer_bit_of(int64_t peer_id) const;

    int64_t peer_of_bit(int bit) const;

    godot::PackedInt64Array known_peers() const;

    void remove_entity(int64_t key);

    InterestDelta recompute();

    void commit(const InterestDelta &delta);

    godot::PackedInt64Array row_of(int64_t key) const;

    godot::PackedInt64Array admitted_peers(int64_t key) const;

    godot::PackedInt64Array row_after(
        int64_t key,
        const InterestDelta &delta
    ) const;

    bool test(int64_t key, int bit) const;

    int dirty_count() const;
    godot::Dictionary rows() const;
    bool has_entity(int64_t key) const;

    int layer_edge_count(const godot::StringName &layer_id) const;

    godot::String explain(int64_t key, int bit) const;

    int stats_edges() const {
        return last_stats.edges;
    }

    int stats_vanished_dirty_skips() const {
        return last_stats.vanished_dirty_skips;
    }

    InterestStats stats() const {
        return last_stats;
    }

    void clear();
};

} // namespace netw

