// The interest verdict core's laws.
//
// The core is a pure function from plain state to a peer-row matrix, so its
// specification is a set of properties that must hold over every state rather
// than a script of observations. Each case below states one of them.
//
// The oracle case enumerates every state of a two-entity two-layer model and
// checks the engine against a grant computed independently of it, which is what
// makes the algebra covered rather than sampled.
//
// The last two cases guard the key itself. An entity is named by an integer
// slot minted above the engine, and both a null slot and a hash order that is
// not a total order are mistakes an object-keyed store could not make.

#include "support/netw_test.h"

#include "godot/templates.hpp"
#include "godot/utility.hpp"
#include "netw/interest_engine.hpp"

namespace TestNetwInterestEngine {

using namespace godot;
using netw::NetwInterestBitSet;
using netw::NetwInterestDelta;
using netw::NetwInterestEngine;

constexpr int64_t ROOT = 1;
constexpr int64_t CHILD = 2;
constexpr int64_t LEAF = 3;

constexpr NetwInterestEngine::Policy OUTSIDERS
    = NetwInterestEngine::HIDE_FROM_OUTSIDERS;
constexpr NetwInterestEngine::Policy INSIDERS
    = NetwInterestEngine::HIDE_FROM_INSIDERS;

PackedInt64Array bits(std::initializer_list<int> p_bits) {
    PackedInt64Array out;
    for (const int bit : p_bits) {
        out = NetwInterestBitSet::with_bit(out, bit, true);
    }
    return out;
}

Array names(std::initializer_list<const char *> p_ids) {
    Array out;
    for (const char *id : p_ids) {
        out.push_back(StringName(id));
    }
    return out;
}

Ref<NetwInterestEngine> fresh() {
    Ref<NetwInterestEngine> engine;
    engine.instantiate();
    return engine;
}

void commit(const Ref<NetwInterestEngine> &p_engine) {
    p_engine->commit(p_engine->recompute());
}

// A deep comparison, because Godot's Array equality is by reference and the
// delta is arrays of arrays. Two engines agreeing by reference would be the one
// answer a determinism check can never accept.
bool same(const Variant &p_left, const Variant &p_right) {
    if (p_left.get_type() != p_right.get_type()) {
        return false;
    }
    if (p_left.get_type() == Variant::ARRAY) {
        const Array left = p_left;
        const Array right = p_right;
        if (left.size() != right.size()) {
            return false;
        }
        for (int index = 0; index < left.size(); ++index) {
            if (!same(left[index], right[index])) {
                return false;
            }
        }
        return true;
    }
    return bool(p_left == p_right);
}

int missing_bits(
    const PackedInt64Array &p_left,
    const PackedInt64Array &p_right
) {
    return NetwInterestBitSet::popcount(
        NetwInterestBitSet::subtract(p_left, p_right)
    );
}

// Three entities in a parent chain over three live peers, with nothing granted
// yet. Every fixture below grows from this one.
Ref<NetwInterestEngine> chain_engine() {
    Ref<NetwInterestEngine> engine = fresh();
    engine->set_live_peers(bits({0, 1, 2}));
    engine->set_order_key(ROOT, 0, 1);
    engine->set_order_key(CHILD, 1, 2);
    engine->set_order_key(LEAF, 2, 3);
    engine->set_parent(CHILD, ROOT);
    engine->set_parent(LEAF, CHILD);
    return engine;
}

Ref<NetwInterestEngine> configured_engine() {
    Ref<NetwInterestEngine> engine = chain_engine();
    engine->set_layer(StringName("near"), bits({0, 2}), OUTSIDERS);
    engine->set_layer(StringName("blind"), bits({2}), INSIDERS);
    engine->set_membership(ROOT, names({"near"}));
    engine->set_membership(CHILD, names({"blind"}));
    engine->set_membership(LEAF, Array());
    engine->set_intent(CHILD, bits({0, 1}));
    return engine;
}

// The same final state reached through two opposite mutation orders.
Ref<NetwInterestEngine> scripted_engine(bool p_reverse) {
    Ref<NetwInterestEngine> engine = fresh();
    engine->set_live_peers(bits({0, 1, 2}));
    const int64_t keys[3] = {ROOT, CHILD, LEAF};
    for (int step = 0; step < 3; ++step) {
        const int at = p_reverse ? 2 - step : step;
        engine->set_order_key(keys[at], at, at + 1);
    }
    for (int step = 0; step < 2; ++step) {
        const int at = p_reverse ? 1 - step : step;
        if (at == 0) {
            engine->set_layer(StringName("near"), bits({0, 2}), OUTSIDERS);
        } else {
            engine->set_layer(StringName("blind"), bits({2}), INSIDERS);
        }
    }
    engine->set_parent(CHILD, ROOT);
    engine->set_parent(LEAF, CHILD);
    engine->set_membership(ROOT, names({"near"}));
    engine->set_membership(CHILD, names({"blind"}));
    engine->set_membership(LEAF, Array());
    engine->set_intent(CHILD, bits({0, 1}));
    return engine;
}

Ref<NetwInterestEngine> single_entity_engine(
    NetwInterestEngine::Policy p_policy
) {
    Ref<NetwInterestEngine> engine = fresh();
    engine->set_live_peers(bits({0, 1, 2}));
    engine->set_layer(StringName("test"), PackedInt64Array(), p_policy);
    engine->set_membership(ROOT, names({"test"}));
    engine->set_order_key(ROOT, 0, 1);
    return engine;
}

StringName layer_id(int p_index) {
    return StringName(p_index == 0 ? "a" : "b");
}

PackedInt64Array random_bits() {
    PackedInt64Array out;
    for (int bit = 0; bit < 3; ++bit) {
        if (netw::gd::randi_range(0, 1) == 1) {
            out = NetwInterestBitSet::with_bit(out, bit, true);
        }
    }
    return out;
}

// One seeded random state, applied in one of two orders. Both arms re-seed the
// same generator and draw in the same sequence, so the STATE is identical and
// only the order of arrival differs, which is the whole point.
Ref<NetwInterestEngine> random_state_engine(int p_seed, bool p_reverse) {
    netw::gd::seed(p_seed);
    PackedInt64Array layer_viewers[2];
    NetwInterestEngine::Policy policies[2];
    for (int index = 0; index < 2; ++index) {
        layer_viewers[index] = random_bits();
        policies[index]
            = netw::gd::randi_range(0, 1) == 1 ? INSIDERS : OUTSIDERS;
    }
    Array memberships[3];
    PackedInt64Array intents[3];
    for (int entity = 0; entity < 3; ++entity) {
        for (int layer = 0; layer < 2; ++layer) {
            if (netw::gd::randi_range(0, 1) == 1) {
                memberships[entity].push_back(layer_id(layer));
            }
        }
        intents[entity] = random_bits();
    }

    Ref<NetwInterestEngine> engine = fresh();
    engine->set_live_peers(bits({0, 1, 2}));
    for (int step = 0; step < 2; ++step) {
        const int at = p_reverse ? 1 - step : step;
        engine->set_layer(layer_id(at), layer_viewers[at], policies[at]);
    }
    const int64_t keys[3] = {ROOT, CHILD, LEAF};
    for (int step = 0; step < 3; ++step) {
        const int at = p_reverse ? 2 - step : step;
        engine->set_order_key(keys[at], at, at + 1);
        engine->set_membership(keys[at], memberships[at]);
        engine->set_intent(keys[at], intents[at]);
    }
    engine->set_parent(CHILD, ROOT);
    engine->set_parent(LEAF, CHILD);
    return engine;
}

TEST_CASE(
    "[Networked][Interest][Hosted] the same state yields the same rows"
) {
    const Ref<NetwInterestEngine> first = configured_engine();
    const Ref<NetwInterestEngine> second = configured_engine();
    const Ref<NetwInterestDelta> first_delta = first->recompute();
    const Ref<NetwInterestDelta> second_delta = second->recompute();

    CHECK(same(first_delta->to_array(), second_delta->to_array()));
    first->commit(first_delta);
    second->commit(second_delta);
    CHECK(same(first->rows(), second->rows()));
}

TEST_CASE(
    "[Networked][Interest][Hosted] mutation order does not move the "
    "matrix"
) {
    const Ref<NetwInterestEngine> forward = scripted_engine(false);
    const Ref<NetwInterestEngine> reverse = scripted_engine(true);

    CHECK(
        same(forward->recompute()->to_array(), reverse->recompute()->to_array())
    );
}

TEST_CASE(
    "[Networked][Interest][Hosted] seeded states survive a permuted "
    "replay"
) {
    int diverged = -1;
    for (int seed = 0; seed < 64 && diverged < 0; ++seed) {
        const Ref<NetwInterestEngine> forward
            = random_state_engine(seed, false);
        const Ref<NetwInterestEngine> reverse = random_state_engine(seed, true);
        if (!same(
                forward->recompute()->to_array(),
                reverse->recompute()->to_array()
            )) {
            diverged = seed;
        }
    }
    NETW_CHECK_EQ(diverged, -1);
}

TEST_CASE(
    "[Networked][Interest][Hosted] repeating a mutation and a flush "
    "changes nothing"
) {
    const Ref<NetwInterestEngine> engine = configured_engine();
    engine->commit(engine->recompute());
    engine->set_membership(ROOT, names({"near"}));
    engine->set_layer(StringName("near"), bits({0, 2}), OUTSIDERS);
    const Ref<NetwInterestDelta> second = engine->recompute();

    CHECK(second->is_empty());
    engine->commit(second);
    CHECK(engine->recompute()->is_empty());
}

TEST_CASE("[Networked][Interest][Hosted] more viewers only ever grants more peers") {
    const Ref<NetwInterestEngine> engine = single_entity_engine(OUTSIDERS);
    engine->set_layer(StringName("test"), bits({0}), OUTSIDERS);
    commit(engine);
    const PackedInt64Array smaller = engine->row_of(ROOT);
    engine->set_layer(StringName("test"), bits({0, 1}), OUTSIDERS);
    commit(engine);
    const PackedInt64Array larger = engine->row_of(ROOT);

    NETW_CHECK_EQ(missing_bits(smaller, larger), 0);
    CHECK(NetwInterestBitSet::test(larger, 1));
}

TEST_CASE("[Networked][Interest][Hosted] more viewers only ever hides from more peers") {
    const Ref<NetwInterestEngine> engine = single_entity_engine(INSIDERS);
    engine->set_layer(StringName("test"), bits({0}), INSIDERS);
    commit(engine);
    const PackedInt64Array larger = engine->row_of(ROOT);
    engine->set_layer(StringName("test"), bits({0, 1}), INSIDERS);
    commit(engine);
    const PackedInt64Array smaller = engine->row_of(ROOT);

    NETW_CHECK_EQ(missing_bits(smaller, larger), 0);
    CHECK(!NetwInterestBitSet::test(smaller, 1));
}

TEST_CASE(
    "[Networked][Interest][Hosted] an ancestor row always clamps its "
    "descendants"
) {
    const Ref<NetwInterestEngine> engine = configured_engine();
    commit(engine);

    NETW_CHECK_EQ(missing_bits(engine->row_of(CHILD), engine->row_of(ROOT)), 0);
    NETW_CHECK_EQ(missing_bits(engine->row_of(LEAF), engine->row_of(CHILD)), 0);
}

TEST_CASE(
    "[Networked][Interest][Hosted] an entity in no layer is granted by "
    "intent alone"
) {
    const Ref<NetwInterestEngine> engine = fresh();
    engine->set_live_peers(bits({0, 1, 2}));
    engine->set_membership(ROOT, Array());
    engine->set_order_key(ROOT, 0, 1);
    engine->set_intent(ROOT, bits({0, 2}));
    commit(engine);

    CHECK(bool(engine->row_of(ROOT) == bits({0, 2})));
}

TEST_CASE(
    "[Networked][Interest][Hosted] the delta names what changed and "
    "nothing else"
) {
    const Ref<NetwInterestEngine> engine = single_entity_engine(OUTSIDERS);
    engine->set_layer(StringName("test"), bits({0}), OUTSIDERS);
    commit(engine);
    engine->set_layer(StringName("test"), bits({1, 2}), OUTSIDERS);
    const Ref<NetwInterestDelta> delta = engine->recompute();

    Array expected_shows;
    Array first_show;
    first_show.push_back(ROOT);
    first_show.push_back(1);
    Array second_show;
    second_show.push_back(ROOT);
    second_show.push_back(2);
    expected_shows.push_back(first_show);
    expected_shows.push_back(second_show);
    CHECK(same(delta->shows, expected_shows));

    Array expected_hides;
    Array only_hide;
    only_hide.push_back(ROOT);
    only_hide.push_back(0);
    expected_hides.push_back(only_hide);
    CHECK(same(delta->hides, expected_hides));

    // A size assertion is not a guard, so the row read below gets a real one.
    NETW_CHECK_EQ(delta->keys.size(), 1);
    if (delta->keys.size() != 1) {
        return;
    }
    const int changed = NetwInterestBitSet::popcount(
        NetwInterestBitSet::symmetric_difference(
            PackedInt64Array(delta->old_rows[0]),
            PackedInt64Array(delta->new_rows[0])
        )
    );
    NETW_CHECK_EQ(changed, 3);
}

TEST_CASE(
    "[Networked][Interest][Hosted] hides run child-first and shows "
    "parent-first"
) {
    const Ref<NetwInterestEngine> engine = chain_engine();
    engine->set_membership(ROOT, names({"test"}));
    engine->set_membership(CHILD, names({"test"}));
    engine->set_membership(LEAF, names({"test"}));
    engine->set_layer(StringName("test"), bits({0}), OUTSIDERS);
    commit(engine);

    engine->set_layer(StringName("test"), PackedInt64Array(), OUTSIDERS);
    const Ref<NetwInterestDelta> hiding = engine->recompute();
    Array expected_hides;
    for (const int64_t key : {LEAF, CHILD, ROOT}) {
        Array entry;
        entry.push_back(key);
        entry.push_back(0);
        expected_hides.push_back(entry);
    }
    CHECK(same(hiding->hides, expected_hides));

    engine->commit(hiding);
    engine->set_layer(StringName("test"), bits({0}), OUTSIDERS);
    const Ref<NetwInterestDelta> showing = engine->recompute();
    Array expected_shows;
    for (const int64_t key : {ROOT, CHILD, LEAF}) {
        Array entry;
        entry.push_back(key);
        entry.push_back(0);
        expected_shows.push_back(entry);
    }
    CHECK(same(showing->shows, expected_shows));
}

// The grant one entity is owed, computed from the model's own bits rather than
// from the engine. An oracle that consults the engine proves nothing.
bool oracle_grant(
    int p_entity,
    int p_peer_bit,
    int p_policy_bits,
    int p_viewer_bits,
    int p_membership_bits
) {
    bool has_membership = false;
    bool admitted = false;
    for (int layer = 0; layer < 2; ++layer) {
        if ((p_membership_bits & (1 << (p_entity * 2 + layer))) == 0) {
            continue;
        }
        has_membership = true;
        const bool viewer
            = (p_viewer_bits & (1 << (layer * 2 + p_peer_bit))) != 0;
        const bool outsiders = ((p_policy_bits >> layer) & 1) == 0;
        admitted = admitted || (outsiders ? viewer : !viewer);
    }
    return has_membership ? admitted : true;
}

Ref<NetwInterestEngine> small_model_engine(
    int p_policy_bits,
    int p_viewer_bits,
    int p_membership_bits
) {
    Ref<NetwInterestEngine> engine = fresh();
    engine->set_live_peers(bits({0, 1}));
    engine->set_order_key(ROOT, 0, 1);
    engine->set_order_key(CHILD, 1, 2);
    engine->set_parent(CHILD, ROOT);
    for (int layer = 0; layer < 2; ++layer) {
        PackedInt64Array viewers;
        for (int peer_bit = 0; peer_bit < 2; ++peer_bit) {
            if ((p_viewer_bits & (1 << (layer * 2 + peer_bit))) != 0) {
                viewers = NetwInterestBitSet::with_bit(viewers, peer_bit, true);
            }
        }
        engine->set_layer(
            layer_id(layer),
            viewers,
            ((p_policy_bits >> layer) & 1) == 0 ? OUTSIDERS : INSIDERS
        );
    }
    for (int entity = 0; entity < 2; ++entity) {
        Array memberships;
        for (int layer = 0; layer < 2; ++layer) {
            if ((p_membership_bits & (1 << (entity * 2 + layer))) != 0) {
                memberships.push_back(layer_id(layer));
            }
        }
        engine->set_membership(entity == 0 ? ROOT : CHILD, memberships);
    }
    return engine;
}

TEST_CASE(
    "[Networked][Interest][Hosted] the oracle agrees on every state of "
    "the small model"
) {
    int failures = 0;
    for (int policy_bits = 0; policy_bits < 4; ++policy_bits) {
        for (int viewer_bits = 0; viewer_bits < 16; ++viewer_bits) {
            for (int membership_bits = 0; membership_bits < 16;
                 ++membership_bits) {
                const Ref<NetwInterestEngine> engine = small_model_engine(
                    policy_bits,
                    viewer_bits,
                    membership_bits
                );
                commit(engine);
                for (int entity = 0; entity < 2; ++entity) {
                    for (int peer_bit = 0; peer_bit < 2; ++peer_bit) {
                        bool expected = oracle_grant(
                            entity,
                            peer_bit,
                            policy_bits,
                            viewer_bits,
                            membership_bits
                        );
                        if (entity == 1) {
                            expected = expected && engine->test(ROOT, peer_bit);
                        }
                        const int64_t key = entity == 0 ? ROOT : CHILD;
                        if (engine->test(key, peer_bit) != expected) {
                            failures += 1;
                        }
                    }
                }
            }
        }
    }
    NETW_CHECK_EQ(failures, 0);
}

TEST_CASE(
    "[Networked][Interest][Hosted] a replayed script produces an "
    "identical delta, matrix and counters"
) {
    const Ref<NetwInterestEngine> first = scripted_engine(false);
    const Ref<NetwInterestEngine> second = scripted_engine(false);
    const Ref<NetwInterestDelta> first_delta = first->recompute();
    const Ref<NetwInterestDelta> second_delta = second->recompute();
    first->commit(first_delta);
    second->commit(second_delta);

    CHECK(same(first_delta->to_array(), second_delta->to_array()));
    CHECK(same(first->rows(), second->rows()));
    CHECK(bool(first->stats()->to_array() == second->stats()->to_array()));
}

Ref<NetwInterestEngine> permuted_engine(
    std::initializer_list<int64_t> p_keys,
    std::initializer_list<const char *> p_layers
) {
    Ref<NetwInterestEngine> engine = fresh();
    engine->set_live_peers(bits({0, 1}));
    for (const char *id : p_layers) {
        const StringName name(id);
        engine->set_layer(
            name,
            name == StringName("near") ? bits({0}) : bits({1}),
            OUTSIDERS
        );
    }
    for (const int64_t key : p_keys) {
        const int depth = key == ROOT ? 0 : (key == CHILD ? 1 : 2);
        engine->set_order_key(key, depth, depth + 1);
    }
    engine->set_parent(CHILD, ROOT);
    engine->set_parent(LEAF, CHILD);
    engine->set_membership(ROOT, names({"near"}));
    engine->set_membership(CHILD, names({"near", "far"}));
    engine->set_membership(LEAF, names({"far"}));
    return engine;
}

TEST_CASE(
    "[Networked][Interest][Hosted] registration and layer order do "
    "not change the output"
) {
    const Ref<NetwInterestEngine> first
        = permuted_engine({ROOT, CHILD, LEAF}, {"near", "far"});
    const Ref<NetwInterestEngine> second
        = permuted_engine({LEAF, ROOT, CHILD}, {"far", "near"});

    CHECK(
        same(first->recompute()->to_array(), second->recompute()->to_array())
    );
}

TEST_CASE(
    "[Networked][Interest][Hosted] the null slot is refused rather "
    "than stored"
) {
    const Ref<NetwInterestEngine> engine = fresh();
    engine->set_live_peers(bits({0, 1}));

    // Zero is what `set_parent` spells "no parent" with, so an entity stored
    // there could never clamp anybody and could never be clamped. Every
    // mutation refuses it rather than storing a record no clamp can reach.
    ERR_PRINT_OFF;
    engine->set_membership(0, names({"near"}));
    engine->set_parent(0, ROOT);
    engine->set_intent(0, bits({0}));
    engine->set_intent_all(0);
    engine->set_order_key(0, 0, 1);
    engine->remove_entity(0);
    ERR_PRINT_ON;

    CHECK(!engine->has_entity(0));
    commit(engine);
    NETW_CHECK_EQ(engine->rows().size(), 0);
}

TEST_CASE(
    "[Networked][Interest][Hosted] entities sharing an order key still "
    "order deterministically"
) {
    // Two entities can share a (depth, route), and the table they are stored
    // in has no order of its own to fall back on. The slot breaks the tie, so
    // the order is total rather than merely usually-total.
    const Ref<NetwInterestEngine> engine = fresh();
    engine->set_live_peers(bits({0}));
    engine->set_layer(StringName("arena"), bits({0}), OUTSIDERS);
    for (const int64_t key : {LEAF, ROOT, CHILD}) {
        engine->set_order_key(key, 0, 1);
        engine->set_membership(key, names({"arena"}));
    }
    const Ref<NetwInterestDelta> shows = engine->recompute();

    Array expected;
    for (const int64_t key : {ROOT, CHILD, LEAF}) {
        Array entry;
        entry.push_back(key);
        entry.push_back(0);
        expected.push_back(entry);
    }
    CHECK(same(shows->shows, expected));
}

} // namespace TestNetwInterestEngine
