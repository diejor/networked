#include "netw/interest_engine.hpp"

#include "godot/class_db.hpp"
#include "godot/utility.hpp"
#include "netw/colors.hpp"
#include "netw/log.hpp"
#include "netw/profile.hpp"

using namespace godot;

namespace netw {

namespace {

// A stable insertion sort over indexing and size alone. Stability is the
// contract, not an implementation detail: it is what makes the bit order inside
// one entity's transitions reproducible.
template <typename T, typename Less>
void insertion_sort(LocalVector<T> &values, Less less) {
    for (uint32_t index = 1; index < values.size(); ++index) {
        T pending = values[index];
        uint32_t slot = index;
        while (slot > 0 && less(pending, values[slot - 1])) {
            values[slot] = values[slot - 1];
            --slot;
        }
        values[slot] = pending;
    }
}

bool contains(const LocalVector<StringName> &names, const StringName &name) {
    for (uint32_t index = 0; index < names.size(); ++index) {
        if (names[index] == name) {
            return true;
        }
    }
    return false;
}

bool same_names(
    const LocalVector<StringName> &left,
    const LocalVector<StringName> &right
) {
    if (left.size() != right.size()) {
        return false;
    }
    for (uint32_t index = 0; index < left.size(); ++index) {
        if (left[index] != right[index]) {
            return false;
        }
    }
    return true;
}

bool contains(const PackedInt64Array &values, int64_t value) {
    for (int index = 0; index < values.size(); ++index) {
        if (values[index] == value) {
            return true;
        }
    }
    return false;
}

Array pair(int64_t key, int bit) {
    Array out;
    out.push_back(key);
    out.push_back(bit);
    return out;
}

} // namespace

int NetwInterestBitSet::word_count(int bit_count) {
    if (bit_count <= 0) {
        return 0;
    }
    return (bit_count + BITS_PER_WORD - 1) / BITS_PER_WORD;
}

PackedInt64Array NetwInterestBitSet::empty(int bit_count) {
    // Resizing is not enough. A packed array of a trivially constructible type
    // does not value-initialise the words it grows by in every build, so an
    // unwritten word is whatever the allocator last left there. Every row that
    // is not fully overwritten afterwards has to be zeroed here.
    PackedInt64Array out;
    const int words = word_count(bit_count);
    out.resize(words);
    for (int index = 0; index < words; ++index) {
        out.set(index, 0);
    }
    return out;
}

PackedInt64Array NetwInterestBitSet::full(int bit_count) {
    PackedInt64Array out = empty(bit_count);
    for (int index = 0; index < out.size(); ++index) {
        out.set(index, WORD_MASK);
    }
    const int remainder = bit_count % BITS_PER_WORD;
    if (remainder != 0 && !out.is_empty()) {
        out.set(out.size() - 1, (int64_t(1) << remainder) - 1);
    }
    return out;
}

PackedInt64Array NetwInterestBitSet::resized(
    const PackedInt64Array &row,
    int words
) {
    const int target = words > 0 ? words : 0;
    PackedInt64Array out = row;
    out.resize(target);
    for (int index = row.size(); index < target; ++index) {
        out.set(index, 0);
    }
    return out;
}

PackedInt64Array NetwInterestBitSet::with_bit(
    const PackedInt64Array &row,
    int bit,
    bool value
) {
    NETW_ERR_COND_V(bit < 0, row, "interest", "Peer bit must be non-negative.");
    const int word = bit / BITS_PER_WORD;
    PackedInt64Array out
        = resized(row, row.size() > word + 1 ? row.size() : word + 1);
    const int64_t mask = int64_t(1) << (bit % BITS_PER_WORD);
    if (value) {
        out.set(word, out[word] | mask);
    } else {
        out.set(word, out[word] & (WORD_MASK ^ mask));
    }
    return out;
}

bool NetwInterestBitSet::test(const PackedInt64Array &row, int bit) {
    if (bit < 0) {
        return false;
    }
    const int word = bit / BITS_PER_WORD;
    if (word >= row.size()) {
        return false;
    }
    return (row[word] & (int64_t(1) << (bit % BITS_PER_WORD))) != 0;
}

namespace {

int64_t word_at(const PackedInt64Array &row, int index) {
    return index < row.size() ? row[index] : 0;
}

int widest(const PackedInt64Array &left, const PackedInt64Array &right) {
    return left.size() > right.size() ? left.size() : right.size();
}

} // namespace

// The four set operations below write every word they resize to, so none of
// them needs the zero-fill `empty` and `resized` carry.
PackedInt64Array NetwInterestBitSet::union_of(
    const PackedInt64Array &left,
    const PackedInt64Array &right
) {
    PackedInt64Array out;
    out.resize(widest(left, right));
    for (int index = 0; index < out.size(); ++index) {
        out.set(index, word_at(left, index) | word_at(right, index));
    }
    return out;
}

PackedInt64Array NetwInterestBitSet::intersect(
    const PackedInt64Array &left,
    const PackedInt64Array &right
) {
    PackedInt64Array out;
    out.resize(widest(left, right));
    for (int index = 0; index < out.size(); ++index) {
        out.set(index, word_at(left, index) & word_at(right, index));
    }
    return out;
}

PackedInt64Array NetwInterestBitSet::subtract(
    const PackedInt64Array &left,
    const PackedInt64Array &right
) {
    PackedInt64Array out;
    out.resize(widest(left, right));
    for (int index = 0; index < out.size(); ++index) {
        out.set(
            index,
            word_at(left, index) & (WORD_MASK ^ word_at(right, index))
        );
    }
    return out;
}

PackedInt64Array NetwInterestBitSet::symmetric_difference(
    const PackedInt64Array &left,
    const PackedInt64Array &right
) {
    PackedInt64Array out;
    out.resize(widest(left, right));
    for (int index = 0; index < out.size(); ++index) {
        out.set(index, word_at(left, index) ^ word_at(right, index));
    }
    return out;
}

bool NetwInterestBitSet::equals(
    const PackedInt64Array &left,
    const PackedInt64Array &right
) {
    const int words = widest(left, right);
    for (int index = 0; index < words; ++index) {
        if (word_at(left, index) != word_at(right, index)) {
            return false;
        }
    }
    return true;
}

PackedInt32Array NetwInterestBitSet::bits(const PackedInt64Array &row) {
    PackedInt32Array out;
    for (int word_index = 0; word_index < row.size(); ++word_index) {
        const int64_t word = row[word_index];
        for (int offset = 0; offset < BITS_PER_WORD; ++offset) {
            if ((word & (int64_t(1) << offset)) != 0) {
                out.push_back(word_index * BITS_PER_WORD + offset);
            }
        }
    }
    return out;
}

int NetwInterestBitSet::popcount(const PackedInt64Array &row) {
    int out = 0;
    for (int index = 0; index < row.size(); ++index) {
        int64_t remaining = row[index];
        while (remaining != 0) {
            remaining &= remaining - 1;
            ++out;
        }
    }
    return out;
}

void NetwInterestBitSet::_bind_methods() {
    ClassDB::bind_static_method(
        "NetwInterestBitSet",
        D_METHOD("word_count", "bit_count"),
        &NetwInterestBitSet::word_count
    );
    ClassDB::bind_static_method(
        "NetwInterestBitSet",
        D_METHOD("empty", "bit_count"),
        &NetwInterestBitSet::empty
    );
    ClassDB::bind_static_method(
        "NetwInterestBitSet",
        D_METHOD("full", "bit_count"),
        &NetwInterestBitSet::full
    );
    ClassDB::bind_static_method(
        "NetwInterestBitSet",
        D_METHOD("resized", "row", "words"),
        &NetwInterestBitSet::resized
    );
    ClassDB::bind_static_method(
        "NetwInterestBitSet",
        D_METHOD("with_bit", "row", "bit", "value"),
        &NetwInterestBitSet::with_bit,
        DEFVAL(true)
    );
    ClassDB::bind_static_method(
        "NetwInterestBitSet",
        D_METHOD("test", "row", "bit"),
        &NetwInterestBitSet::test
    );
    ClassDB::bind_static_method(
        "NetwInterestBitSet",
        D_METHOD("union_of", "left", "right"),
        &NetwInterestBitSet::union_of
    );
    ClassDB::bind_static_method(
        "NetwInterestBitSet",
        D_METHOD("intersect", "left", "right"),
        &NetwInterestBitSet::intersect
    );
    ClassDB::bind_static_method(
        "NetwInterestBitSet",
        D_METHOD("subtract", "left", "right"),
        &NetwInterestBitSet::subtract
    );
    ClassDB::bind_static_method(
        "NetwInterestBitSet",
        D_METHOD("symmetric_difference", "left", "right"),
        &NetwInterestBitSet::symmetric_difference
    );
    ClassDB::bind_static_method(
        "NetwInterestBitSet",
        D_METHOD("equals", "left", "right"),
        &NetwInterestBitSet::equals
    );
    ClassDB::bind_static_method(
        "NetwInterestBitSet",
        D_METHOD("bits", "row"),
        &NetwInterestBitSet::bits
    );
    ClassDB::bind_static_method(
        "NetwInterestBitSet",
        D_METHOD("popcount", "row"),
        &NetwInterestBitSet::popcount
    );

    ClassDB::bind_integer_constant(
        "NetwInterestBitSet",
        StringName(),
        "BITS_PER_WORD",
        BITS_PER_WORD
    );
    ClassDB::bind_integer_constant(
        "NetwInterestBitSet",
        StringName(),
        "WORD_MASK",
        WORD_MASK
    );
}

PackedInt32Array NetwInterestStats::to_array() const {
    PackedInt32Array out;
    out.push_back(layers_recomputed);
    out.push_back(entities_recomputed);
    out.push_back(edges);
    out.push_back(shows);
    out.push_back(hides);
    out.push_back(words_per_row);
    out.push_back(vanished_dirty_skips);
    return out;
}

void NetwInterestStats::_bind_methods() {
    ClassDB::bind_method(
        D_METHOD("get_layers_recomputed"),
        &NetwInterestStats::get_layers_recomputed
    );
    ClassDB::bind_method(
        D_METHOD("get_entities_recomputed"),
        &NetwInterestStats::get_entities_recomputed
    );
    ClassDB::bind_method(D_METHOD("get_edges"), &NetwInterestStats::get_edges);
    ClassDB::bind_method(D_METHOD("get_shows"), &NetwInterestStats::get_shows);
    ClassDB::bind_method(D_METHOD("get_hides"), &NetwInterestStats::get_hides);
    ClassDB::bind_method(
        D_METHOD("get_words_per_row"),
        &NetwInterestStats::get_words_per_row
    );
    ClassDB::bind_method(
        D_METHOD("get_vanished_dirty_skips"),
        &NetwInterestStats::get_vanished_dirty_skips
    );
    ClassDB::bind_method(D_METHOD("to_array"), &NetwInterestStats::to_array);

    ADD_PROPERTY(
        PropertyInfo(Variant::INT, "layers_recomputed"),
        "",
        "get_layers_recomputed"
    );
    ADD_PROPERTY(
        PropertyInfo(Variant::INT, "entities_recomputed"),
        "",
        "get_entities_recomputed"
    );
    ADD_PROPERTY(PropertyInfo(Variant::INT, "edges"), "", "get_edges");
    ADD_PROPERTY(PropertyInfo(Variant::INT, "shows"), "", "get_shows");
    ADD_PROPERTY(PropertyInfo(Variant::INT, "hides"), "", "get_hides");
    ADD_PROPERTY(
        PropertyInfo(Variant::INT, "words_per_row"),
        "",
        "get_words_per_row"
    );
    ADD_PROPERTY(
        PropertyInfo(Variant::INT, "vanished_dirty_skips"),
        "",
        "get_vanished_dirty_skips"
    );
}

NetwInterestDelta::NetwInterestDelta() {
    stats.instantiate();
}

Array NetwInterestDelta::to_array() const {
    Array out;
    out.push_back(keys);
    out.push_back(old_rows);
    out.push_back(new_rows);
    out.push_back(order_keys);
    out.push_back(shows);
    out.push_back(hides);
    out.push_back(layer_shows);
    out.push_back(layer_hides);
    out.push_back(stats.is_valid() ? stats->to_array() : PackedInt32Array());
    return out;
}

void NetwInterestDelta::_bind_methods() {
    ClassDB::bind_method(D_METHOD("is_empty"), &NetwInterestDelta::is_empty);
    ClassDB::bind_method(D_METHOD("to_array"), &NetwInterestDelta::to_array);
    ClassDB::bind_method(D_METHOD("get_keys"), &NetwInterestDelta::get_keys);
    ClassDB::bind_method(
        D_METHOD("get_old_rows"),
        &NetwInterestDelta::get_old_rows
    );
    ClassDB::bind_method(
        D_METHOD("get_new_rows"),
        &NetwInterestDelta::get_new_rows
    );
    ClassDB::bind_method(
        D_METHOD("get_order_keys"),
        &NetwInterestDelta::get_order_keys
    );
    ClassDB::bind_method(D_METHOD("get_shows"), &NetwInterestDelta::get_shows);
    ClassDB::bind_method(D_METHOD("get_hides"), &NetwInterestDelta::get_hides);
    ClassDB::bind_method(
        D_METHOD("get_layer_shows"),
        &NetwInterestDelta::get_layer_shows
    );
    ClassDB::bind_method(
        D_METHOD("get_layer_hides"),
        &NetwInterestDelta::get_layer_hides
    );
    ClassDB::bind_method(
        D_METHOD("get_removed_keys"),
        &NetwInterestDelta::get_removed_keys
    );
    ClassDB::bind_method(D_METHOD("get_stats"), &NetwInterestDelta::get_stats);

    ADD_PROPERTY(
        PropertyInfo(Variant::PACKED_INT64_ARRAY, "keys"),
        "",
        "get_keys"
    );
    ADD_PROPERTY(PropertyInfo(Variant::ARRAY, "old_rows"), "", "get_old_rows");
    ADD_PROPERTY(PropertyInfo(Variant::ARRAY, "new_rows"), "", "get_new_rows");
    ADD_PROPERTY(
        PropertyInfo(Variant::ARRAY, "order_keys"),
        "",
        "get_order_keys"
    );
    ADD_PROPERTY(PropertyInfo(Variant::ARRAY, "shows"), "", "get_shows");
    ADD_PROPERTY(PropertyInfo(Variant::ARRAY, "hides"), "", "get_hides");
    ADD_PROPERTY(
        PropertyInfo(Variant::ARRAY, "layer_shows"),
        "",
        "get_layer_shows"
    );
    ADD_PROPERTY(
        PropertyInfo(Variant::ARRAY, "layer_hides"),
        "",
        "get_layer_hides"
    );
    ADD_PROPERTY(
        PropertyInfo(Variant::PACKED_INT64_ARRAY, "removed_keys"),
        "",
        "get_removed_keys"
    );
    ADD_PROPERTY(
        PropertyInfo(
            Variant::OBJECT,
            "stats",
            PROPERTY_HINT_RESOURCE_TYPE,
            "NetwInterestStats"
        ),
        "",
        "get_stats"
    );
}

bool NetwInterestEngine::Order::precedes(
    const Order &other,
    bool deeper_first
) const {
    if (depth != other.depth) {
        return deeper_first ? depth > other.depth : depth < other.depth;
    }
    return route < other.route;
}

NetwInterestEngine::NetwInterestEngine() {
    last_stats.instantiate();
}

NetwInterestEngine::Record &NetwInterestEngine::record_for(int64_t key) {
    removed.erase(key);
    HashMap<int64_t, Record>::Iterator found = entities.find(key);
    if (found != entities.end()) {
        return found->value;
    }
    dirty_entities.insert(key);
    return entities.insert(key, Record())->value;
}

void NetwInterestEngine::ensure_layer(const StringName &id) {
    if (layers.has(id)) {
        return;
    }
    layers.insert(id, Layer());
    dirty_layers.insert(id);
}

void NetwInterestEngine::mark_layer_members_dirty(const StringName &id) {
    LocalVector<int64_t> members;
    for (const KeyValue<int64_t, Record> &entry : entities) {
        if (contains(entry.value.layers, id)) {
            members.push_back(entry.key);
        }
    }
    for (uint32_t index = 0; index < members.size(); ++index) {
        mark_entity_tree_dirty(members[index]);
    }
}

void NetwInterestEngine::mark_entity_tree_dirty(int64_t root) {
    dirty_entities.insert(root);
    bool changed = true;
    while (changed) {
        changed = false;
        for (const KeyValue<int64_t, Record> &entry : entities) {
            if (dirty_entities.has(entry.key)) {
                continue;
            }
            if (entry.value.parent != 0
                && dirty_entities.has(entry.value.parent)) {
                dirty_entities.insert(entry.key);
                changed = true;
            }
        }
    }
}

bool NetwInterestEngine::layer_admits_bit(const StringName &id, int bit) const {
    const HashMap<StringName, Layer>::ConstIterator found = layers.find(id);
    if (found == layers.end()) {
        return false;
    }
    const bool viewer = NetwInterestBitSet::test(found->value.viewers, bit);
    return found->value.policy == HIDE_FROM_OUTSIDERS ? viewer : !viewer;
}

NetwInterestEngine::Order NetwInterestEngine::order_of(int64_t key) const {
    const HashMap<int64_t, Record>::ConstIterator found = entities.find(key);
    if (found == entities.end()) {
        return Order();
    }
    Order out;
    out.depth = found->value.depth;
    out.route = found->value.route;
    return out;
}

void NetwInterestEngine::set_layer(
    const StringName &id,
    const PackedInt64Array &viewers,
    int policy
) {
    NETW_ERR_COND(id.is_empty(), "interest", "Interest layer id is empty.");
    NETW_ERR_COND(
        policy != HIDE_FROM_OUTSIDERS && policy != HIDE_FROM_INSIDERS,
        "interest",
        "Unknown interest policy %d.",
        policy
    );
    const HashMap<StringName, Layer>::Iterator found = layers.find(id);
    if (found != layers.end() && found->value.policy == Policy(policy)
        && NetwInterestBitSet::equals(found->value.viewers, viewers)) {
        return;
    }
    Layer fresh;
    fresh.viewers = viewers;
    fresh.policy = Policy(policy);
    layers[id] = fresh;
    dirty_layers.insert(id);
    mark_layer_members_dirty(id);
    ++revision;
}

void NetwInterestEngine::remove_layer(const StringName &id) {
    if (!layers.has(id)) {
        return;
    }
    layers.erase(id);
    dirty_layers.insert(id);
    mark_layer_members_dirty(id);
    ++revision;
}

void NetwInterestEngine::set_membership(int64_t key, const Array &layer_ids) {
    NETW_ERR_COND(
        !is_key(key),
        "interest",
        "Interest entity key must be positive."
    );
    LocalVector<StringName> normalized;
    for (int index = 0; index < layer_ids.size(); ++index) {
        const StringName id = layer_ids[index];
        if (id.is_empty() || contains(normalized, id)) {
            continue;
        }
        normalized.push_back(id);
        ensure_layer(id);
    }
    Record &record = record_for(key);
    if (same_names(record.layers, normalized)) {
        return;
    }
    record.layers = normalized;
    mark_entity_tree_dirty(key);
    ++revision;
}

void NetwInterestEngine::set_parent(int64_t key, int64_t parent_key) {
    NETW_ERR_COND(
        !is_key(key),
        "interest",
        "Interest entity key must be positive."
    );
    NETW_ERR_COND(
        key == parent_key,
        "interest",
        "An entity cannot be its own parent."
    );
    int64_t walk = parent_key;
    while (walk != 0) {
        const HashMap<int64_t, Record>::ConstIterator found
            = entities.find(walk);
        if (found == entities.end()) {
            break;
        }
        NETW_ERR_COND(
            walk == key,
            "interest",
            "Interest parent link closes a cycle."
        );
        walk = found->value.parent;
    }
    Record &record = record_for(key);
    if (record.parent == parent_key) {
        return;
    }
    record.parent = parent_key;
    mark_entity_tree_dirty(key);
    ++revision;
}

void NetwInterestEngine::set_intent(int64_t key, const PackedInt64Array &mask) {
    NETW_ERR_COND(
        !is_key(key),
        "interest",
        "Interest entity key must be positive."
    );
    Record &record = record_for(key);
    if (!record.intent_all && NetwInterestBitSet::equals(record.intent, mask)) {
        return;
    }
    record.intent_all = false;
    record.intent = mask;
    mark_entity_tree_dirty(key);
    ++revision;
}

void NetwInterestEngine::set_intent_all(int64_t key) {
    NETW_ERR_COND(
        !is_key(key),
        "interest",
        "Interest entity key must be positive."
    );
    Record &record = record_for(key);
    if (record.intent_all) {
        return;
    }
    record.intent_all = true;
    record.intent = PackedInt64Array();
    mark_entity_tree_dirty(key);
    ++revision;
}

void NetwInterestEngine::set_order_key(int64_t key, int depth, int route) {
    NETW_ERR_COND(
        !is_key(key),
        "interest",
        "Interest entity key must be positive."
    );
    NETW_ERR_COND(depth < 0, "interest", "Interest order depth is negative.");
    NETW_ERR_COND(route < 0, "interest", "Interest order route is negative.");
    Record &record = record_for(key);
    if (record.depth == depth && record.route == route) {
        return;
    }
    record.depth = int32_t(depth);
    record.route = int32_t(route);
    dirty_entities.insert(key);
    ++revision;
}

void NetwInterestEngine::set_live_peers(const PackedInt64Array &bits) {
    if (NetwInterestBitSet::equals(live_peers, bits)) {
        return;
    }
    live_peers = bits;
    for (const KeyValue<StringName, Layer> &entry : layers) {
        dirty_layers.insert(entry.key);
    }
    for (const KeyValue<int64_t, Record> &entry : entities) {
        dirty_entities.insert(entry.key);
    }
    ++revision;
}

void NetwInterestEngine::remove_entity(int64_t key) {
    NETW_ERR_COND(
        !is_key(key),
        "interest",
        "Interest entity key must be positive."
    );
    if (!entities.has(key) && !committed.has(key)) {
        return;
    }
    // The order a removal is answered at is stored with the removal itself,
    // so a key that never reached intake still has one. Keeping the marker and
    // the order in separate tables lets two such removals be compared against
    // an order nobody wrote.
    removed.insert(key, order_of(key));
    entities.erase(key);
    dirty_entities.erase(key);
    LocalVector<int64_t> orphans;
    for (KeyValue<int64_t, Record> &entry : entities) {
        if (entry.value.parent == key) {
            entry.value.parent = 0;
            orphans.push_back(entry.key);
        }
    }
    for (uint32_t index = 0; index < orphans.size(); ++index) {
        mark_entity_tree_dirty(orphans[index]);
    }
    ++revision;
}

void NetwInterestEngine::recompute_layers(
    HashMap<StringName, PackedInt64Array> &rows_by_layer,
    const Ref<NetwInterestStats> &out_stats
) const {
    // Zoned apart from the entity fold rather than inside it. Which of the two
    // dominates is the question that decides whether either is worth splitting,
    // and one zone over both cannot answer it.
    NETW_ZONE_NC("NetwInterestEngine layer fold", colors::INTEREST);
    NETW_ZONE_VALUE(dirty_layers.size());
    LocalVector<StringName> ids;
    for (const StringName &id : dirty_layers) {
        ids.push_back(id);
    }
    insertion_sort(ids, [](const StringName &left, const StringName &right) {
        return String(left) < String(right);
    });
    for (uint32_t index = 0; index < ids.size(); ++index) {
        const StringName &id = ids[index];
        const HashMap<StringName, Layer>::ConstIterator found = layers.find(id);
        if (found == layers.end()) {
            rows_by_layer.erase(id);
            continue;
        }
        rows_by_layer[id] = found->value.policy == HIDE_FROM_OUTSIDERS
            ? NetwInterestBitSet::intersect(found->value.viewers, live_peers)
            : NetwInterestBitSet::subtract(live_peers, found->value.viewers);
        ++out_stats->layers_recomputed;
    }
}

PackedInt64Array NetwInterestEngine::compute_entity_row(
    int64_t key,
    const HashMap<StringName, PackedInt64Array> &rows_by_layer,
    const HashMap<int64_t, PackedInt64Array> &desired_rows
) const {
    const Record &record = entities.find(key)->value;
    PackedInt64Array grant;
    if (record.layers.is_empty()) {
        grant = live_peers;
    } else {
        grant = NetwInterestBitSet::empty(
            live_peers.size() * NetwInterestBitSet::BITS_PER_WORD
        );
        for (uint32_t index = 0; index < record.layers.size(); ++index) {
            const HashMap<StringName, PackedInt64Array>::ConstIterator admit
                = rows_by_layer.find(record.layers[index]);
            grant = NetwInterestBitSet::union_of(
                grant,
                admit != rows_by_layer.end() ? admit->value : PackedInt64Array()
            );
        }
    }
    if (!record.intent_all) {
        grant = NetwInterestBitSet::intersect(grant, record.intent);
    }
    if (record.parent != 0) {
        const HashMap<int64_t, PackedInt64Array>::ConstIterator pending
            = desired_rows.find(record.parent);
        grant = NetwInterestBitSet::intersect(
            grant,
            pending != desired_rows.end() ? pending->value
                                          : row_of(record.parent)
        );
    }
    return NetwInterestBitSet::intersect(grant, live_peers);
}

void NetwInterestEngine::append_transition_bits(
    int64_t key,
    const PackedInt64Array &old_row,
    const PackedInt64Array &desired,
    const Order &order,
    LocalVector<Transition> &shows,
    LocalVector<Transition> &hides
) const {
    const PackedInt32Array gained = NetwInterestBitSet::bits(
        NetwInterestBitSet::subtract(desired, old_row)
    );
    for (int index = 0; index < gained.size(); ++index) {
        Transition transition;
        transition.key = key;
        transition.bit = gained[index];
        transition.order = order;
        shows.push_back(transition);
    }
    const PackedInt32Array lost = NetwInterestBitSet::bits(
        NetwInterestBitSet::subtract(old_row, desired)
    );
    for (int index = 0; index < lost.size(); ++index) {
        Transition transition;
        transition.key = key;
        transition.bit = lost[index];
        transition.order = order;
        hides.push_back(transition);
    }
}

void NetwInterestEngine::append_layer_transition_bits(
    const StringName &id,
    int64_t key,
    const PackedInt64Array &old_row,
    const PackedInt64Array &desired,
    const Order &order,
    LocalVector<LayerTransition> &shows,
    LocalVector<LayerTransition> &hides
) const {
    const PackedInt32Array gained = NetwInterestBitSet::bits(
        NetwInterestBitSet::subtract(desired, old_row)
    );
    for (int index = 0; index < gained.size(); ++index) {
        LayerTransition transition;
        transition.layer = id;
        transition.key = key;
        transition.bit = gained[index];
        transition.order = order;
        shows.push_back(transition);
    }
    const PackedInt32Array lost = NetwInterestBitSet::bits(
        NetwInterestBitSet::subtract(old_row, desired)
    );
    for (int index = 0; index < lost.size(); ++index) {
        LayerTransition transition;
        transition.layer = id;
        transition.key = key;
        transition.bit = lost[index];
        transition.order = order;
        hides.push_back(transition);
    }
}

LocalVector<int64_t> NetwInterestEngine::ordered_dirty_keys() const {
    LocalVector<int64_t> out;
    for (const int64_t key : dirty_entities) {
        out.push_back(key);
    }
    insertion_sort(out, [this](int64_t left, int64_t right) {
        const Order left_order = order_of(left);
        const Order right_order = order_of(right);
        if (left_order == right_order) {
            return left < right;
        }
        return left_order.precedes(right_order, false);
    });
    return out;
}

LocalVector<int64_t> NetwInterestEngine::ordered_removed_keys() const {
    LocalVector<int64_t> out;
    for (const KeyValue<int64_t, Order> &entry : removed) {
        out.push_back(entry.key);
    }
    insertion_sort(out, [this](int64_t left, int64_t right) {
        const Order left_order = removed.find(left)->value;
        const Order right_order = removed.find(right)->value;
        if (left_order == right_order) {
            return left < right;
        }
        return left_order.precedes(right_order, false);
    });
    return out;
}

int NetwInterestEngine::edge_count_after(
    const HashMap<int64_t, PackedInt64Array> &changed,
    const PackedInt64Array &removed_keys
) const {
    HashSet<int64_t> seen;
    int out = 0;
    for (const KeyValue<int64_t, PackedInt64Array> &entry : committed) {
        if (contains(removed_keys, entry.key)) {
            continue;
        }
        const HashMap<int64_t, PackedInt64Array>::ConstIterator moved
            = changed.find(entry.key);
        out += NetwInterestBitSet::popcount(
            moved != changed.end() ? moved->value : entry.value
        );
        seen.insert(entry.key);
    }
    for (const KeyValue<int64_t, PackedInt64Array> &entry : changed) {
        if (seen.has(entry.key) || contains(removed_keys, entry.key)) {
            continue;
        }
        out += NetwInterestBitSet::popcount(entry.value);
    }
    return out;
}

Ref<NetwInterestDelta> NetwInterestEngine::recompute() {
    NETW_ZONE_NC("NetwInterestEngine recompute", colors::INTEREST);
    NETW_ZONE_VALUE(dirty_entities.size());
    Ref<NetwInterestDelta> delta;
    delta.instantiate();
    delta->commit_revision = revision;

    // Direct-initialised, never copy-initialised: the engine's HashMap copy
    // constructor is explicit and godot-cpp's is not, so copy-initialisation
    // falls through to a move that cannot bind a const lvalue and only one of
    // the two builds rejects it.
    HashMap<StringName, PackedInt64Array> rows_by_layer(layer_rows);
    recompute_layers(rows_by_layer, delta->stats);

    HashMap<int64_t, PackedInt64Array> desired_rows;
    HashMap<int64_t, PackedInt64Array> changed_rows;
    LocalVector<Transition> shows;
    LocalVector<Transition> hides;
    LocalVector<LayerTransition> layer_show_rows;
    LocalVector<LayerTransition> layer_hide_rows;

    {
        NETW_ZONE_NC("NetwInterestEngine entity fold", colors::INTEREST);
        const LocalVector<int64_t> ordered = ordered_dirty_keys();
        for (uint32_t index = 0; index < ordered.size(); ++index) {
            const int64_t key = ordered[index];
            if (!entities.has(key)) {
                ++delta->stats->vanished_dirty_skips;
                continue;
            }
            const PackedInt64Array desired
                = compute_entity_row(key, rows_by_layer, desired_rows);
            desired_rows[key] = desired;
            ++delta->stats->entities_recomputed;

            const PackedInt64Array old_row = row_of(key);
            changed_rows[key] = desired;
            const Order order = order_of(key);
            if (!NetwInterestBitSet::equals(old_row, desired)) {
                delta->keys.push_back(key);
                delta->old_rows.push_back(old_row);
                delta->new_rows.push_back(desired);
                delta->order_keys.push_back(pair(order.depth, order.route));
                append_transition_bits(
                    key,
                    old_row,
                    desired,
                    order,
                    shows,
                    hides
                );
            }

            // Attribution diffs memberships as well as rows, so joining a layer
            // attributes the edges it already admitted and leaving one gives
            // them back, both without the entity's own row moving.
            const Record &record = entities.find(key)->value;
            const HashMap<int64_t, LocalVector<StringName>>::ConstIterator was
                = committed_memberships.find(key);
            LocalVector<StringName> touched;
            if (was != committed_memberships.end()) {
                for (uint32_t at = 0; at < was->value.size(); ++at) {
                    if (!contains(touched, was->value[at])) {
                        touched.push_back(was->value[at]);
                    }
                }
            }
            for (uint32_t at = 0; at < record.layers.size(); ++at) {
                if (!contains(touched, record.layers[at])) {
                    touched.push_back(record.layers[at]);
                }
            }
            for (uint32_t at = 0; at < touched.size(); ++at) {
                const StringName &id = touched[at];
                const bool held = was != committed_memberships.end()
                    && contains(was->value, id);
                const bool holds = contains(record.layers, id);
                PackedInt64Array before;
                if (held) {
                    const HashMap<StringName, PackedInt64Array>::ConstIterator
                        row = layer_rows.find(id);
                    if (row != layer_rows.end()) {
                        before = row->value;
                    }
                }
                PackedInt64Array after;
                if (holds) {
                    const HashMap<StringName, PackedInt64Array>::ConstIterator
                        row = rows_by_layer.find(id);
                    if (row != rows_by_layer.end()) {
                        after = row->value;
                    }
                }
                append_layer_transition_bits(
                    id,
                    key,
                    before,
                    after,
                    order,
                    layer_show_rows,
                    layer_hide_rows
                );
            }
        }
    }

    {
        NETW_ZONE_NC("NetwInterestEngine removal fold", colors::INTEREST);
        const LocalVector<int64_t> gone = ordered_removed_keys();
        for (uint32_t index = 0; index < gone.size(); ++index) {
            const int64_t key = gone[index];
            const PackedInt64Array old_row = row_of(key);
            const PackedInt64Array desired = NetwInterestBitSet::empty(
                live_peers.size() * NetwInterestBitSet::BITS_PER_WORD
            );
            changed_rows[key] = desired;
            const Order order = removed.find(key)->value;
            if (!NetwInterestBitSet::equals(old_row, desired)) {
                delta->keys.push_back(key);
                delta->old_rows.push_back(old_row);
                delta->new_rows.push_back(desired);
                delta->order_keys.push_back(pair(order.depth, order.route));
                append_transition_bits(
                    key,
                    old_row,
                    desired,
                    order,
                    shows,
                    hides
                );
            }
            const HashMap<int64_t, LocalVector<StringName>>::ConstIterator was
                = committed_memberships.find(key);
            if (was == committed_memberships.end()) {
                continue;
            }
            for (uint32_t at = 0; at < was->value.size(); ++at) {
                const StringName &id = was->value[at];
                PackedInt64Array before;
                const HashMap<StringName, PackedInt64Array>::ConstIterator row
                    = layer_rows.find(id);
                if (row != layer_rows.end()) {
                    before = row->value;
                }
                append_layer_transition_bits(
                    id,
                    key,
                    before,
                    PackedInt64Array(),
                    order,
                    layer_show_rows,
                    layer_hide_rows
                );
            }
            delta->removed_keys.push_back(key);
        }
    }

    {
        NETW_ZONE_NC("NetwInterestEngine ordering", colors::INTEREST);
        insertion_sort(
            shows,
            [](const Transition &left, const Transition &right) {
                if (left.order == right.order) {
                    return false;
                }
                return left.order.precedes(right.order, false);
            }
        );
        insertion_sort(
            hides,
            [](const Transition &left, const Transition &right) {
                if (left.order == right.order) {
                    return false;
                }
                return left.order.precedes(right.order, true);
            }
        );
        for (uint32_t index = 0; index < shows.size(); ++index) {
            delta->shows.push_back(pair(shows[index].key, shows[index].bit));
        }
        for (uint32_t index = 0; index < hides.size(); ++index) {
            delta->hides.push_back(pair(hides[index].key, hides[index].bit));
        }

        const auto layer_less = [](bool deeper_first) {
            return [deeper_first](
                       const LayerTransition &left,
                       const LayerTransition &right
                   ) {
                if (!(left.order == right.order)) {
                    return left.order.precedes(right.order, deeper_first);
                }
                return String(left.layer) < String(right.layer);
            };
        };
        insertion_sort(layer_show_rows, layer_less(false));
        insertion_sort(layer_hide_rows, layer_less(true));
        for (uint32_t index = 0; index < layer_show_rows.size(); ++index) {
            Array row;
            row.push_back(layer_show_rows[index].layer);
            row.push_back(layer_show_rows[index].key);
            row.push_back(layer_show_rows[index].bit);
            delta->layer_shows.push_back(row);
        }
        for (uint32_t index = 0; index < layer_hide_rows.size(); ++index) {
            Array row;
            row.push_back(layer_hide_rows[index].layer);
            row.push_back(layer_hide_rows[index].key);
            row.push_back(layer_hide_rows[index].bit);
            delta->layer_hides.push_back(row);
        }
    }

    {
        NETW_ZONE_NC("NetwInterestEngine snapshot", colors::INTEREST);
        delta->layer_rows = rows_by_layer;
        for (const KeyValue<int64_t, Record> &entry : entities) {
            delta->memberships.insert(entry.key, entry.value.layers);
        }
        delta->stats->words_per_row = live_peers.size();
        delta->stats->edges
            = edge_count_after(changed_rows, delta->removed_keys);
        delta->stats->shows = delta->shows.size();
        delta->stats->hides = delta->hides.size();
    }

    NETW_PLOT(profile::names::INTEREST_EDGES, int64_t(delta->stats->edges));
    NETW_PLOT(
        profile::names::INTEREST_TRANSITIONS,
        int64_t(delta->stats->shows + delta->stats->hides)
    );
    return delta;
}

void NetwInterestEngine::commit(const Ref<NetwInterestDelta> &delta) {
    NETW_ZONE_NC("NetwInterestEngine commit", colors::INTEREST);
    NETW_ERR_COND(delta.is_null(), "interest", "Interest delta is null.");
    for (int index = 0; index < delta->keys.size(); ++index) {
        committed[delta->keys[index]]
            = PackedInt64Array(delta->new_rows[index]);
    }
    for (int index = 0; index < delta->removed_keys.size(); ++index) {
        committed.erase(delta->removed_keys[index]);
    }
    layer_rows = delta->layer_rows;
    committed_memberships = delta->memberships;
    last_stats = delta->stats;
    if (revision == delta->commit_revision) {
        dirty_layers.clear();
        dirty_entities.clear();
        removed.clear();
    }
}

PackedInt64Array NetwInterestEngine::row_of(int64_t key) const {
    const HashMap<int64_t, PackedInt64Array>::ConstIterator found
        = committed.find(key);
    return NetwInterestBitSet::resized(
        found != committed.end() ? found->value : PackedInt64Array(),
        live_peers.size()
    );
}

PackedInt64Array NetwInterestEngine::row_after(
    int64_t key,
    const Ref<NetwInterestDelta> &delta
) const {
    NETW_ERR_COND_V(
        delta.is_null(),
        row_of(key),
        "interest",
        "Interest delta is null."
    );
    for (int index = 0; index < delta->keys.size(); ++index) {
        if (delta->keys[index] == key) {
            return PackedInt64Array(delta->new_rows[index]);
        }
    }
    if (contains(delta->removed_keys, key)) {
        return NetwInterestBitSet::empty(
            live_peers.size() * NetwInterestBitSet::BITS_PER_WORD
        );
    }
    return row_of(key);
}

bool NetwInterestEngine::test(int64_t key, int bit) const {
    return NetwInterestBitSet::test(row_of(key), bit);
}

Dictionary NetwInterestEngine::rows() const {
    Dictionary out;
    for (const KeyValue<int64_t, PackedInt64Array> &entry : committed) {
        out[entry.key] = row_of(entry.key);
    }
    return out;
}

bool NetwInterestEngine::has_entity(int64_t key) const {
    return entities.has(key);
}

int NetwInterestEngine::layer_edge_count(const StringName &layer_id) const {
    const HashMap<StringName, PackedInt64Array>::ConstIterator row
        = layer_rows.find(layer_id);
    if (row == layer_rows.end()) {
        return 0;
    }
    int members = 0;
    for (const KeyValue<int64_t, LocalVector<StringName>> &entry :
         committed_memberships) {
        if (contains(entry.value, layer_id)) {
            ++members;
        }
    }
    return members * NetwInterestBitSet::popcount(row->value);
}

String NetwInterestEngine::explain(int64_t key, int bit) const {
    if (!entities.has(key)) {
        return "entity is not registered";
    }
    if (!NetwInterestBitSet::test(live_peers, bit)) {
        return "peer bit is not live";
    }
    int64_t current = key;
    while (current != 0) {
        const HashMap<int64_t, Record>::ConstIterator found
            = entities.find(current);
        if (found == entities.end()) {
            break;
        }
        const Record &record = found->value;
        if (!record.intent_all
            && !NetwInterestBitSet::test(record.intent, bit)) {
            return vformat(
                "synchronizer intent denies at route %d",
                record.route
            );
        }
        if (!record.layers.is_empty()) {
            bool admitted = false;
            for (uint32_t index = 0; index < record.layers.size(); ++index) {
                if (layer_admits_bit(record.layers[index], bit)) {
                    admitted = true;
                    break;
                }
            }
            if (!admitted) {
                return vformat(
                    "layer composition denies at route %d",
                    record.route
                );
            }
        }
        current = record.parent;
    }
    return "admitted";
}

void NetwInterestEngine::clear() {
    layers.clear();
    entities.clear();
    layer_rows.clear();
    committed.clear();
    committed_memberships.clear();
    dirty_layers.clear();
    dirty_entities.clear();
    removed.clear();
    live_peers = PackedInt64Array();
    last_stats.instantiate();
    revision = 0;
}

void NetwInterestEngine::_bind_methods() {
    ClassDB::bind_static_method(
        "NetwInterestEngine",
        D_METHOD("is_key", "key"),
        &NetwInterestEngine::is_key
    );
    ClassDB::bind_method(
        D_METHOD("set_layer", "id", "viewers", "policy"),
        &NetwInterestEngine::set_layer
    );
    ClassDB::bind_method(
        D_METHOD("remove_layer", "id"),
        &NetwInterestEngine::remove_layer
    );
    ClassDB::bind_method(
        D_METHOD("set_membership", "key", "layer_ids"),
        &NetwInterestEngine::set_membership
    );
    ClassDB::bind_method(
        D_METHOD("set_parent", "key", "parent_key"),
        &NetwInterestEngine::set_parent
    );
    ClassDB::bind_method(
        D_METHOD("set_intent", "key", "mask"),
        &NetwInterestEngine::set_intent
    );
    ClassDB::bind_method(
        D_METHOD("set_intent_all", "key"),
        &NetwInterestEngine::set_intent_all
    );
    ClassDB::bind_method(
        D_METHOD("set_order_key", "key", "depth", "route"),
        &NetwInterestEngine::set_order_key
    );
    ClassDB::bind_method(
        D_METHOD("set_live_peers", "bits"),
        &NetwInterestEngine::set_live_peers
    );
    ClassDB::bind_method(
        D_METHOD("remove_entity", "key"),
        &NetwInterestEngine::remove_entity
    );

    ClassDB::bind_method(D_METHOD("recompute"), &NetwInterestEngine::recompute);
    ClassDB::bind_method(
        D_METHOD("commit", "delta"),
        &NetwInterestEngine::commit
    );

    ClassDB::bind_method(
        D_METHOD("row_of", "key"),
        &NetwInterestEngine::row_of
    );
    ClassDB::bind_method(
        D_METHOD("row_after", "key", "delta"),
        &NetwInterestEngine::row_after
    );
    ClassDB::bind_method(
        D_METHOD("test", "key", "bit"),
        &NetwInterestEngine::test
    );
    ClassDB::bind_method(D_METHOD("rows"), &NetwInterestEngine::rows);
    ClassDB::bind_method(
        D_METHOD("has_entity", "key"),
        &NetwInterestEngine::has_entity
    );
    ClassDB::bind_method(
        D_METHOD("layer_edge_count", "layer_id"),
        &NetwInterestEngine::layer_edge_count
    );
    ClassDB::bind_method(
        D_METHOD("explain", "key", "bit"),
        &NetwInterestEngine::explain
    );
    ClassDB::bind_method(D_METHOD("stats"), &NetwInterestEngine::stats);
    ClassDB::bind_method(D_METHOD("clear"), &NetwInterestEngine::clear);

    BIND_ENUM_CONSTANT(HIDE_FROM_OUTSIDERS);
    BIND_ENUM_CONSTANT(HIDE_FROM_INSIDERS);
}

} // namespace netw
