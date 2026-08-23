#include "netw/interest_engine.hpp"

#include "godot/utility.hpp"
#include "netw/colors.hpp"
#include "netw/log.hpp"
#include "netw/profile.hpp"

using namespace godot;

namespace netw {

namespace {

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

bool contains_key(const LocalVector<int64_t> &values, int64_t value) {
    for (uint32_t index = 0; index < values.size(); ++index) {
        if (values[index] == value) {
            return true;
        }
    }
    return false;
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

int InterestBitSet::word_count(int bit_count) {
    if (bit_count <= 0) {
        return 0;
    }
    return (bit_count + BITS_PER_WORD - 1) / BITS_PER_WORD;
}

PackedInt64Array InterestBitSet::empty(int bit_count) {
    PackedInt64Array out;
    const int words = word_count(bit_count);
    out.resize(words);
    for (int index = 0; index < words; ++index) {
        out.set(index, 0);
    }
    return out;
}

PackedInt64Array InterestBitSet::full(int bit_count) {
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

PackedInt64Array InterestBitSet::resized(
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

PackedInt64Array InterestBitSet::with_bit(
    const PackedInt64Array &row,
    int bit,
    bool value
) {
    NETW_ERR_COND_V(bit < 0, row, sys::INTEREST, "Peer bit must be non-negative.");
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

bool InterestBitSet::test(const PackedInt64Array &row, int bit) {
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

PackedInt64Array InterestBitSet::union_of(
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

PackedInt64Array InterestBitSet::intersect(
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

PackedInt64Array InterestBitSet::subtract(
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

PackedInt64Array InterestBitSet::symmetric_difference(
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

bool InterestBitSet::equals(
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

PackedInt32Array InterestBitSet::bits(const PackedInt64Array &row) {
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

int InterestBitSet::popcount(const PackedInt64Array &row) {
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


PackedInt32Array InterestStats::to_array() const {
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

InterestDelta::InterestDelta() {
}

Array InterestDelta::to_array() const {
    Array out;
    out.push_back(keys);
    out.push_back(old_rows);
    out.push_back(new_rows);
    out.push_back(order_keys);
    out.push_back(shows);
    out.push_back(hides);
    out.push_back(layer_shows);
    out.push_back(layer_hides);
    out.push_back(stats.to_array());
    return out;
}


bool InterestEngine::Order::precedes(
    const Order &other,
    bool deeper_first
) const {
    if (depth != other.depth) {
        return deeper_first ? depth > other.depth : depth < other.depth;
    }
    return route < other.route;
}

InterestEngine::InterestEngine() {
    last_stats = InterestStats();
}

InterestEngine::Record &InterestEngine::record_for(int64_t key) {
    removed.erase(key);
    HashMap<int64_t, Record>::Iterator found = entities.find(key);
    if (found != entities.end()) {
        return found->value;
    }
    dirty_entities.insert(key);
    return entities.insert(key, Record())->value;
}

void InterestEngine::ensure_layer(const StringName &id) {
    if (layers.has(id)) {
        return;
    }
    layers.insert(id, Layer());
    dirty_layers.insert(id);
}

void InterestEngine::mark_layer_members_dirty(const StringName &id) {
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

void InterestEngine::mark_entity_tree_dirty(int64_t root) {
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

bool InterestEngine::layer_admits_bit(const StringName &id, int bit) const {
    const HashMap<StringName, Layer>::ConstIterator found = layers.find(id);
    if (found == layers.end()) {
        return false;
    }
    const bool viewer = InterestBitSet::test(found->value.viewers, bit);
    return found->value.policy == HIDE_FROM_OUTSIDERS ? viewer : !viewer;
}

InterestEngine::Order InterestEngine::order_of(int64_t key) const {
    const HashMap<int64_t, Record>::ConstIterator found = entities.find(key);
    if (found == entities.end()) {
        return Order();
    }
    Order out;
    out.depth = found->value.depth;
    out.route = found->value.route;
    return out;
}

void InterestEngine::set_layer(
    const StringName &id,
    const PackedInt64Array &viewers,
    int policy
) {
    NETW_ERR_COND(id.is_empty(), "interest", "Interest layer id is empty.");
    NETW_ERR_COND(
        policy != HIDE_FROM_OUTSIDERS && policy != HIDE_FROM_INSIDERS,
        sys::INTEREST,
        "Unknown interest policy %d.",
        policy
    );
    const HashMap<StringName, Layer>::Iterator found = layers.find(id);
    if (found != layers.end() && found->value.policy == Policy(policy)
        && InterestBitSet::equals(found->value.viewers, viewers)) {
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

void InterestEngine::remove_layer(const StringName &id) {
    if (!layers.has(id)) {
        return;
    }
    layers.erase(id);
    dirty_layers.insert(id);
    mark_layer_members_dirty(id);
    ++revision;
}

void InterestEngine::declare_layer(const StringName &id) {
    NETW_ERR_COND(id.is_empty(), "interest", "Interest layer id is empty.");
    ensure_layer(id);
}

bool InterestEngine::has_layer(const StringName &id) const {
    return layers.has(id);
}

bool InterestEngine::layer_add_viewer(
    const StringName &id,
    int64_t peer_id
) {
    NETW_ERR_COND_V(
        id.is_empty(),
        false,
        sys::INTEREST,
        "Interest layer id is empty."
    );
    const int bit = peer_bit_for(peer_id);
    if (bit < 0) {
        return false;
    }
    ensure_layer(id);
    Layer &layer = layers[id];
    if (InterestBitSet::test(layer.viewers, bit)) {
        return false;
    }
    layer.viewers = InterestBitSet::with_bit(layer.viewers, bit, true);
    dirty_layers.insert(id);
    mark_layer_members_dirty(id);
    ++revision;
    return true;
}

bool InterestEngine::layer_remove_viewer(
    const StringName &id,
    int64_t peer_id
) {
    const HashMap<StringName, Layer>::Iterator found = layers.find(id);
    const int bit = peer_bit_of(peer_id);
    if (found == layers.end() || bit < 0
        || !InterestBitSet::test(found->value.viewers, bit)) {
        return false;
    }
    found->value.viewers
        = InterestBitSet::with_bit(found->value.viewers, bit, false);
    dirty_layers.insert(id);
    mark_layer_members_dirty(id);
    ++revision;
    return true;
}

bool InterestEngine::layer_has_viewer(
    const StringName &id,
    int64_t peer_id
) const {
    const HashMap<StringName, Layer>::ConstIterator found = layers.find(id);
    const int bit = peer_bit_of(peer_id);
    if (found == layers.end() || bit < 0) {
        return false;
    }
    return InterestBitSet::test(found->value.viewers, bit);
}

bool InterestEngine::layer_set_policy(const StringName &id, int policy) {
    NETW_ERR_COND_V(
        id.is_empty(),
        false,
        sys::INTEREST,
        "Interest layer id is empty."
    );
    NETW_ERR_COND_V(
        policy != HIDE_FROM_OUTSIDERS && policy != HIDE_FROM_INSIDERS,
        false,
        sys::INTEREST,
        "Unknown interest policy %d.",
        policy
    );
    ensure_layer(id);
    Layer &layer = layers[id];
    if (layer.policy == Policy(policy)) {
        return false;
    }
    layer.policy = Policy(policy);
    dirty_layers.insert(id);
    mark_layer_members_dirty(id);
    ++revision;
    return true;
}

bool InterestEngine::layer_set_leave_policy(
    const StringName &id,
    int policy
) {
    NETW_ERR_COND_V(
        id.is_empty(),
        false,
        sys::INTEREST,
        "Interest layer id is empty."
    );
    NETW_ERR_COND_V(
        policy < 0 || policy > NetwInterestDecl::LEAVE_CUSTOM,
        false,
        sys::INTEREST,
        "Unknown interest leave policy %d.",
        policy
    );
    ensure_layer(id);
    Layer &layer = layers[id];
    if (layer.leave_policy == policy) {
        return false;
    }
    layer.leave_policy = int32_t(policy);
    return true;
}

int InterestEngine::layer_leave_policy(const StringName &id) const {
    const HashMap<StringName, Layer>::ConstIterator found = layers.find(id);
    return found != layers.end() ? found->value.leave_policy
                                 : int(NetwInterestDecl::LEAVE_DESPAWN);
}

bool InterestEngine::layer_set_perception_policy(
    const StringName &id,
    int policy
) {
    NETW_ERR_COND_V(
        id.is_empty(),
        false,
        sys::INTEREST,
        "Interest layer id is empty."
    );
    NETW_ERR_COND_V(
        policy < 0 || policy > NetwInterestDecl::PERCEPTION_CUSTOM,
        false,
        sys::INTEREST,
        "Unknown interest perception policy %d.",
        policy
    );
    ensure_layer(id);
    Layer &layer = layers[id];
    if (layer.perception_policy == policy) {
        return false;
    }
    layer.perception_policy = int32_t(policy);
    return true;
}

int InterestEngine::layer_perception_policy(const StringName &id) const {
    const HashMap<StringName, Layer>::ConstIterator found = layers.find(id);
    return found != layers.end() ? found->value.perception_policy
                                 : int(NetwInterestDecl::PERCEPTION_HIDE);
}

int InterestEngine::layer_policy(const StringName &id) const {
    const HashMap<StringName, Layer>::ConstIterator found = layers.find(id);
    return found != layers.end() ? int(found->value.policy)
                                 : int(HIDE_FROM_OUTSIDERS);
}

bool InterestEngine::layer_admits(
    const StringName &id,
    int64_t peer_id
) const {
    if (peer_id == 0) {
        return false;
    }
    const int bit = peer_bit_of(peer_id);
    if (bit < 0) {
        return layer_policy(id) == HIDE_FROM_INSIDERS;
    }
    return layer_admits_bit(id, bit);
}

String InterestEngine::layer_explain(
    const StringName &id,
    int64_t peer_id
) const {
    if (peer_id == 0) {
        return "REJECT peer=0 (no peer context)";
    }
    const bool viewer = layer_has_viewer(id, peer_id);
    const bool outsiders = layer_policy(id) == HIDE_FROM_OUTSIDERS;
    return vformat(
        "%s peer=%d %s viewers under %s",
        layer_admits(id, peer_id) ? "ADMIT" : "REJECT",
        peer_id,
        viewer ? "in" : "not in",
        outsiders ? "HIDE_FROM_OUTSIDERS" : "HIDE_FROM_INSIDERS"
    );
}

PackedInt64Array InterestEngine::layer_viewers(const StringName &id) const {
    PackedInt64Array out;
    const HashMap<StringName, Layer>::ConstIterator found = layers.find(id);
    if (found == layers.end()) {
        return out;
    }
    const PackedInt32Array set = InterestBitSet::bits(found->value.viewers);
    for (int index = 0; index < set.size(); ++index) {
        const int64_t peer_id = peer_of_bit(set[index]);
        if (peer_id != 0) {
            out.push_back(peer_id);
        }
    }
    return out;
}

bool InterestEngine::roster_add(const StringName &id, int64_t key) {
    NETW_ERR_COND_V(
        id.is_empty(),
        false,
        sys::INTEREST,
        "Interest layer id is empty."
    );
    NETW_ERR_COND_V(
        !is_key(key),
        false,
        sys::INTEREST,
        "Interest entity key must be positive."
    );
    ensure_layer(id);
    LocalVector<int64_t> &members = layers[id].members;
    if (contains_key(members, key)) {
        return false;
    }
    members.push_back(key);
    return true;
}

bool InterestEngine::roster_remove(const StringName &id, int64_t key) {
    const HashMap<StringName, Layer>::Iterator found = layers.find(id);
    if (found == layers.end()) {
        return false;
    }
    LocalVector<int64_t> &members = found->value.members;
    for (uint32_t index = 0; index < members.size(); ++index) {
        if (members[index] == key) {
            members.remove_at(index);
            return true;
        }
    }
    return false;
}

bool InterestEngine::roster_has(const StringName &id, int64_t key) const {
    const HashMap<StringName, Layer>::ConstIterator found = layers.find(id);
    return found != layers.end() && contains_key(found->value.members, key);
}

bool InterestEngine::set_exit_handler(
    int64_t key,
    const Callable &handler
) {
    if (exit_handlers.has(key)) {
        return false;
    }
    exit_handlers.insert(key, handler);
    return true;
}

Callable InterestEngine::exit_handler(int64_t key) const {
    const HashMap<int64_t, Callable>::ConstIterator found
        = exit_handlers.find(key);
    return found ? found->value : Callable();
}

Callable InterestEngine::take_exit_handler(int64_t key) {
    const HashMap<int64_t, Callable>::Iterator found = exit_handlers.find(key);
    if (!found) {
        return Callable();
    }
    const Callable handler = found->value;
    exit_handlers.remove(found);
    return handler;
}

StringName InterestEngine::scene_membership(int64_t key) const {
    const HashMap<int64_t, StringName>::ConstIterator found
        = scene_memberships.find(key);
    return found ? found->value : StringName();
}

bool InterestEngine::set_scene_membership(
    int64_t key,
    const StringName &id
) {
    const HashMap<int64_t, StringName>::Iterator found
        = scene_memberships.find(key);
    if (id.is_empty()) {
        if (!found) {
            return false;
        }
        scene_memberships.remove(found);
        return true;
    }
    if (found) {
        if (found->value == id) {
            return false;
        }
        found->value = id;
        return true;
    }
    scene_memberships.insert(key, id);
    return true;
}

int64_t InterestEngine::transitions_total() const {
    int64_t total = 0;
    for (const KeyValue<StringName, Layer> &row : layers) {
        total += row.value.transitions;
    }
    return total;
}

PackedInt64Array InterestEngine::co_members(
    int64_t key,
    const Array &layer_ids
) const {
    PackedInt64Array out;
    HashSet<int64_t> seen;
    for (int64_t index = 0; index < layer_ids.size(); ++index) {
        const HashMap<StringName, Layer>::ConstIterator found
            = layers.find(layer_ids[index]);
        if (!found) {
            continue;
        }
        const LocalVector<int64_t> &members = found->value.members;
        for (uint32_t at = 0; at < members.size(); ++at) {
            if (members[at] == key || seen.has(members[at])) {
                continue;
            }
            seen.insert(members[at]);
            out.push_back(members[at]);
        }
    }
    return out;
}

bool InterestEngine::projection_admits(
    int64_t key,
    const Ref<NetwInterestDecl> &decl
) const {
    NETW_ERR_COND_V(
        decl.is_null(),
        false,
        sys::INTEREST,
        "InterestEngine.projection_admits: entity %d declares nothing to "
        "project from.",
        key
    );
    const Array labels = decl->labels();
    if (labels.is_empty()) {
        return true;
    }
    for (int64_t index = 0; index < labels.size(); ++index) {
        if (roster_has(labels[index], key)) {
            return true;
        }
    }
    return false;
}

PackedInt64Array InterestEngine::roster(const StringName &id) const {
    PackedInt64Array out;
    const HashMap<StringName, Layer>::ConstIterator found = layers.find(id);
    if (found == layers.end()) {
        return out;
    }
    for (uint32_t index = 0; index < found->value.members.size(); ++index) {
        out.push_back(found->value.members[index]);
    }
    return out;
}

void InterestEngine::note_transition(const StringName &id) {
    if (id.is_empty()) {
        return;
    }
    ensure_layer(id);
    ++layers[id].transitions;
}

int64_t InterestEngine::transitions(const StringName &id) const {
    const HashMap<StringName, Layer>::ConstIterator found = layers.find(id);
    return found != layers.end() ? found->value.transitions : 0;
}

PackedInt64Array InterestEngine::viewer_peers() const {
    PackedInt64Array seen;
    for (const KeyValue<StringName, Layer> &entry : layers) {
        seen = InterestBitSet::union_of(seen, entry.value.viewers);
    }
    PackedInt64Array out;
    const PackedInt32Array set = InterestBitSet::bits(seen);
    for (int index = 0; index < set.size(); ++index) {
        const int64_t peer_id = peer_of_bit(set[index]);
        if (peer_id != 0) {
            out.push_back(peer_id);
        }
    }
    return out;
}

void InterestEngine::set_membership(int64_t key, const Array &layer_ids) {
    NETW_ERR_COND(
        !is_key(key),
        sys::INTEREST,
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

bool InterestEngine::membership_add(
    int64_t key,
    const StringName &layer_id
) {
    NETW_ERR_COND_V(
        !is_key(key),
        false,
        sys::INTEREST,
        "Interest entity key must be positive."
    );
    NETW_ERR_COND_V(
        layer_id.is_empty(),
        false,
        sys::INTEREST,
        "Interest layer id is empty."
    );
    Record &record = record_for(key);
    if (contains(record.layers, layer_id)) {
        return false;
    }
    record.layers.push_back(layer_id);
    ensure_layer(layer_id);
    mark_entity_tree_dirty(key);
    ++revision;
    return true;
}

bool InterestEngine::membership_remove(
    int64_t key,
    const StringName &layer_id
) {
    const HashMap<int64_t, Record>::Iterator found = entities.find(key);
    if (found == entities.end()) {
        return false;
    }
    LocalVector<StringName> &layers = found->value.layers;
    for (uint32_t index = 0; index < layers.size(); ++index) {
        if (layers[index] != layer_id) {
            continue;
        }
        layers.remove_at(index);
        mark_entity_tree_dirty(key);
        ++revision;
        return true;
    }
    return false;
}

Array InterestEngine::memberships(int64_t key) const {
    Array out;
    const HashMap<int64_t, Record>::ConstIterator found = entities.find(key);
    if (!found) {
        return out;
    }
    for (uint32_t index = 0; index < found->value.layers.size(); ++index) {
        out.push_back(found->value.layers[index]);
    }
    return out;
}

bool InterestEngine::has_memberships(int64_t key) const {
    const HashMap<int64_t, Record>::ConstIterator found = entities.find(key);
    return found && !found->value.layers.is_empty();
}

bool InterestEngine::has_intent(int64_t key) const {
    const HashMap<int64_t, Record>::ConstIterator found = entities.find(key);
    return found && !found->value.intent_all;
}

bool InterestEngine::had_committed_intent(int64_t key) const {
    return committed_intents.has(key);
}

int InterestEngine::dirty_count() const {
    return int(dirty_entities.size());
}

PackedInt64Array InterestEngine::intent_keys() const {
    PackedInt64Array out;
    for (const KeyValue<int64_t, Record> &entry : entities) {
        if (!entry.value.intent_all) {
            out.push_back(entry.key);
        }
    }
    return out;
}

PackedInt64Array InterestEngine::membership_keys() const {
    PackedInt64Array out;
    for (const KeyValue<int64_t, Record> &entry : entities) {
        if (!entry.value.layers.is_empty()) {
            out.push_back(entry.key);
        }
    }
    return out;
}

void InterestEngine::set_parent(int64_t key, int64_t parent_key) {
    NETW_ERR_COND(
        !is_key(key),
        sys::INTEREST,
        "Interest entity key must be positive."
    );
    NETW_ERR_COND(
        key == parent_key,
        sys::INTEREST,
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
            sys::INTEREST,
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

void InterestEngine::set_intent(int64_t key, const PackedInt64Array &mask) {
    NETW_ERR_COND(
        !is_key(key),
        sys::INTEREST,
        "Interest entity key must be positive."
    );
    Record &record = record_for(key);
    if (!record.intent_all && InterestBitSet::equals(record.intent, mask)) {
        return;
    }
    record.intent_all = false;
    record.intent = mask;
    mark_entity_tree_dirty(key);
    ++revision;
}

void InterestEngine::set_intent_for_peers(
    int64_t key,
    const PackedInt64Array &peer_ids
) {
    NETW_ZONE_NC("InterestEngine intent by peer", colors::INTEREST);
    NETW_ERR_COND(
        !is_key(key),
        sys::INTEREST,
        "Interest entity key must be positive."
    );
    PackedInt64Array mask;
    for (int at = 0; at < peer_ids.size(); ++at) {
        const int bit = peer_bit_for(peer_ids[at]);
        if (bit < 0) {
            continue;
        }
        mask = InterestBitSet::with_bit(mask, bit, true);
    }
    NETW_TRACE(
        sys::INTEREST,
        "Interest intent for key %d names %d peer(s).",
        int(key),
        peer_ids.size()
    );
    set_intent(key, mask);
}

void InterestEngine::set_intent_all(int64_t key) {
    NETW_ERR_COND(
        !is_key(key),
        sys::INTEREST,
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

void InterestEngine::set_order_key(int64_t key, int depth, int route) {
    NETW_ERR_COND(
        !is_key(key),
        sys::INTEREST,
        "Interest entity key must be positive."
    );
    NETW_ERR_COND(depth < 0, sys::INTEREST, "Interest order depth is negative.");
    NETW_ERR_COND(route < 0, sys::INTEREST, "Interest order route is negative.");
    Record &record = record_for(key);
    if (record.depth == depth && record.route == route) {
        return;
    }
    record.depth = int32_t(depth);
    record.route = int32_t(route);
    dirty_entities.insert(key);
    ++revision;
}

void InterestEngine::set_live_peers(const PackedInt64Array &bits) {
    if (InterestBitSet::equals(live_peers, bits)) {
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

bool InterestEngine::set_live_peer_ids(const PackedInt64Array &peer_ids) {
    NETW_ZONE_NC("InterestEngine live peers by id", colors::INTEREST);
    PackedInt64Array bits;
    bool minted = false;
    for (int at = 0; at < peer_ids.size(); ++at) {
        const int64_t peer_id = peer_ids[at];
        minted = minted || peer_bit_of(peer_id) < 0;
        const int bit = peer_bit_for(peer_id);
        if (bit < 0) {
            continue;
        }
        bits = InterestBitSet::with_bit(bits, bit, true);
    }
    NETW_TRACE(
        sys::INTEREST,
        "Interest live peers name %d peer(s), %d newly bitted.",
        peer_ids.size(),
        int(minted)
    );
    set_live_peers(bits);
    return minted;
}

void InterestEngine::remove_entity(int64_t key) {
    NETW_ERR_COND(
        !is_key(key),
        sys::INTEREST,
        "Interest entity key must be positive."
    );
    if (!entities.has(key) && !committed.has(key)) {
        return;
    }
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

void InterestEngine::recompute_layers(
    HashMap<StringName, PackedInt64Array> &rows_by_layer,
    InterestStats &out_stats
) const {
    NETW_ZONE_NC("InterestEngine layer fold", colors::INTEREST);
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
            ? InterestBitSet::intersect(found->value.viewers, live_peers)
            : InterestBitSet::subtract(live_peers, found->value.viewers);
        ++out_stats.layers_recomputed;
    }
}

PackedInt64Array InterestEngine::compute_entity_row(
    int64_t key,
    const HashMap<StringName, PackedInt64Array> &rows_by_layer,
    const HashMap<int64_t, PackedInt64Array> &desired_rows
) const {
    const Record &record = entities.find(key)->value;
    PackedInt64Array grant;
    if (record.layers.is_empty()) {
        grant = live_peers;
    } else {
        grant = InterestBitSet::empty(
            live_peers.size() * InterestBitSet::BITS_PER_WORD
        );
        for (uint32_t index = 0; index < record.layers.size(); ++index) {
            const HashMap<StringName, PackedInt64Array>::ConstIterator admit
                = rows_by_layer.find(record.layers[index]);
            grant = InterestBitSet::union_of(
                grant,
                admit != rows_by_layer.end() ? admit->value : PackedInt64Array()
            );
        }
    }
    if (!record.intent_all) {
        grant = InterestBitSet::intersect(grant, record.intent);
    }
    if (record.parent != 0) {
        const HashMap<int64_t, PackedInt64Array>::ConstIterator pending
            = desired_rows.find(record.parent);
        grant = InterestBitSet::intersect(
            grant,
            pending != desired_rows.end() ? pending->value
                                          : row_of(record.parent)
        );
    }
    return InterestBitSet::intersect(grant, live_peers);
}

void InterestEngine::append_transition_bits(
    int64_t key,
    const PackedInt64Array &old_row,
    const PackedInt64Array &desired,
    const Order &order,
    LocalVector<Transition> &shows,
    LocalVector<Transition> &hides
) const {
    const PackedInt32Array gained = InterestBitSet::bits(
        InterestBitSet::subtract(desired, old_row)
    );
    for (int index = 0; index < gained.size(); ++index) {
        Transition transition;
        transition.key = key;
        transition.bit = gained[index];
        transition.order = order;
        shows.push_back(transition);
    }
    const PackedInt32Array lost = InterestBitSet::bits(
        InterestBitSet::subtract(old_row, desired)
    );
    for (int index = 0; index < lost.size(); ++index) {
        Transition transition;
        transition.key = key;
        transition.bit = lost[index];
        transition.order = order;
        hides.push_back(transition);
    }
}

void InterestEngine::append_layer_transition_bits(
    const StringName &id,
    int64_t key,
    const PackedInt64Array &old_row,
    const PackedInt64Array &desired,
    const Order &order,
    LocalVector<LayerTransition> &shows,
    LocalVector<LayerTransition> &hides
) const {
    const PackedInt32Array gained = InterestBitSet::bits(
        InterestBitSet::subtract(desired, old_row)
    );
    for (int index = 0; index < gained.size(); ++index) {
        LayerTransition transition;
        transition.layer = id;
        transition.key = key;
        transition.bit = gained[index];
        transition.order = order;
        shows.push_back(transition);
    }
    const PackedInt32Array lost = InterestBitSet::bits(
        InterestBitSet::subtract(old_row, desired)
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

LocalVector<int64_t> InterestEngine::ordered_dirty_keys() const {
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

LocalVector<int64_t> InterestEngine::ordered_removed_keys() const {
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

int InterestEngine::edge_count_after(
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
        out += InterestBitSet::popcount(
            moved != changed.end() ? moved->value : entry.value
        );
        seen.insert(entry.key);
    }
    for (const KeyValue<int64_t, PackedInt64Array> &entry : changed) {
        if (seen.has(entry.key) || contains(removed_keys, entry.key)) {
            continue;
        }
        out += InterestBitSet::popcount(entry.value);
    }
    return out;
}

InterestDelta InterestEngine::recompute() {
    NETW_ZONE_NC("InterestEngine recompute", colors::INTEREST);
    NETW_ZONE_VALUE(dirty_entities.size());
    InterestDelta delta;
    delta.commit_revision = revision;

    HashMap<StringName, PackedInt64Array> rows_by_layer(layer_rows);
    recompute_layers(rows_by_layer, delta.stats);

    HashMap<int64_t, PackedInt64Array> desired_rows;
    HashMap<int64_t, PackedInt64Array> changed_rows;
    LocalVector<Transition> shows;
    LocalVector<Transition> hides;
    LocalVector<LayerTransition> layer_show_rows;
    LocalVector<LayerTransition> layer_hide_rows;

    {
        NETW_ZONE_NC("InterestEngine entity fold", colors::INTEREST);
        const LocalVector<int64_t> ordered = ordered_dirty_keys();
        for (uint32_t index = 0; index < ordered.size(); ++index) {
            const int64_t key = ordered[index];
            if (!entities.has(key)) {
                ++delta.stats.vanished_dirty_skips;
                continue;
            }
            const PackedInt64Array desired
                = compute_entity_row(key, rows_by_layer, desired_rows);
            desired_rows[key] = desired;
            ++delta.stats.entities_recomputed;

            const PackedInt64Array old_row = row_of(key);
            changed_rows[key] = desired;
            const Order order = order_of(key);
            if (!InterestBitSet::equals(old_row, desired)) {
                delta.keys.push_back(key);
                delta.old_rows.push_back(old_row);
                delta.new_rows.push_back(desired);
                delta.order_keys.push_back(pair(order.depth, order.route));
                append_transition_bits(
                    key,
                    old_row,
                    desired,
                    order,
                    shows,
                    hides
                );
            }

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
        NETW_ZONE_NC("InterestEngine removal fold", colors::INTEREST);
        const LocalVector<int64_t> gone = ordered_removed_keys();
        for (uint32_t index = 0; index < gone.size(); ++index) {
            const int64_t key = gone[index];
            const PackedInt64Array old_row = row_of(key);
            const PackedInt64Array desired = InterestBitSet::empty(
                live_peers.size() * InterestBitSet::BITS_PER_WORD
            );
            changed_rows[key] = desired;
            const Order order = removed.find(key)->value;
            if (!InterestBitSet::equals(old_row, desired)) {
                delta.keys.push_back(key);
                delta.old_rows.push_back(old_row);
                delta.new_rows.push_back(desired);
                delta.order_keys.push_back(pair(order.depth, order.route));
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
            delta.removed_keys.push_back(key);
        }
    }

    {
        NETW_ZONE_NC("InterestEngine ordering", colors::INTEREST);
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
            delta.shows.push_back(pair(shows[index].key, shows[index].bit));
        }
        for (uint32_t index = 0; index < hides.size(); ++index) {
            delta.hides.push_back(pair(hides[index].key, hides[index].bit));
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
            delta.layer_shows.push_back(row);
        }
        for (uint32_t index = 0; index < layer_hide_rows.size(); ++index) {
            Array row;
            row.push_back(layer_hide_rows[index].layer);
            row.push_back(layer_hide_rows[index].key);
            row.push_back(layer_hide_rows[index].bit);
            delta.layer_hides.push_back(row);
        }
    }

    {
        NETW_ZONE_NC("InterestEngine snapshot", colors::INTEREST);
        delta.layer_rows = rows_by_layer;
        for (const KeyValue<int64_t, Record> &entry : entities) {
            delta.memberships.insert(entry.key, entry.value.layers);
            if (!entry.value.intent_all) {
                delta.intents.insert(entry.key);
            }
        }
        delta.stats.words_per_row = live_peers.size();
        delta.stats.edges
            = edge_count_after(changed_rows, delta.removed_keys);
        delta.stats.shows = delta.shows.size();
        delta.stats.hides = delta.hides.size();
    }

    NETW_PLOT(profile::names::INTEREST_EDGES, int64_t(delta.stats.edges));
    NETW_PLOT(
        profile::names::INTEREST_TRANSITIONS,
        int64_t(delta.stats.shows + delta.stats.hides)
    );
    return delta;
}

void InterestEngine::commit(const InterestDelta &delta) {
    NETW_ZONE_NC("InterestEngine commit", colors::INTEREST);
    for (int index = 0; index < delta.keys.size(); ++index) {
        committed[delta.keys[index]]
            = PackedInt64Array(delta.new_rows[index]);
    }
    for (int index = 0; index < delta.removed_keys.size(); ++index) {
        committed.erase(delta.removed_keys[index]);
    }
    layer_rows = delta.layer_rows;
    committed_memberships = delta.memberships;
    committed_intents = delta.intents;
    last_stats = delta.stats;
    if (revision == delta.commit_revision) {
        dirty_layers.clear();
        dirty_entities.clear();
        removed.clear();
    }
}

PackedInt64Array InterestEngine::row_of(int64_t key) const {
    const HashMap<int64_t, PackedInt64Array>::ConstIterator found
        = committed.find(key);
    return InterestBitSet::resized(
        found != committed.end() ? found->value : PackedInt64Array(),
        live_peers.size()
    );
}

PackedInt64Array InterestEngine::admitted_peers(int64_t key) const {
    PackedInt64Array out;
    const PackedInt32Array set = InterestBitSet::bits(row_of(key));
    for (int at = 0; at < set.size(); ++at) {
        const int64_t peer_id = peer_of_bit(set[at]);
        if (peer_id == 0) {
            continue;
        }
        out.push_back(peer_id);
    }
    return out;
}

PackedInt64Array InterestEngine::row_after(
    int64_t key,
    const InterestDelta &delta
) const {
    for (int index = 0; index < delta.keys.size(); ++index) {
        if (delta.keys[index] == key) {
            return PackedInt64Array(delta.new_rows[index]);
        }
    }
    if (contains(delta.removed_keys, key)) {
        return InterestBitSet::empty(
            live_peers.size() * InterestBitSet::BITS_PER_WORD
        );
    }
    return row_of(key);
}

bool InterestEngine::test(int64_t key, int bit) const {
    return InterestBitSet::test(row_of(key), bit);
}

Dictionary InterestEngine::rows() const {
    Dictionary out;
    for (const KeyValue<int64_t, PackedInt64Array> &entry : committed) {
        out[entry.key] = row_of(entry.key);
    }
    return out;
}

bool InterestEngine::has_entity(int64_t key) const {
    return entities.has(key);
}

int InterestEngine::layer_edge_count(const StringName &layer_id) const {
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
    return members * InterestBitSet::popcount(row->value);
}

String InterestEngine::explain(int64_t key, int bit) const {
    if (!entities.has(key)) {
        return "entity is not registered";
    }
    if (!InterestBitSet::test(live_peers, bit)) {
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
            && !InterestBitSet::test(record.intent, bit)) {
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

int InterestEngine::order_route_for(int64_t key) {
    NETW_ERR_COND_V(
        !is_key(key),
        0,
        sys::INTEREST,
        "Interest entity key must be positive."
    );
    const HashMap<int64_t, int32_t>::ConstIterator found
        = fallback_routes.find(key);
    if (found) {
        return found->value;
    }
    const int route = next_fallback_route++;
    fallback_routes.insert(key, route);
    return route;
}

int InterestEngine::peer_bit_for(int64_t peer_id) {
    NETW_ERR_COND_V(
        peer_id == 0,
        -1,
        sys::INTEREST,
        "Interest peer id must be non-zero."
    );
    const HashMap<int64_t, int32_t>::ConstIterator found
        = peer_bits.find(peer_id);
    if (found) {
        return found->value;
    }
    const int bit = int(bit_peers.size());
    peer_bits.insert(peer_id, bit);
    bit_peers.push_back(peer_id);
    return bit;
}

int InterestEngine::peer_bit_of(int64_t peer_id) const {
    const HashMap<int64_t, int32_t>::ConstIterator found
        = peer_bits.find(peer_id);
    return found ? found->value : -1;
}

int64_t InterestEngine::peer_of_bit(int bit) const {
    if (bit < 0 || bit >= int(bit_peers.size())) {
        return 0;
    }
    return bit_peers[bit];
}

PackedInt64Array InterestEngine::known_peers() const {
    PackedInt64Array out;
    out.resize(int(bit_peers.size()));
    for (uint32_t at = 0; at < bit_peers.size(); ++at) {
        out.set(int(at), bit_peers[at]);
    }
    return out;
}

void InterestEngine::clear() {
    layers.clear();
    peer_bits.clear();
    bit_peers.clear();
    exit_handlers.clear();
    scene_memberships.clear();
    fallback_routes.clear();
    next_fallback_route = 1;
    entities.clear();
    layer_rows.clear();
    committed.clear();
    committed_memberships.clear();
    committed_intents.clear();
    dirty_layers.clear();
    dirty_entities.clear();
    removed.clear();
    live_peers = PackedInt64Array();
    last_stats = InterestStats();
    revision = 0;
}


} // namespace netw
