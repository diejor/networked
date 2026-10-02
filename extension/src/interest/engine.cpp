#include "netw/interest/engine.hpp"

#include <algorithm>

#include "godot/utility.hpp"
#include "netw/colors.hpp"
#include "netw/log.hpp"
#include "netw/profile.hpp"

using namespace godot;

namespace netw::interest {

namespace {

template <typename T, typename Less>
void stable_sort(LocalVector<T> &values, Less less) {
    if (values.size() < 2) {
        return;
    }
    std::stable_sort(values.ptr(), values.ptr() + values.size(), less);
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

int BitSet::word_count(int bit_count) {
    if (bit_count <= 0) {
        return 0;
    }
    return (bit_count + BITS_PER_WORD - 1) / BITS_PER_WORD;
}

PackedInt64Array BitSet::empty(int bit_count) {
    PackedInt64Array out;
    const int words = word_count(bit_count);
    out.resize(words);
    for (int index = 0; index < words; ++index) {
        out.set(index, 0);
    }
    return out;
}

PackedInt64Array BitSet::full(int bit_count) {
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

PackedInt64Array BitSet::resized(const PackedInt64Array &row, int words) {
    const int target = words > 0 ? words : 0;
    PackedInt64Array out = row;
    out.resize(target);
    for (int index = row.size(); index < target; ++index) {
        out.set(index, 0);
    }
    return out;
}

PackedInt64Array BitSet::with_bit(
    const PackedInt64Array &row,
    int bit,
    bool value
) {
    NETW_ERR_COND_V(
        bit < 0,
        row,
        sys::INTEREST,
        "Interest was given a negative peer index."
    );
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

bool BitSet::test(const PackedInt64Array &row, int bit) {
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

PackedInt64Array BitSet::union_of(
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

PackedInt64Array BitSet::intersect(
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

PackedInt64Array BitSet::subtract(
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

PackedInt64Array BitSet::symmetric_difference(
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

bool BitSet::equals(
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

PackedInt32Array BitSet::bits(const PackedInt64Array &row) {
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

int BitSet::popcount(const PackedInt64Array &row) {
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

PackedInt32Array Stats::to_array() const {
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

Delta::Delta() {
}

Array Delta::to_array() const {
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

bool Engine::Order::precedes(const Order &other, bool deeper_first) const {
    if (depth != other.depth) {
        return deeper_first ? depth > other.depth : depth < other.depth;
    }
    return route < other.route;
}

Engine::Engine() {
    last_stats = Stats();
}

Engine::Record &Engine::record_for(int64_t key) {
    removed.erase(key);
    HashMap<int64_t, Record>::Iterator found = entities.find(key);
    if (found != entities.end()) {
        return found->value;
    }
    Record &made = entities.insert(key, Record())->value;
    if (children.has(key)) {
        mark_entity_tree_dirty(key);
    } else {
        dirty_entities.insert(key);
    }
    return made;
}

void Engine::note_moved(int64_t key) {
    if (moved_overflow) {
        return;
    }
    if (moved.size() >= MOVED_KEPT) {
        moved.clear();
        moved_overflow = true;
        return;
    }
    moved.push_back(key);
}

bool Engine::take_moved(LocalVector<int64_t> &r_keys) {
    r_keys.clear();
    for (uint32_t at = 0; at < moved.size(); ++at) {
        r_keys.push_back(moved[at]);
    }
    moved.clear();
    const bool overflowed = moved_overflow;
    moved_overflow = false;
    return overflowed;
}

void Engine::link_child(int64_t parent, int64_t key) {
    if (parent == 0) {
        return;
    }
    HashMap<int64_t, LocalVector<int64_t>>::Iterator found
        = children.find(parent);
    if (found == children.end()) {
        found = children.insert(parent, LocalVector<int64_t>());
    }
    found->value.push_back(key);
}

void Engine::unlink_child(int64_t parent, int64_t key) {
    if (parent == 0) {
        return;
    }
    HashMap<int64_t, LocalVector<int64_t>>::Iterator found
        = children.find(parent);
    if (found == children.end()) {
        return;
    }
    LocalVector<int64_t> &held = found->value;
    for (uint32_t at = 0; at < held.size(); ++at) {
        if (held[at] == key) {
            held.remove_at(at);
            break;
        }
    }
    if (held.is_empty()) {
        children.remove(found);
    }
}

void Engine::index_layer(const StringName &id, int64_t key, bool member) {
    HashMap<StringName, HashSet<int64_t>>::Iterator found = layer_keys.find(id);
    if (member) {
        if (found == layer_keys.end()) {
            found = layer_keys.insert(id, HashSet<int64_t>());
        }
        found->value.insert(key);
        return;
    }
    if (found == layer_keys.end()) {
        return;
    }
    found->value.erase(key);
    if (found->value.is_empty()) {
        layer_keys.remove(found);
    }
}

int Engine::committed_popcount(int64_t key) const {
    const HashMap<int64_t, PackedInt64Array>::ConstIterator found
        = committed.find(key);
    return found != committed.end() ? BitSet::popcount(found->value) : 0;
}

void Engine::ensure_layer(const StringName &id) {
    if (layers.has(id)) {
        return;
    }
    layers.insert(id, Layer());
    dirty_layers.insert(id);
}

void Engine::mark_layer_members_dirty(const StringName &id) {
    const HashMap<StringName, HashSet<int64_t>>::ConstIterator members
        = layer_keys.find(id);
    if (members != layer_keys.end()) {
        for (const int64_t &key : members->value) {
            mark_entity_tree_dirty(key);
        }
    }
    for (const KeyValue<int64_t, StringName> &root : scene_roots) {
        if (root.value != id) {
            continue;
        }
        int64_t above = entities.has(root.key)
            ? entities.find(root.key)->value.parent
            : 0;
        for (uint32_t guard = 0; guard <= entities.size() && above != 0;
             ++guard) {
            dirty_entities.insert(above);
            const HashMap<int64_t, Record>::ConstIterator held
                = entities.find(above);
            above = held != entities.end() ? held->value.parent : 0;
        }
    }
}

void Engine::mark_entity_tree_dirty(int64_t root) {
    dirty_entities.insert(root);
    LocalVector<int64_t> pending;
    pending.push_back(root);
    while (!pending.is_empty()) {
        const int64_t key = pending[pending.size() - 1];
        pending.resize(pending.size() - 1);
#if defined(NETW_TESTS)
        mark_visits += 1;
#endif
        const HashMap<int64_t, LocalVector<int64_t>>::ConstIterator below
            = children.find(key);
        if (below == children.end()) {
            continue;
        }
        for (uint32_t at = 0; at < below->value.size(); ++at) {
            const int64_t child = below->value[at];
            if (!entities.has(child)) {
                continue;
            }
            dirty_entities.insert(child);
            pending.push_back(child);
        }
    }
}

bool Engine::layer_admits_bit(const StringName &id, int bit) const {
    const HashMap<StringName, Layer>::ConstIterator found = layers.find(id);
    if (found == layers.end()) {
        return false;
    }
    const bool viewer = BitSet::test(found->value.viewers, bit);
    return found->value.policy == HIDE_FROM_OUTSIDERS ? viewer : !viewer;
}

Engine::Order Engine::order_of(int64_t key) const {
    const HashMap<int64_t, Record>::ConstIterator found = entities.find(key);
    if (found == entities.end()) {
        return Order();
    }
    Order out;
    out.depth = found->value.depth;
    out.route = found->value.route;
    return out;
}

void Engine::set_layer(
    const StringName &id,
    const PackedInt64Array &viewers,
    int policy
) {
    NETW_ERR_COND(
        id.is_empty(),
        sys::INTEREST,
        "An interest layer needs a name, and it was empty."
    );
    NETW_ERR_COND(
        policy != HIDE_FROM_OUTSIDERS && policy != HIDE_FROM_INSIDERS,
        sys::INTEREST,
        "Interest policy %d does not exist.",
        policy
    );
    const HashMap<StringName, Layer>::Iterator found = layers.find(id);
    if (found != layers.end() && found->value.policy == Policy(policy)
        && BitSet::equals(found->value.viewers, viewers)) {
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

void Engine::remove_layer(const StringName &id) {
    if (!layers.has(id)) {
        return;
    }
    layers.erase(id);
    dirty_layers.insert(id);
    mark_layer_members_dirty(id);
    ++revision;
}

void Engine::declare_layer(const StringName &id) {
    NETW_ERR_COND(
        id.is_empty(),
        sys::INTEREST,
        "An interest layer needs a name, and it was empty."
    );
    ensure_layer(id);
}

bool Engine::has_layer(const StringName &id) const {
    return layers.has(id);
}

bool Engine::layer_add_viewer(const StringName &id, int64_t peer_id) {
    NETW_ERR_COND_V(
        id.is_empty(),
        false,
        sys::INTEREST,
        "An interest layer needs a name, and it was empty."
    );
    const int bit = peer_bit_for(peer_id);
    if (bit < 0) {
        return false;
    }
    ensure_layer(id);
    Layer &layer = layers[id];
    if (BitSet::test(layer.viewers, bit)) {
        return false;
    }
    layer.viewers = BitSet::with_bit(layer.viewers, bit, true);
    dirty_layers.insert(id);
    mark_layer_members_dirty(id);
    ++revision;
    return true;
}

bool Engine::layer_remove_viewer(const StringName &id, int64_t peer_id) {
    const HashMap<StringName, Layer>::Iterator found = layers.find(id);
    const int bit = peer_bit_of(peer_id);
    if (found == layers.end() || bit < 0
        || !BitSet::test(found->value.viewers, bit)) {
        return false;
    }
    found->value.viewers = BitSet::with_bit(found->value.viewers, bit, false);
    dirty_layers.insert(id);
    mark_layer_members_dirty(id);
    ++revision;
    return true;
}

bool Engine::forget_peer(int64_t peer_id) {
    const int bit = peer_bit_of(peer_id);
    if (bit < 0) {
        return false;
    }
    bool changed = false;
    for (KeyValue<StringName, Layer> &entry : layers) {
        if (!BitSet::test(entry.value.viewers, bit)) {
            continue;
        }
        entry.value.viewers = BitSet::with_bit(entry.value.viewers, bit, false);
        dirty_layers.insert(entry.key);
        mark_layer_members_dirty(entry.key);
        changed = true;
    }
    if (changed) {
        ++revision;
    }
    return changed;
}

bool Engine::layer_has_viewer(const StringName &id, int64_t peer_id) const {
    const HashMap<StringName, Layer>::ConstIterator found = layers.find(id);
    const int bit = peer_bit_of(peer_id);
    if (found == layers.end() || bit < 0) {
        return false;
    }
    return BitSet::test(found->value.viewers, bit);
}

bool Engine::layer_set_policy(const StringName &id, int policy) {
    NETW_ERR_COND_V(
        id.is_empty(),
        false,
        sys::INTEREST,
        "An interest layer needs a name, and it was empty."
    );
    NETW_ERR_COND_V(
        policy != HIDE_FROM_OUTSIDERS && policy != HIDE_FROM_INSIDERS,
        false,
        sys::INTEREST,
        "Interest policy %d does not exist.",
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

bool Engine::layer_set_leave_policy(const StringName &id, int policy) {
    NETW_ERR_COND_V(
        id.is_empty(),
        false,
        sys::INTEREST,
        "An interest layer needs a name, and it was empty."
    );
    NETW_ERR_COND_V(
        policy < 0 || policy > Decl::LEAVE_CUSTOM,
        false,
        sys::INTEREST,
        "Interest leave policy %d does not exist.",
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

int Engine::layer_leave_policy(const StringName &id) const {
    const HashMap<StringName, Layer>::ConstIterator found = layers.find(id);
    return found != layers.end() ? found->value.leave_policy
                                 : int(Decl::LEAVE_HIDE);
}

bool Engine::layer_set_perception_policy(const StringName &id, int policy) {
    NETW_ERR_COND_V(
        id.is_empty(),
        false,
        sys::INTEREST,
        "An interest layer needs a name, and it was empty."
    );
    NETW_ERR_COND_V(
        policy < 0 || policy > Decl::PERCEPTION_CUSTOM,
        false,
        sys::INTEREST,
        "Interest perception policy %d does not exist.",
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

int Engine::layer_perception_policy(const StringName &id) const {
    const HashMap<StringName, Layer>::ConstIterator found = layers.find(id);
    return found != layers.end() ? found->value.perception_policy
                                 : int(Decl::PERCEPTION_HIDE);
}

int Engine::layer_policy(const StringName &id) const {
    const HashMap<StringName, Layer>::ConstIterator found = layers.find(id);
    return found != layers.end() ? int(found->value.policy)
                                 : int(HIDE_FROM_OUTSIDERS);
}

bool Engine::layer_admits(const StringName &id, int64_t peer_id) const {
    if (peer_id == 0) {
        return false;
    }
    const int bit = peer_bit_of(peer_id);
    if (bit < 0) {
        return layer_policy(id) == HIDE_FROM_INSIDERS;
    }
    return layer_admits_bit(id, bit);
}

String Engine::layer_explain(const StringName &id, int64_t peer_id) const {
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

PackedInt64Array Engine::layer_viewers(const StringName &id) const {
    PackedInt64Array out;
    const HashMap<StringName, Layer>::ConstIterator found = layers.find(id);
    if (found == layers.end()) {
        return out;
    }
    const PackedInt32Array set = BitSet::bits(found->value.viewers);
    for (int index = 0; index < set.size(); ++index) {
        const int64_t peer_id = peer_of_bit(set[index]);
        if (peer_id != 0) {
            out.push_back(peer_id);
        }
    }
    return out;
}

bool Engine::roster_add(const StringName &id, int64_t key) {
    NETW_ERR_COND_V(
        id.is_empty(),
        false,
        sys::INTEREST,
        "An interest layer needs a name, and it was empty."
    );
    NETW_ERR_COND_V(
        !is_key(key),
        false,
        sys::INTEREST,
        "Interest was given an entity with no valid key."
    );
    ensure_layer(id);
    Layer &layer = layers[id];
    if (layer.member_set.has(key)) {
        return false;
    }
    layer.member_set.insert(key);
    layer.members.push_back(key);
    note_moved(key);
    return true;
}

bool Engine::roster_remove(const StringName &id, int64_t key) {
    const HashMap<StringName, Layer>::Iterator found = layers.find(id);
    if (found == layers.end() || !found->value.member_set.has(key)) {
        return false;
    }
    found->value.member_set.erase(key);
    LocalVector<int64_t> &members = found->value.members;
    for (uint32_t index = 0; index < members.size(); ++index) {
        if (members[index] == key) {
            members.remove_at(index);
            break;
        }
    }
    note_moved(key);
    return true;
}

bool Engine::roster_has(const StringName &id, int64_t key) const {
    const HashMap<StringName, Layer>::ConstIterator found = layers.find(id);
    return found != layers.end() && found->value.member_set.has(key);
}

bool Engine::set_exit_handler(int64_t key, const Callable &handler) {
    if (exit_handlers.has(key)) {
        return false;
    }
    exit_handlers.insert(key, handler);
    return true;
}

Callable Engine::exit_handler(int64_t key) const {
    const HashMap<int64_t, Callable>::ConstIterator found
        = exit_handlers.find(key);
    return found ? found->value : Callable();
}

Callable Engine::take_exit_handler(int64_t key) {
    const HashMap<int64_t, Callable>::Iterator found = exit_handlers.find(key);
    if (!found) {
        return Callable();
    }
    const Callable handler = found->value;
    exit_handlers.remove(found);
    return handler;
}

StringName Engine::scene_membership(int64_t key) const {
    const HashMap<int64_t, StringName>::ConstIterator found
        = scene_memberships.find(key);
    return found ? found->value : StringName();
}

bool Engine::set_scene_membership(int64_t key, const StringName &id) {
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

bool Engine::set_scene_root(int64_t key, const StringName &id) {
    const HashMap<int64_t, StringName>::Iterator found = scene_roots.find(key);
    if (id.is_empty()) {
        if (!found) {
            return false;
        }
        scene_roots.remove(found);
        mark_entity_tree_dirty(key);
        return true;
    }
    if (found) {
        if (found->value == id) {
            return false;
        }
        found->value = id;
        mark_entity_tree_dirty(key);
        return true;
    }
    scene_roots.insert(key, id);
    mark_entity_tree_dirty(key);
    return true;
}

StringName Engine::scene_root_layer(int64_t key) const {
    const HashMap<int64_t, StringName>::ConstIterator found
        = scene_roots.find(key);
    return found ? found->value : StringName();
}

bool Engine::descends_from(int64_t key, int64_t ancestor) const {
    int64_t at = key;
    for (uint32_t guard = 0; guard <= entities.size(); ++guard) {
        const HashMap<int64_t, Record>::ConstIterator found = entities.find(at);
        if (found == entities.end() || found->value.parent == 0) {
            return false;
        }
        at = found->value.parent;
        if (at == ancestor) {
            return true;
        }
    }
    return false;
}

PackedInt64Array Engine::nested_scene_residency(
    int64_t key,
    const HashMap<StringName, PackedInt64Array> &rows_by_layer
) const {
    PackedInt64Array grant;
    for (const KeyValue<int64_t, StringName> &row : scene_roots) {
        if (row.key == key || !descends_from(row.key, key)) {
            continue;
        }
        const HashMap<StringName, PackedInt64Array>::ConstIterator found
            = rows_by_layer.find(row.value);
        if (found == rows_by_layer.end()) {
            continue;
        }
        grant = BitSet::union_of(grant, found->value);
    }
    return grant;
}

int64_t Engine::transitions_total() const {
    int64_t total = 0;
    for (const KeyValue<StringName, Layer> &row : layers) {
        total += row.value.transitions;
    }
    return total;
}

PackedInt64Array Engine::co_members(int64_t key, const Array &layer_ids) const {
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

bool Engine::projection_admits(int64_t key, const Decl *decl) const {
    NETW_ERR_COND_V(
        decl == nullptr,
        false,
        sys::INTEREST,
        "Interest has no declaration for entity %d, so it is not shown.",
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

PackedInt64Array Engine::roster(const StringName &id) const {
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

void Engine::note_transition(const StringName &id) {
    if (id.is_empty()) {
        return;
    }
    ensure_layer(id);
    ++layers[id].transitions;
}

int64_t Engine::transitions(const StringName &id) const {
    const HashMap<StringName, Layer>::ConstIterator found = layers.find(id);
    return found != layers.end() ? found->value.transitions : 0;
}

PackedInt64Array Engine::viewer_peers() const {
    PackedInt64Array seen;
    for (const KeyValue<StringName, Layer> &entry : layers) {
        seen = BitSet::union_of(seen, entry.value.viewers);
    }
    PackedInt64Array out;
    const PackedInt32Array set = BitSet::bits(seen);
    for (int index = 0; index < set.size(); ++index) {
        const int64_t peer_id = peer_of_bit(set[index]);
        if (peer_id != 0) {
            out.push_back(peer_id);
        }
    }
    return out;
}

void Engine::set_membership(int64_t key, const Array &layer_ids) {
    NETW_ERR_COND(
        !is_key(key),
        sys::INTEREST,
        "Interest was given an entity with no valid key."
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
    for (uint32_t index = 0; index < record.layers.size(); ++index) {
        index_layer(record.layers[index], key, false);
    }
    record.layers = normalized;
    for (uint32_t index = 0; index < record.layers.size(); ++index) {
        index_layer(record.layers[index], key, true);
    }
    mark_entity_tree_dirty(key);
    note_moved(key);
    ++revision;
}

bool Engine::membership_add(int64_t key, const StringName &layer_id) {
    NETW_ERR_COND_V(
        !is_key(key),
        false,
        sys::INTEREST,
        "Interest was given an entity with no valid key."
    );
    NETW_ERR_COND_V(
        layer_id.is_empty(),
        false,
        sys::INTEREST,
        "An interest layer needs a name, and it was empty."
    );
    Record &record = record_for(key);
    if (contains(record.layers, layer_id)) {
        return false;
    }
    record.layers.push_back(layer_id);
    index_layer(layer_id, key, true);
    ensure_layer(layer_id);
    mark_entity_tree_dirty(key);
    note_moved(key);
    ++revision;
    return true;
}

bool Engine::membership_remove(int64_t key, const StringName &layer_id) {
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
        index_layer(layer_id, key, false);
        mark_entity_tree_dirty(key);
        note_moved(key);
        ++revision;
        return true;
    }
    return false;
}

Array Engine::memberships(int64_t key) const {
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

bool Engine::has_memberships(int64_t key) const {
    const HashMap<int64_t, Record>::ConstIterator found = entities.find(key);
    return found && !found->value.layers.is_empty();
}

bool Engine::has_intent(int64_t key) const {
    const HashMap<int64_t, Record>::ConstIterator found = entities.find(key);
    return found && !found->value.intent_all;
}

bool Engine::had_committed_intent(int64_t key) const {
    return committed_intents.has(key);
}

int Engine::dirty_count() const {
    return int(dirty_entities.size());
}

PackedInt64Array Engine::intent_keys() const {
    PackedInt64Array out;
    for (const KeyValue<int64_t, Record> &entry : entities) {
        if (!entry.value.intent_all) {
            out.push_back(entry.key);
        }
    }
    return out;
}

PackedInt64Array Engine::membership_keys() const {
    PackedInt64Array out;
    for (const KeyValue<int64_t, Record> &entry : entities) {
        if (!entry.value.layers.is_empty()) {
            out.push_back(entry.key);
        }
    }
    return out;
}

void Engine::set_parent(int64_t key, int64_t parent_key) {
    NETW_ERR_COND(
        !is_key(key),
        sys::INTEREST,
        "Interest was given an entity with no valid key."
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
            "An entity cannot be the parent of its own parent."
        );
        walk = found->value.parent;
    }
    Record &record = record_for(key);
    if (record.parent == parent_key) {
        return;
    }
    unlink_child(record.parent, key);
    record.parent = parent_key;
    link_child(parent_key, key);
    mark_entity_tree_dirty(key);
    ++revision;
}

void Engine::set_intent(int64_t key, const PackedInt64Array &mask) {
    NETW_ERR_COND(
        !is_key(key),
        sys::INTEREST,
        "Interest was given an entity with no valid key."
    );
    Record &record = record_for(key);
    if (!record.intent_all && BitSet::equals(record.intent, mask)) {
        return;
    }
    record.intent_all = false;
    record.intent = mask;
    mark_entity_tree_dirty(key);
    ++revision;
}

void Engine::set_intent_for_peers(
    int64_t key,
    const PackedInt64Array &peer_ids
) {
    NETW_ZONE_NC("interest::Engine intent by peer", colors::INTEREST);
    NETW_ERR_COND(
        !is_key(key),
        sys::INTEREST,
        "Interest was given an entity with no valid key."
    );
    PackedInt64Array mask;
    for (int at = 0; at < peer_ids.size(); ++at) {
        const int bit = peer_bit_for(peer_ids[at]);
        if (bit < 0) {
            continue;
        }
        mask = BitSet::with_bit(mask, bit, true);
    }
    NETW_TRACE(
        sys::INTEREST,
        "Interest intent for key %d names %d peer(s).",
        int(key),
        peer_ids.size()
    );
    set_intent(key, mask);
}

void Engine::set_intent_all(int64_t key) {
    NETW_ERR_COND(
        !is_key(key),
        sys::INTEREST,
        "Interest was given an entity with no valid key."
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

void Engine::set_order_key(int64_t key, int depth, int route) {
    NETW_ERR_COND(
        !is_key(key),
        sys::INTEREST,
        "Interest was given an entity with no valid key."
    );
    NETW_ERR_COND(
        depth < 0,
        sys::INTEREST,
        "Interest was given a negative tree depth."
    );
    NETW_ERR_COND(
        route < 0,
        sys::INTEREST,
        "Interest was given a negative route."
    );
    Record &record = record_for(key);
    if (record.depth == depth && record.route == route) {
        return;
    }
    record.depth = int32_t(depth);
    record.route = int32_t(route);
    dirty_entities.insert(key);
    ++revision;
}

void Engine::set_live_peers(const PackedInt64Array &bits) {
    if (BitSet::equals(live_peers, bits)) {
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

bool Engine::set_live_peer_ids(const PackedInt64Array &peer_ids) {
    NETW_ZONE_NC("interest::Engine live peers by id", colors::INTEREST);
    PackedInt64Array bits;
    bool minted = false;
    for (int at = 0; at < peer_ids.size(); ++at) {
        const int64_t peer_id = peer_ids[at];
        minted = minted || peer_bit_of(peer_id) < 0;
        const int bit = peer_bit_for(peer_id);
        if (bit < 0) {
            continue;
        }
        bits = BitSet::with_bit(bits, bit, true);
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

void Engine::remove_entity(int64_t key) {
    NETW_ERR_COND(
        !is_key(key),
        sys::INTEREST,
        "Interest was given an entity with no valid key."
    );
    if (!entities.has(key) && !committed.has(key)) {
        return;
    }
    removed.insert(key, order_of(key));
    const HashMap<int64_t, Record>::Iterator leaving = entities.find(key);
    if (leaving != entities.end()) {
        unlink_child(leaving->value.parent, key);
        for (uint32_t at = 0; at < leaving->value.layers.size(); ++at) {
            index_layer(leaving->value.layers[at], key, false);
        }
        entities.remove(leaving);
    }
    dirty_entities.erase(key);
    LocalVector<int64_t> orphans;
    const HashMap<int64_t, LocalVector<int64_t>>::Iterator below
        = children.find(key);
    if (below != children.end()) {
        for (uint32_t at = 0; at < below->value.size(); ++at) {
            orphans.push_back(below->value[at]);
        }
        children.remove(below);
    }
    for (uint32_t index = 0; index < orphans.size(); ++index) {
        const HashMap<int64_t, Record>::Iterator orphan
            = entities.find(orphans[index]);
        if (orphan == entities.end()) {
            continue;
        }
        orphan->value.parent = 0;
        mark_entity_tree_dirty(orphans[index]);
    }
    note_moved(key);
    ++revision;
}

void Engine::recompute_layers(
    HashMap<StringName, PackedInt64Array> &rows_by_layer,
    Stats &out_stats
) const {
    NETW_ZONE_NC("interest::Engine layer fold", colors::INTEREST);
    NETW_ZONE_VALUE(dirty_layers.size());
    LocalVector<StringName> ids;
    for (const StringName &id : dirty_layers) {
        ids.push_back(id);
    }
    stable_sort(ids, [](const StringName &left, const StringName &right) {
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
            ? BitSet::intersect(found->value.viewers, live_peers)
            : BitSet::subtract(live_peers, found->value.viewers);
        ++out_stats.layers_recomputed;
    }
}

PackedInt64Array Engine::compute_entity_row(
    int64_t key,
    const HashMap<StringName, PackedInt64Array> &rows_by_layer,
    const HashMap<int64_t, PackedInt64Array> &desired_rows
) const {
    const Record &record = entities.find(key)->value;
    PackedInt64Array grant;
    if (record.layers.is_empty()) {
        grant = live_peers;
    } else {
        grant = BitSet::empty(live_peers.size() * BitSet::BITS_PER_WORD);
        for (uint32_t index = 0; index < record.layers.size(); ++index) {
            const HashMap<StringName, PackedInt64Array>::ConstIterator admit
                = rows_by_layer.find(record.layers[index]);
            grant = BitSet::union_of(
                grant,
                admit != rows_by_layer.end() ? admit->value : PackedInt64Array()
            );
        }
    }
    if (!record.intent_all) {
        grant = BitSet::intersect(grant, record.intent);
    }
    if (scene_roots.has(key)) {
        grant = BitSet::union_of(
            grant,
            nested_scene_residency(key, rows_by_layer)
        );
    }
    if (record.parent != 0) {
        const HashMap<int64_t, PackedInt64Array>::ConstIterator pending
            = desired_rows.find(record.parent);
        grant = BitSet::intersect(
            grant,
            pending != desired_rows.end() ? pending->value
                                          : row_of(record.parent)
        );
    }
    return BitSet::intersect(grant, live_peers);
}

void Engine::append_transition_bits(
    int64_t key,
    const PackedInt64Array &old_row,
    const PackedInt64Array &desired,
    const Order &order,
    LocalVector<Transition> &shows,
    LocalVector<Transition> &hides
) const {
    const PackedInt32Array gained
        = BitSet::bits(BitSet::subtract(desired, old_row));
    for (int index = 0; index < gained.size(); ++index) {
        Transition transition;
        transition.key = key;
        transition.bit = gained[index];
        transition.order = order;
        shows.push_back(transition);
    }
    const PackedInt32Array lost
        = BitSet::bits(BitSet::subtract(old_row, desired));
    for (int index = 0; index < lost.size(); ++index) {
        Transition transition;
        transition.key = key;
        transition.bit = lost[index];
        transition.order = order;
        hides.push_back(transition);
    }
}

void Engine::append_layer_transition_bits(
    const StringName &id,
    int64_t key,
    const PackedInt64Array &old_row,
    const PackedInt64Array &desired,
    const Order &order,
    LocalVector<LayerTransition> &shows,
    LocalVector<LayerTransition> &hides
) const {
    const PackedInt32Array gained
        = BitSet::bits(BitSet::subtract(desired, old_row));
    for (int index = 0; index < gained.size(); ++index) {
        LayerTransition transition;
        transition.layer = id;
        transition.key = key;
        transition.bit = gained[index];
        transition.order = order;
        shows.push_back(transition);
    }
    const PackedInt32Array lost
        = BitSet::bits(BitSet::subtract(old_row, desired));
    for (int index = 0; index < lost.size(); ++index) {
        LayerTransition transition;
        transition.layer = id;
        transition.key = key;
        transition.bit = lost[index];
        transition.order = order;
        hides.push_back(transition);
    }
}

LocalVector<int64_t> Engine::ordered_dirty_keys() const {
    LocalVector<int64_t> out;
    for (const int64_t key : dirty_entities) {
        out.push_back(key);
    }
    stable_sort(out, [this](int64_t left, int64_t right) {
        const Order left_order = order_of(left);
        const Order right_order = order_of(right);
        if (left_order == right_order) {
            return left < right;
        }
        return left_order.precedes(right_order, false);
    });
    return out;
}

LocalVector<int64_t> Engine::ordered_removed_keys() const {
    LocalVector<int64_t> out;
    for (const KeyValue<int64_t, Order> &entry : removed) {
        out.push_back(entry.key);
    }
    stable_sort(out, [this](int64_t left, int64_t right) {
        const Order left_order = removed.find(left)->value;
        const Order right_order = removed.find(right)->value;
        if (left_order == right_order) {
            return left < right;
        }
        return left_order.precedes(right_order, false);
    });
    return out;
}

Delta Engine::recompute() {
    NETW_ZONE_NC("interest::Engine recompute", colors::INTEREST);
    NETW_ZONE_VALUE(dirty_entities.size());
    Delta delta;
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
        NETW_ZONE_NC("interest::Engine entity fold", colors::INTEREST);
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
            delta.recomputed.push_back(key);

            const PackedInt64Array old_row = row_of(key);
            changed_rows[key] = desired;
            const Order order = order_of(key);
            if (!BitSet::equals(old_row, desired)) {
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
        NETW_ZONE_NC("interest::Engine removal fold", colors::INTEREST);
        const LocalVector<int64_t> gone = ordered_removed_keys();
        for (uint32_t index = 0; index < gone.size(); ++index) {
            const int64_t key = gone[index];
            const PackedInt64Array old_row = row_of(key);
            const PackedInt64Array desired
                = BitSet::empty(live_peers.size() * BitSet::BITS_PER_WORD);
            changed_rows[key] = desired;
            const Order order = removed.find(key)->value;
            if (!BitSet::equals(old_row, desired)) {
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
        NETW_ZONE_NC("interest::Engine ordering", colors::INTEREST);
        stable_sort(shows, [](const Transition &left, const Transition &right) {
            if (left.order == right.order) {
                return false;
            }
            return left.order.precedes(right.order, false);
        });
        stable_sort(hides, [](const Transition &left, const Transition &right) {
            if (left.order == right.order) {
                return false;
            }
            return left.order.precedes(right.order, true);
        });
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
        stable_sort(layer_show_rows, layer_less(false));
        stable_sort(layer_hide_rows, layer_less(true));
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
        NETW_ZONE_NC("interest::Engine snapshot", colors::INTEREST);
        delta.layer_rows = rows_by_layer;
        for (uint32_t at = 0; at < delta.recomputed.size(); ++at) {
            const int64_t key = delta.recomputed[at];
            const Record &record = entities.find(key)->value;
            delta.memberships.insert(key, record.layers);
            if (!record.intent_all) {
                delta.intents.insert(key);
            }
        }
        delta.stats.words_per_row = live_peers.size();
        int64_t edges = committed_edges;
        for (const KeyValue<int64_t, PackedInt64Array> &entry : changed_rows) {
            edges += BitSet::popcount(entry.value)
                - committed_popcount(entry.key);
        }
        delta.stats.edges = int32_t(edges);
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

void Engine::commit(const Delta &delta) {
    NETW_ZONE_NC("interest::Engine commit", colors::INTEREST);
    for (int index = 0; index < delta.keys.size(); ++index) {
        const int64_t key = delta.keys[index];
        const PackedInt64Array row(delta.new_rows[index]);
        committed_edges += BitSet::popcount(row) - committed_popcount(key);
        committed[key] = row;
        note_moved(key);
    }
    for (int index = 0; index < delta.removed_keys.size(); ++index) {
        const int64_t key = delta.removed_keys[index];
        committed_edges -= committed_popcount(key);
        committed.erase(key);
        committed_memberships.erase(key);
        committed_intents.erase(key);
        note_moved(key);
    }
    for (uint32_t at = 0; at < delta.recomputed.size(); ++at) {
        const int64_t key = delta.recomputed[at];
        const HashMap<int64_t, LocalVector<StringName>>::ConstIterator named
            = delta.memberships.find(key);
        if (named != delta.memberships.end()) {
            committed_memberships[key] = named->value;
        }
        if (delta.intents.has(key)) {
            committed_intents.insert(key);
        } else {
            committed_intents.erase(key);
        }
    }
    layer_rows = delta.layer_rows;
    last_stats = delta.stats;
    if (revision == delta.commit_revision) {
        dirty_layers.clear();
        dirty_entities.clear();
        removed.clear();
    }
}

PackedInt64Array Engine::row_of(int64_t key) const {
    const HashMap<int64_t, PackedInt64Array>::ConstIterator found
        = committed.find(key);
    return BitSet::resized(
        found != committed.end() ? found->value : PackedInt64Array(),
        live_peers.size()
    );
}

PackedInt64Array Engine::admitted_peers(int64_t key) const {
    PackedInt64Array out;
    const PackedInt32Array set = BitSet::bits(row_of(key));
    for (int at = 0; at < set.size(); ++at) {
        const int64_t peer_id = peer_of_bit(set[at]);
        if (peer_id == 0) {
            continue;
        }
        out.push_back(peer_id);
    }
    return out;
}

PackedInt64Array Engine::row_after(int64_t key, const Delta &delta) const {
    for (int index = 0; index < delta.keys.size(); ++index) {
        if (delta.keys[index] == key) {
            return PackedInt64Array(delta.new_rows[index]);
        }
    }
    if (contains(delta.removed_keys, key)) {
        return BitSet::empty(live_peers.size() * BitSet::BITS_PER_WORD);
    }
    return row_of(key);
}

bool Engine::test(int64_t key, int bit) const {
    return BitSet::test(row_of(key), bit);
}

Dictionary Engine::rows() const {
    Dictionary out;
    for (const KeyValue<int64_t, PackedInt64Array> &entry : committed) {
        out[entry.key] = row_of(entry.key);
    }
    return out;
}

bool Engine::has_entity(int64_t key) const {
    return entities.has(key);
}

int Engine::layer_edge_count(const StringName &layer_id) const {
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
    return members * BitSet::popcount(row->value);
}

String Engine::explain(int64_t key, int bit) const {
    if (!entities.has(key)) {
        return "entity is not registered";
    }
    if (!BitSet::test(live_peers, bit)) {
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
        if (!record.intent_all && !BitSet::test(record.intent, bit)) {
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

int Engine::order_route_for(int64_t key) {
    NETW_ERR_COND_V(
        !is_key(key),
        0,
        sys::INTEREST,
        "Interest was given an entity with no valid key."
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

int Engine::peer_bit_for(int64_t peer_id) {
    NETW_ERR_COND_V(
        peer_id == 0,
        -1,
        sys::INTEREST,
        "Interest was given peer 0, which is not a peer."
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

int Engine::peer_bit_of(int64_t peer_id) const {
    const HashMap<int64_t, int32_t>::ConstIterator found
        = peer_bits.find(peer_id);
    return found ? found->value : -1;
}

int64_t Engine::peer_of_bit(int bit) const {
    if (bit < 0 || bit >= int(bit_peers.size())) {
        return 0;
    }
    return bit_peers[bit];
}

PackedInt64Array Engine::known_peers() const {
    PackedInt64Array out;
    out.resize(int(bit_peers.size()));
    for (uint32_t at = 0; at < bit_peers.size(); ++at) {
        out.set(int(at), bit_peers[at]);
    }
    return out;
}

void Engine::clear() {
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
    last_stats = Stats();
    revision = 0;
    children.clear();
    layer_keys.clear();
    committed_edges = 0;
    moved.clear();
    moved_overflow = true;
}

} // namespace netw::interest
