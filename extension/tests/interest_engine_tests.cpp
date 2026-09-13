#include "support/netw_test.h"

#include "godot/templates.hpp"
#include "godot/utility.hpp"
#include "netw/interest/engine.hpp"

namespace TestEngine {

using namespace godot;
using netw::interest::BitSet;
using netw::interest::Decl;
using netw::interest::Delta;
using netw::interest::Engine;

constexpr int64_t ROOT = 1;
constexpr int64_t CHILD = 2;
constexpr int64_t LEAF = 3;

constexpr Engine::Policy OUTSIDERS = Engine::HIDE_FROM_OUTSIDERS;
constexpr Engine::Policy INSIDERS = Engine::HIDE_FROM_INSIDERS;

PackedInt64Array bits(std::initializer_list<int> p_bits) {
    PackedInt64Array out;
    for (const int bit : p_bits) {
        out = BitSet::with_bit(out, bit, true);
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

void check_line(const String &p_produced, const char *p_expected) {
    NETW_FORMAT_TEXT(produced, p_produced.utf8().get_data());
    NETW_FORMAT_TEXT(expected, p_expected);
    CAPTURE(produced);
    CAPTURE(expected);
    const bool matches = p_produced == String(p_expected);
    CHECK(matches);
}

Engine fresh() {
    Engine engine;
    return engine;
}

void commit(Engine &p_engine) {
    p_engine.commit(p_engine.recompute());
}

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
    return BitSet::popcount(BitSet::subtract(p_left, p_right));
}

Engine chain_engine() {
    Engine engine = fresh();
    engine.set_live_peers(bits({0, 1, 2}));
    engine.set_order_key(ROOT, 0, 1);
    engine.set_order_key(CHILD, 1, 2);
    engine.set_order_key(LEAF, 2, 3);
    engine.set_parent(CHILD, ROOT);
    engine.set_parent(LEAF, CHILD);
    return engine;
}

Engine configured_engine() {
    Engine engine = chain_engine();
    engine.set_layer(StringName("near"), bits({0, 2}), OUTSIDERS);
    engine.set_layer(StringName("blind"), bits({2}), INSIDERS);
    engine.set_membership(ROOT, names({"near"}));
    engine.set_membership(CHILD, names({"blind"}));
    engine.set_membership(LEAF, Array());
    engine.set_intent(CHILD, bits({0, 1}));
    return engine;
}

Engine scripted_engine(bool p_reverse) {
    Engine engine = fresh();
    engine.set_live_peers(bits({0, 1, 2}));
    const int64_t keys[3] = {ROOT, CHILD, LEAF};
    for (int step = 0; step < 3; ++step) {
        const int at = p_reverse ? 2 - step : step;
        engine.set_order_key(keys[at], at, at + 1);
    }
    for (int step = 0; step < 2; ++step) {
        const int at = p_reverse ? 1 - step : step;
        if (at == 0) {
            engine.set_layer(StringName("near"), bits({0, 2}), OUTSIDERS);
        } else {
            engine.set_layer(StringName("blind"), bits({2}), INSIDERS);
        }
    }
    engine.set_parent(CHILD, ROOT);
    engine.set_parent(LEAF, CHILD);
    engine.set_membership(ROOT, names({"near"}));
    engine.set_membership(CHILD, names({"blind"}));
    engine.set_membership(LEAF, Array());
    engine.set_intent(CHILD, bits({0, 1}));
    return engine;
}

Engine single_entity_engine(Engine::Policy p_policy) {
    Engine engine = fresh();
    engine.set_live_peers(bits({0, 1, 2}));
    engine.set_layer(StringName("test"), PackedInt64Array(), p_policy);
    engine.set_membership(ROOT, names({"test"}));
    engine.set_order_key(ROOT, 0, 1);
    return engine;
}

StringName layer_id(int p_index) {
    return StringName(p_index == 0 ? "a" : "b");
}

PackedInt64Array random_bits() {
    PackedInt64Array out;
    for (int bit = 0; bit < 3; ++bit) {
        if (netw::gd::randi_range(0, 1) == 1) {
            out = BitSet::with_bit(out, bit, true);
        }
    }
    return out;
}

Engine random_state_engine(int p_seed, bool p_reverse) {
    netw::gd::seed(p_seed);
    PackedInt64Array layer_viewers[2];
    Engine::Policy policies[2];
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

    Engine engine = fresh();
    engine.set_live_peers(bits({0, 1, 2}));
    for (int step = 0; step < 2; ++step) {
        const int at = p_reverse ? 1 - step : step;
        engine.set_layer(layer_id(at), layer_viewers[at], policies[at]);
    }
    const int64_t keys[3] = {ROOT, CHILD, LEAF};
    for (int step = 0; step < 3; ++step) {
        const int at = p_reverse ? 2 - step : step;
        engine.set_order_key(keys[at], at, at + 1);
        engine.set_membership(keys[at], memberships[at]);
        engine.set_intent(keys[at], intents[at]);
    }
    engine.set_parent(CHILD, ROOT);
    engine.set_parent(LEAF, CHILD);
    return engine;
}

TEST_CASE("[Networked][Interest][Hosted] the same state yields the same rows") {
    Engine first = configured_engine();
    Engine second = configured_engine();
    Delta first_delta = first.recompute();
    Delta second_delta = second.recompute();

    CHECK(same(first_delta.to_array(), second_delta.to_array()));
    first.commit(first_delta);
    second.commit(second_delta);
    CHECK(same(first.rows(), second.rows()));
}

TEST_CASE(
    "[Networked][Interest][Hosted] mutation order does not move the "
    "matrix"
) {
    Engine forward = scripted_engine(false);
    Engine reverse = scripted_engine(true);

    CHECK(same(forward.recompute().to_array(), reverse.recompute().to_array()));
}

TEST_CASE(
    "[Networked][Interest][Hosted] seeded states survive a permuted "
    "replay"
) {
    int diverged = -1;
    for (int seed = 0; seed < 64 && diverged < 0; ++seed) {
        Engine forward = random_state_engine(seed, false);
        Engine reverse = random_state_engine(seed, true);
        if (!same(
                forward.recompute().to_array(),
                reverse.recompute().to_array()
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
    Engine engine = configured_engine();
    engine.commit(engine.recompute());
    engine.set_membership(ROOT, names({"near"}));
    engine.set_layer(StringName("near"), bits({0, 2}), OUTSIDERS);
    Delta second = engine.recompute();

    CHECK(second.is_empty());
    engine.commit(second);
    CHECK(engine.recompute().is_empty());
}

TEST_CASE(
    "[Networked][Interest][Hosted] more viewers only ever grants more peers"
) {
    Engine engine = single_entity_engine(OUTSIDERS);
    engine.set_layer(StringName("test"), bits({0}), OUTSIDERS);
    commit(engine);
    const PackedInt64Array smaller = engine.row_of(ROOT);
    engine.set_layer(StringName("test"), bits({0, 1}), OUTSIDERS);
    commit(engine);
    const PackedInt64Array larger = engine.row_of(ROOT);

    NETW_CHECK_EQ(missing_bits(smaller, larger), 0);
    CHECK(BitSet::test(larger, 1));
}

TEST_CASE(
    "[Networked][Interest][Hosted] more viewers only ever hides from more peers"
) {
    Engine engine = single_entity_engine(INSIDERS);
    engine.set_layer(StringName("test"), bits({0}), INSIDERS);
    commit(engine);
    const PackedInt64Array larger = engine.row_of(ROOT);
    engine.set_layer(StringName("test"), bits({0, 1}), INSIDERS);
    commit(engine);
    const PackedInt64Array smaller = engine.row_of(ROOT);

    NETW_CHECK_EQ(missing_bits(smaller, larger), 0);
    CHECK(!BitSet::test(smaller, 1));
}

TEST_CASE(
    "[Networked][Interest][Hosted] an ancestor row always clamps its "
    "descendants"
) {
    Engine engine = configured_engine();
    commit(engine);

    NETW_CHECK_EQ(missing_bits(engine.row_of(CHILD), engine.row_of(ROOT)), 0);
    NETW_CHECK_EQ(missing_bits(engine.row_of(LEAF), engine.row_of(CHILD)), 0);
}

TEST_CASE(
    "[Networked][Interest][Hosted] an entity in no layer is granted by "
    "intent alone"
) {
    Engine engine = fresh();
    engine.set_live_peers(bits({0, 1, 2}));
    engine.set_membership(ROOT, Array());
    engine.set_order_key(ROOT, 0, 1);
    engine.set_intent(ROOT, bits({0, 2}));
    commit(engine);

    CHECK(bool(engine.row_of(ROOT) == bits({0, 2})));
}

TEST_CASE(
    "[Networked][Interest][Hosted] the delta names what changed and "
    "nothing else"
) {
    Engine engine = single_entity_engine(OUTSIDERS);
    engine.set_layer(StringName("test"), bits({0}), OUTSIDERS);
    commit(engine);
    engine.set_layer(StringName("test"), bits({1, 2}), OUTSIDERS);
    Delta delta = engine.recompute();

    Array expected_shows;
    Array first_show;
    first_show.push_back(ROOT);
    first_show.push_back(1);
    Array second_show;
    second_show.push_back(ROOT);
    second_show.push_back(2);
    expected_shows.push_back(first_show);
    expected_shows.push_back(second_show);
    CHECK(same(delta.shows, expected_shows));

    Array expected_hides;
    Array only_hide;
    only_hide.push_back(ROOT);
    only_hide.push_back(0);
    expected_hides.push_back(only_hide);
    CHECK(same(delta.hides, expected_hides));

    NETW_CHECK_EQ(delta.keys.size(), 1);
    if (delta.keys.size() != 1) {
        return;
    }
    const int changed = BitSet::popcount(
        BitSet::symmetric_difference(
            PackedInt64Array(delta.old_rows[0]),
            PackedInt64Array(delta.new_rows[0])
        )
    );
    NETW_CHECK_EQ(changed, 3);
}

TEST_CASE(
    "[Networked][Interest][Hosted] hides run child-first and shows "
    "parent-first"
) {
    Engine engine = chain_engine();
    engine.set_membership(ROOT, names({"test"}));
    engine.set_membership(CHILD, names({"test"}));
    engine.set_membership(LEAF, names({"test"}));
    engine.set_layer(StringName("test"), bits({0}), OUTSIDERS);
    commit(engine);

    engine.set_layer(StringName("test"), PackedInt64Array(), OUTSIDERS);
    Delta hiding = engine.recompute();
    Array expected_hides;
    for (const int64_t key : {LEAF, CHILD, ROOT}) {
        Array entry;
        entry.push_back(key);
        entry.push_back(0);
        expected_hides.push_back(entry);
    }
    CHECK(same(hiding.hides, expected_hides));

    engine.commit(hiding);
    engine.set_layer(StringName("test"), bits({0}), OUTSIDERS);
    Delta showing = engine.recompute();
    Array expected_shows;
    for (const int64_t key : {ROOT, CHILD, LEAF}) {
        Array entry;
        entry.push_back(key);
        entry.push_back(0);
        expected_shows.push_back(entry);
    }
    CHECK(same(showing.shows, expected_shows));
}

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

Engine small_model_engine(
    int p_policy_bits,
    int p_viewer_bits,
    int p_membership_bits
) {
    Engine engine = fresh();
    engine.set_live_peers(bits({0, 1}));
    engine.set_order_key(ROOT, 0, 1);
    engine.set_order_key(CHILD, 1, 2);
    engine.set_parent(CHILD, ROOT);
    for (int layer = 0; layer < 2; ++layer) {
        PackedInt64Array viewers;
        for (int peer_bit = 0; peer_bit < 2; ++peer_bit) {
            if ((p_viewer_bits & (1 << (layer * 2 + peer_bit))) != 0) {
                viewers = BitSet::with_bit(viewers, peer_bit, true);
            }
        }
        engine.set_layer(
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
        engine.set_membership(entity == 0 ? ROOT : CHILD, memberships);
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
                Engine engine = small_model_engine(
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
                            expected = expected && engine.test(ROOT, peer_bit);
                        }
                        const int64_t key = entity == 0 ? ROOT : CHILD;
                        if (engine.test(key, peer_bit) != expected) {
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
    Engine first = scripted_engine(false);
    Engine second = scripted_engine(false);
    Delta first_delta = first.recompute();
    Delta second_delta = second.recompute();
    first.commit(first_delta);
    second.commit(second_delta);

    CHECK(same(first_delta.to_array(), second_delta.to_array()));
    CHECK(same(first.rows(), second.rows()));
    CHECK(bool(first.stats().to_array() == second.stats().to_array()));
}

Engine permuted_engine(
    std::initializer_list<int64_t> p_keys,
    std::initializer_list<const char *> p_layers
) {
    Engine engine = fresh();
    engine.set_live_peers(bits({0, 1}));
    for (const char *id : p_layers) {
        const StringName name(id);
        engine.set_layer(
            name,
            name == StringName("near") ? bits({0}) : bits({1}),
            OUTSIDERS
        );
    }
    for (const int64_t key : p_keys) {
        const int depth = key == ROOT ? 0 : (key == CHILD ? 1 : 2);
        engine.set_order_key(key, depth, depth + 1);
    }
    engine.set_parent(CHILD, ROOT);
    engine.set_parent(LEAF, CHILD);
    engine.set_membership(ROOT, names({"near"}));
    engine.set_membership(CHILD, names({"near", "far"}));
    engine.set_membership(LEAF, names({"far"}));
    return engine;
}

TEST_CASE(
    "[Networked][Interest][Hosted] registration and layer order do "
    "not change the output"
) {
    Engine first = permuted_engine({ROOT, CHILD, LEAF}, {"near", "far"});
    Engine second = permuted_engine({LEAF, ROOT, CHILD}, {"far", "near"});

    CHECK(same(first.recompute().to_array(), second.recompute().to_array()));
}

TEST_CASE(
    "[Networked][Interest][Hosted] the null slot is refused rather "
    "than stored"
) {
    Engine engine = fresh();
    engine.set_live_peers(bits({0, 1}));

    ERR_PRINT_OFF;
    engine.set_membership(0, names({"near"}));
    engine.set_parent(0, ROOT);
    engine.set_intent(0, bits({0}));
    engine.set_intent_all(0);
    engine.set_order_key(0, 0, 1);
    engine.remove_entity(0);
    ERR_PRINT_ON;

    CHECK(!engine.has_entity(0));
    commit(engine);
    NETW_CHECK_EQ(engine.rows().size(), 0);
}

TEST_CASE(
    "[Networked][Interest][Hosted] entities sharing an order key still "
    "order deterministically"
) {
    Engine engine = fresh();
    engine.set_live_peers(bits({0}));
    engine.set_layer(StringName("arena"), bits({0}), OUTSIDERS);
    for (const int64_t key : {LEAF, ROOT, CHILD}) {
        engine.set_order_key(key, 0, 1);
        engine.set_membership(key, names({"arena"}));
    }
    Delta shows = engine.recompute();

    Array expected;
    for (const int64_t key : {ROOT, CHILD, LEAF}) {
        Array entry;
        entry.push_back(key);
        entry.push_back(0);
        expected.push_back(entry);
    }
    CHECK(same(shows.shows, expected));
}

TEST_CASE(
    "[Networked][Interest][Hosted] a layer's viewer set answers in peer ids "
    "and reports only real edges"
) {
    Engine engine = fresh();
    const StringName near("near");

    CHECK(engine.layer_add_viewer(near, 11));
    CHECK(!engine.layer_add_viewer(near, 11));
    CHECK(engine.layer_add_viewer(near, 4));
    CHECK(engine.layer_has_viewer(near, 11));
    CHECK(!engine.layer_has_viewer(near, 9));
    CHECK(!engine.layer_has_viewer(StringName("far"), 11));

    PackedInt64Array expected;
    expected.push_back(11);
    expected.push_back(4);
    CHECK(engine.layer_viewers(near) == expected);

    CHECK(engine.layer_remove_viewer(near, 11));
    CHECK(!engine.layer_remove_viewer(near, 11));
    CHECK(!engine.layer_remove_viewer(StringName("far"), 4));
    CHECK(!engine.layer_has_viewer(near, 11));
    NETW_CHECK_EQ(engine.layer_viewers(near).size(), 1);
}

TEST_CASE(
    "[Networked][Interest][Hosted] a viewer edge makes the layer's members "
    "owe a pass"
) {
    Engine engine = fresh();
    const StringName near("near");
    const int bit = engine.peer_bit_for(11);
    engine.set_live_peers(bits({bit}));
    engine.set_order_key(ROOT, 0, 1);
    engine.set_membership(ROOT, names({"near"}));
    commit(engine);
    CHECK(!engine.test(ROOT, bit));

    engine.layer_add_viewer(near, 11);
    commit(engine);
    CHECK(engine.test(ROOT, bit));

    engine.layer_remove_viewer(near, 11);
    commit(engine);
    CHECK(!engine.test(ROOT, bit));
}

TEST_CASE(
    "[Networked][Interest][Hosted] a layer's policy is what the verdict "
    "composes with, and replacing it reports the change"
) {
    Engine engine = fresh();
    const StringName blind("blind");
    const int bit = engine.peer_bit_for(11);
    engine.set_live_peers(bits({bit}));
    engine.set_order_key(ROOT, 0, 1);
    engine.set_membership(ROOT, names({"blind"}));
    engine.layer_add_viewer(blind, 11);
    commit(engine);
    CHECK(engine.test(ROOT, bit));

    NETW_CHECK_EQ(engine.layer_policy(blind), int(OUTSIDERS));
    CHECK(engine.layer_set_policy(blind, INSIDERS));
    CHECK(!engine.layer_set_policy(blind, INSIDERS));
    NETW_CHECK_EQ(engine.layer_policy(blind), int(INSIDERS));

    commit(engine);
    CHECK(!engine.test(ROOT, bit));
}

TEST_CASE(
    "[Networked][Interest][Hosted] one layer's verdict on one peer is its "
    "policy composed with viewer membership and nothing else"
) {
    Engine engine = fresh();
    const StringName sight("sight");
    engine.declare_layer(sight);

    CHECK_FALSE(engine.layer_admits(sight, 7));
    engine.layer_add_viewer(sight, 7);
    CHECK(engine.layer_admits(sight, 7));

    SUBCASE("the inside policy inverts it") {
        engine.layer_set_policy(sight, INSIDERS);
        CHECK_FALSE(engine.layer_admits(sight, 7));
    }

    SUBCASE("a peer the engine never minted a bit for is an outsider") {
        CHECK_FALSE(engine.layer_admits(sight, 9));
        engine.layer_set_policy(sight, INSIDERS);
        CHECK(engine.layer_admits(sight, 9));
    }

    SUBCASE("peer zero names no participant under either policy") {
        CHECK_FALSE(engine.layer_admits(sight, 0));
        engine.layer_set_policy(sight, INSIDERS);
        CHECK_FALSE(engine.layer_admits(sight, 0));
    }

    SUBCASE("the server peer is evaluated as an ordinary participant") {
        CHECK_FALSE(engine.layer_admits(sight, 1));
        engine.layer_add_viewer(sight, 1);
        CHECK(engine.layer_admits(sight, 1));
    }

    SUBCASE("an undeclared layer admits nobody") {
        CHECK_FALSE(engine.layer_admits(StringName("nowhere"), 7));
    }
}

TEST_CASE(
    "[Networked][Interest][Hosted] the layer explanation names the verdict, "
    "the membership and the policy that composed them"
) {
    Engine engine = fresh();
    const StringName sight("sight");
    engine.layer_add_viewer(sight, 7);

    check_line(
        engine.layer_explain(sight, 7),
        "ADMIT peer=7 in viewers under HIDE_FROM_OUTSIDERS"
    );
    check_line(
        engine.layer_explain(sight, 9),
        "REJECT peer=9 not in viewers under HIDE_FROM_OUTSIDERS"
    );

    engine.layer_set_policy(sight, INSIDERS);
    check_line(
        engine.layer_explain(sight, 7),
        "REJECT peer=7 in viewers under HIDE_FROM_INSIDERS"
    );
    check_line(
        engine.layer_explain(sight, 0),
        "REJECT peer=0 (no peer context)"
    );
}

TEST_CASE(
    "[Networked][Interest][Hosted] the dirty set is the engine's, and a "
    "commit that saw every mutation empties it"
) {
    Engine engine = fresh();
    const StringName near("near");
    engine.set_live_peers(bits({0}));
    engine.set_order_key(ROOT, 0, 1);
    engine.set_order_key(CHILD, 1, 2);
    engine.set_parent(CHILD, ROOT);

    NETW_CHECK_EQ(engine.dirty_count(), 2);

    commit(engine);
    NETW_CHECK_EQ(engine.dirty_count(), 0);

    engine.membership_add(ROOT, near);
    NETW_CHECK_EQ(engine.dirty_count(), 2);

    Delta stale = engine.recompute();
    engine.membership_add(CHILD, near);
    engine.commit(stale);
    NETW_CHECK_EQ(engine.dirty_count(), 2);

    commit(engine);
    NETW_CHECK_EQ(engine.dirty_count(), 0);
}

TEST_CASE(
    "[Networked][Interest][Hosted] a membership moves one layer at a time and "
    "keeps the order it joined in"
) {
    Engine engine = fresh();
    const StringName near("near");
    const StringName far("far");

    CHECK(!engine.has_memberships(ROOT));
    CHECK(engine.membership_add(ROOT, near));
    CHECK(!engine.membership_add(ROOT, near));
    CHECK(engine.membership_add(ROOT, far));

    Array expected;
    expected.push_back(near);
    expected.push_back(far);
    CHECK(engine.memberships(ROOT) == expected);
    CHECK(engine.has_memberships(ROOT));
    CHECK(engine.has_layer(near));

    CHECK(engine.membership_remove(ROOT, near));
    CHECK(!engine.membership_remove(ROOT, near));
    CHECK(!engine.membership_remove(CHILD, far));
    NETW_CHECK_EQ(engine.memberships(ROOT).size(), 1);

    CHECK(engine.membership_remove(ROOT, far));
    CHECK(!engine.has_memberships(ROOT));

    ERR_PRINT_OFF;
    CHECK(!engine.membership_add(0, near));
    CHECK(!engine.membership_add(ROOT, StringName()));
    ERR_PRINT_ON;
}

TEST_CASE(
    "[Networked][Interest][Hosted] a membership edge makes the entity owe a "
    "pass, and the keys name who is owed one"
) {
    Engine engine = fresh();
    const StringName empty("empty");
    const int bit = engine.peer_bit_for(11);
    engine.set_live_peers(bits({bit}));
    engine.set_order_key(ROOT, 0, 1);
    engine.declare_layer(empty);
    commit(engine);
    CHECK(engine.test(ROOT, bit));

    engine.membership_add(ROOT, empty);
    commit(engine);
    CHECK(!engine.test(ROOT, bit));

    PackedInt64Array expected;
    expected.push_back(ROOT);
    CHECK(engine.membership_keys() == expected);

    engine.membership_remove(ROOT, empty);
    commit(engine);
    CHECK(engine.test(ROOT, bit));
    NETW_CHECK_EQ(engine.membership_keys().size(), 0);
}

TEST_CASE(
    "[Networked][Interest][Hosted] an intent row is a different thing from "
    "the default of admitting every live peer"
) {
    Engine engine = fresh();
    engine.set_live_peers(bits({0, 1}));
    engine.set_order_key(ROOT, 0, 1);

    CHECK(!engine.has_intent(ROOT));
    NETW_CHECK_EQ(engine.intent_keys().size(), 0);

    engine.set_intent(ROOT, bits({0, 1}));
    CHECK(engine.has_intent(ROOT));

    PackedInt64Array expected;
    expected.push_back(ROOT);
    CHECK(engine.intent_keys() == expected);

    engine.set_intent_all(ROOT);
    CHECK(!engine.has_intent(ROOT));
    NETW_CHECK_EQ(engine.intent_keys().size(), 0);
}

TEST_CASE(
    "[Networked][Interest][Hosted] what the committed matrix was computed "
    "with is answered by the commit, not by intake"
) {
    Engine engine = fresh();
    engine.set_live_peers(bits({0, 1}));
    engine.set_order_key(ROOT, 0, 1);

    CHECK(!engine.had_committed_intent(ROOT));

    engine.set_intent(ROOT, bits({0}));
    CHECK(!engine.had_committed_intent(ROOT));

    commit(engine);
    CHECK(engine.had_committed_intent(ROOT));

    engine.set_intent_all(ROOT);
    CHECK(engine.had_committed_intent(ROOT));

    commit(engine);
    CHECK(!engine.had_committed_intent(ROOT));

    engine.set_intent(ROOT, bits({0}));
    commit(engine);
    CHECK(engine.had_committed_intent(ROOT));
    engine.clear();
    CHECK(!engine.had_committed_intent(ROOT));
}

TEST_CASE(
    "[Networked][Interest][Hosted] a layer's departure policies are declared "
    "on it and never composed into a row"
) {
    Engine engine = fresh();
    const StringName near("near");
    const int bit = engine.peer_bit_for(11);
    engine.set_live_peers(bits({bit}));
    engine.set_order_key(ROOT, 0, 1);
    engine.set_membership(ROOT, names({"near"}));
    engine.layer_add_viewer(near, 11);
    commit(engine);

    NETW_CHECK_EQ(engine.layer_leave_policy(near), 0);
    NETW_CHECK_EQ(engine.layer_perception_policy(near), 0);
    NETW_CHECK_EQ(engine.layer_leave_policy(StringName("nowhere")), 0);

    CHECK(engine.layer_set_leave_policy(near, 1));
    CHECK(!engine.layer_set_leave_policy(near, 1));
    CHECK(engine.layer_set_perception_policy(near, 2));

    NETW_CHECK_EQ(engine.layer_leave_policy(near), 1);
    NETW_CHECK_EQ(engine.layer_perception_policy(near), 2);

    commit(engine);
    CHECK(engine.test(ROOT, bit));

    ERR_PRINT_OFF;
    CHECK(!engine.layer_set_leave_policy(near, 3));
    CHECK(!engine.layer_set_perception_policy(near, -1));
    CHECK(!engine.layer_set_leave_policy(StringName(), 1));
    ERR_PRINT_ON;

    NETW_CHECK_EQ(engine.layer_leave_policy(near), 1);
    NETW_CHECK_EQ(engine.layer_perception_policy(near), 2);
}

TEST_CASE(
    "[Networked][Interest][Hosted] a layer's roster is its own book, not the "
    "memberships read backwards"
) {
    Engine engine = fresh();
    const StringName near("near");

    CHECK(engine.roster_add(near, ROOT));
    CHECK(!engine.roster_add(near, ROOT));
    CHECK(engine.roster_add(near, CHILD));
    CHECK(engine.roster_has(near, ROOT));
    CHECK(!engine.roster_has(near, LEAF));
    CHECK(!engine.roster_has(StringName("far"), ROOT));
    CHECK(engine.has_layer(near));

    CHECK(!engine.has_memberships(ROOT));
    engine.membership_add(LEAF, StringName("far"));
    CHECK(!engine.roster_has(StringName("far"), LEAF));

    PackedInt64Array expected;
    expected.push_back(ROOT);
    expected.push_back(CHILD);
    CHECK(engine.roster(near) == expected);

    CHECK(engine.roster_remove(near, ROOT));
    CHECK(!engine.roster_remove(near, ROOT));
    CHECK(!engine.roster_remove(StringName("nowhere"), CHILD));
    NETW_CHECK_EQ(engine.roster(near).size(), 1);

    ERR_PRINT_OFF;
    CHECK(!engine.roster_add(near, 0));
    CHECK(!engine.roster_add(StringName(), ROOT));
    ERR_PRINT_ON;

    engine.clear();
    NETW_CHECK_EQ(engine.roster(near).size(), 0);
}

TEST_CASE(
    "[Networked][Interest][Hosted] a layer counts the edges it reported, "
    "since it was declared"
) {
    Engine engine = fresh();
    const StringName near("near");

    NETW_CHECK_EQ(engine.transitions(near), 0);

    engine.note_transition(near);
    engine.note_transition(near);
    engine.note_transition(StringName("far"));

    NETW_CHECK_EQ(engine.transitions(near), 2);
    NETW_CHECK_EQ(engine.transitions(StringName("far")), 1);
    NETW_CHECK_EQ(engine.transitions(StringName("nowhere")), 0);

    engine.note_transition(StringName());
    NETW_CHECK_EQ(engine.transitions(StringName()), 0);

    engine.remove_layer(near);
    NETW_CHECK_EQ(engine.transitions(near), 0);
}

TEST_CASE(
    "[Networked][Interest][Hosted] a viewer bit that names no peer is not a "
    "viewer"
) {
    Engine engine = fresh();
    const StringName near("near");
    engine.set_layer(near, bits({0, 1}), OUTSIDERS);

    NETW_CHECK_EQ(engine.layer_viewers(near).size(), 0);
    NETW_CHECK_EQ(engine.viewer_peers().size(), 0);

    engine.peer_bit_for(11);
    PackedInt64Array expected;
    expected.push_back(11);
    CHECK(engine.layer_viewers(near) == expected);
}

TEST_CASE(
    "[Networked][Interest][Hosted] the viewer peers are the union across "
    "every layer"
) {
    Engine engine = fresh();
    engine.layer_add_viewer(StringName("near"), 11);
    engine.layer_add_viewer(StringName("near"), 4);
    engine.layer_add_viewer(StringName("far"), 4);
    engine.layer_add_viewer(StringName("far"), 7);

    PackedInt64Array expected;
    expected.push_back(11);
    expected.push_back(4);
    expected.push_back(7);
    CHECK(engine.viewer_peers() == expected);

    engine.remove_layer(StringName("far"));
    NETW_CHECK_EQ(engine.viewer_peers().size(), 2);
}

TEST_CASE(
    "[Networked][Interest][Hosted] a declared layer exists admitting nobody"
) {
    Engine engine = fresh();
    const StringName near("near");
    CHECK(!engine.has_layer(near));

    engine.declare_layer(near);

    CHECK(engine.has_layer(near));
    NETW_CHECK_EQ(engine.layer_viewers(near).size(), 0);
    CHECK(!engine.layer_has_viewer(near, 11));
}

TEST_CASE(
    "[Networked][Interest][Hosted] an entity with no route is ordered by an "
    "ordinal minted once"
) {
    Engine engine = fresh();

    NETW_CHECK_EQ(engine.order_route_for(ROOT), 1);
    NETW_CHECK_EQ(engine.order_route_for(CHILD), 2);
    NETW_CHECK_EQ(engine.order_route_for(ROOT), 1);

    engine.remove_entity(ROOT);
    NETW_CHECK_EQ(engine.order_route_for(ROOT), 1);

    ERR_PRINT_OFF;
    NETW_CHECK_EQ(engine.order_route_for(0), 0);
    ERR_PRINT_ON;

    engine.clear();
    NETW_CHECK_EQ(engine.order_route_for(CHILD), 1);
}

TEST_CASE(
    "[Networked][Interest][Hosted] a peer's bit is minted once and stays "
    "dense from zero"
) {
    Engine engine = fresh();

    NETW_CHECK_EQ(engine.peer_bit_for(7), 0);
    NETW_CHECK_EQ(engine.peer_bit_for(3), 1);
    NETW_CHECK_EQ(engine.peer_bit_for(7), 0);
    NETW_CHECK_EQ(engine.peer_bit_for(3), 1);
    NETW_CHECK_EQ(engine.peer_bit_for(9), 2);

    PackedInt64Array expected;
    expected.push_back(7);
    expected.push_back(3);
    expected.push_back(9);
    CHECK(engine.known_peers() == expected);
}

TEST_CASE(
    "[Networked][Interest][Hosted] a bit and its peer resolve back to each "
    "other, and nothing else does"
) {
    Engine engine = fresh();
    const int bit = engine.peer_bit_for(42);

    NETW_CHECK_EQ(engine.peer_of_bit(bit), 42);
    NETW_CHECK_EQ(engine.peer_of_bit(bit + 1), 0);
    NETW_CHECK_EQ(engine.peer_of_bit(-1), 0);
}

TEST_CASE(
    "[Networked][Interest][Hosted] reading a peer's bit does not enrol the "
    "peer"
) {
    Engine engine = fresh();

    NETW_CHECK_EQ(engine.peer_bit_of(5), -1);
    NETW_CHECK_EQ(engine.known_peers().size(), 0);
    NETW_CHECK_EQ(engine.peer_bit_for(5), 0);
    NETW_CHECK_EQ(engine.peer_bit_of(5), 0);
}

TEST_CASE(
    "[Networked][Interest][Hosted] peer id zero is refused a bit rather "
    "than given one"
) {
    Engine engine = fresh();

    ERR_PRINT_OFF;
    NETW_CHECK_EQ(engine.peer_bit_for(0), -1);
    ERR_PRINT_ON;

    NETW_CHECK_EQ(engine.known_peers().size(), 0);
    NETW_CHECK_EQ(engine.peer_bit_for(1), 0);
    NETW_CHECK_EQ(engine.peer_of_bit(0), 1);
}

TEST_CASE(
    "[Networked][Interest][Hosted] a cleared engine mints the next session's "
    "bits from zero again"
) {
    Engine engine = fresh();
    engine.peer_bit_for(7);
    engine.peer_bit_for(3);

    engine.clear();

    NETW_CHECK_EQ(engine.peer_bit_of(7), -1);
    NETW_CHECK_EQ(engine.peer_of_bit(0), 0);
    NETW_CHECK_EQ(engine.peer_bit_for(3), 0);
}

TEST_CASE(
    "[Networked][Interest][Hosted] a peer that computes no admission projects "
    "from what the entity declares"
) {
    Engine engine;
    Decl silent;
    Decl declared;
    declared.join(StringName("arena"));
    declared.join(StringName("stealth"));

    engine.declare_layer(StringName("arena"));
    engine.declare_layer(StringName("stealth"));

    CHECK(engine.projection_admits(1, &silent));
    CHECK_FALSE(engine.projection_admits(1, &declared));

    engine.roster_add(StringName("stealth"), 1);
    CHECK(engine.projection_admits(1, &declared));
    CHECK_FALSE(engine.projection_admits(2, &declared));

    engine.roster_remove(StringName("stealth"), 1);
    CHECK_FALSE(engine.projection_admits(1, &declared));
}

TEST_CASE(
    "[Networked][Interest][Hosted] a projection over a layer nobody declared "
    "admits nobody, and no declaration at all is refused"
) {
    Engine engine;
    Decl declared;
    declared.join(StringName("nowhere"));

    CHECK_FALSE(engine.projection_admits(1, &declared));

    ERR_PRINT_OFF;
    CHECK_FALSE(engine.projection_admits(1, nullptr));
    ERR_PRINT_ON;
}

TEST_CASE(
    "[Networked][Interest][Hosted] sharing a scope is every enrolled entity "
    "but the asker, named once"
) {
    Engine engine;
    engine.declare_layer(StringName("arena"));
    engine.declare_layer(StringName("stealth"));
    engine.roster_add(StringName("arena"), 1);
    engine.roster_add(StringName("arena"), 2);
    engine.roster_add(StringName("stealth"), 2);
    engine.roster_add(StringName("stealth"), 3);

    Array both;
    both.push_back(StringName("arena"));
    both.push_back(StringName("stealth"));

    PackedInt64Array expected;
    expected.push_back(1);
    expected.push_back(3);
    CHECK(engine.co_members(2, both) == expected);

    PackedInt64Array from_one;
    from_one.push_back(2);
    from_one.push_back(3);
    CHECK(engine.co_members(1, both) == from_one);

    Array unknown;
    unknown.push_back(StringName("nowhere"));
    NETW_CHECK_EQ(engine.co_members(1, unknown).size(), 0);
    NETW_CHECK_EQ(engine.co_members(1, Array()).size(), 0);
}

TEST_CASE(
    "[Networked][Interest][Hosted] the churn of the plane is every layer's, "
    "and a layer nothing reported for contributes nothing"
) {
    Engine engine;
    engine.declare_layer(StringName("arena"));
    engine.declare_layer(StringName("stealth"));
    engine.declare_layer(StringName("quiet"));

    NETW_CHECK_EQ(engine.transitions_total(), 0);

    engine.note_transition(StringName("arena"));
    engine.note_transition(StringName("arena"));
    engine.note_transition(StringName("stealth"));

    NETW_CHECK_EQ(engine.transitions_total(), 3);
    NETW_CHECK_EQ(engine.transitions(StringName("quiet")), 0);
}

TEST_CASE(
    "[Networked][Interest][Hosted] a scene membership moves once per move, and "
    "an empty name forgets the entity"
) {
    Engine engine;

    CHECK(engine.scene_membership(1) == StringName());
    CHECK_FALSE(engine.set_scene_membership(1, StringName()));

    CHECK(engine.set_scene_membership(1, StringName("level-1")));
    CHECK(engine.scene_membership(1) == StringName("level-1"));
    CHECK_FALSE(engine.set_scene_membership(1, StringName("level-1")));

    CHECK(engine.set_scene_membership(1, StringName("level-2")));
    CHECK(engine.scene_membership(1) == StringName("level-2"));
    CHECK(engine.scene_membership(2) == StringName());

    CHECK(engine.set_scene_membership(1, StringName()));
    CHECK(engine.scene_membership(1) == StringName());
    CHECK_FALSE(engine.set_scene_membership(1, StringName()));
}

TEST_CASE(
    "[Networked][Interest][Hosted] the scene memberships end with the session, "
    "so a second one starts naming nothing"
) {
    Engine engine;
    engine.set_scene_membership(1, StringName("level-1"));
    engine.set_scene_membership(2, StringName("level-2"));

    engine.clear();

    CHECK(engine.scene_membership(1) == StringName());
    CHECK(engine.scene_membership(2) == StringName());
}

TEST_CASE(
    "[Networked][Interest][Hosted] an entity's departure is watched through "
    "exactly one handler, handed back to disconnect it"
) {
    Engine engine;
    Ref<RefCounted> holder;
    holder.instantiate();
    const Callable first(holder.ptr(), StringName("get_class"));
    const Callable second(holder.ptr(), StringName("get_instance_id"));

    CHECK_FALSE(engine.exit_handler(1).is_valid());

    CHECK(engine.set_exit_handler(1, first));
    CHECK(engine.exit_handler(1) == first);
    CHECK_FALSE(engine.set_exit_handler(1, second));
    CHECK(engine.exit_handler(1) == first);

    CHECK(engine.take_exit_handler(1) == first);
    CHECK_FALSE(engine.exit_handler(1).is_valid());
    CHECK_FALSE(engine.take_exit_handler(1).is_valid());

    CHECK(engine.set_exit_handler(1, second));
    engine.clear();
    CHECK_FALSE(engine.exit_handler(1).is_valid());
}

TEST_CASE(
    "[Networked][Interest][Hosted] the committed row answers as peer ids, so "
    "no caller outside the engine has to know the bit numbering"
) {
    Engine engine = fresh();
    engine.set_layer(StringName("arena"), bits({0, 1}), OUTSIDERS);
    engine.set_membership(ROOT, names({"arena"}));
    engine.set_live_peer_ids(PackedInt64Array({11, 22}));
    commit(engine);

    const PackedInt64Array admitted = engine.admitted_peers(ROOT);

    NETW_CHECK_EQ(admitted.size(), 2);
    CHECK(admitted.has(11));
    CHECK(admitted.has(22));
    NETW_CHECK_EQ(engine.admitted_peers(LEAF).size(), 0);
}

TEST_CASE(
    "[Networked][Interest][Hosted] a bit that names no peer is dropped from "
    "the admitted ids rather than reported as peer zero"
) {
    Engine engine = fresh();
    engine.set_layer(StringName("arena"), bits({0, 1, 2}), OUTSIDERS);
    engine.set_membership(ROOT, names({"arena"}));
    engine.set_live_peers(bits({0, 1, 2}));
    commit(engine);

    const PackedInt64Array admitted = engine.admitted_peers(ROOT);

    NETW_CHECK_EQ(admitted.size(), 0);
    CHECK_FALSE(admitted.has(0));
}

TEST_CASE(
    "[Networked][Interest][Hosted] live peers stated as ids mint the bits the "
    "engine numbers them by, and report the minting"
) {
    Engine engine = fresh();

    CHECK(engine.set_live_peer_ids(PackedInt64Array({11, 22})));
    NETW_CHECK_EQ(engine.peer_bit_of(11), 0);
    NETW_CHECK_EQ(engine.peer_bit_of(22), 1);

    CHECK_FALSE(engine.set_live_peer_ids(PackedInt64Array({22, 11})));

    CHECK(engine.set_live_peer_ids(PackedInt64Array({11, 22, 33})));
    NETW_CHECK_EQ(engine.peer_bit_of(33), 2);
}

TEST_CASE(
    "[Networked][Interest][Hosted] a live peer id of zero is refused, and the "
    "peers beside it are still numbered"
) {
    Engine engine = fresh();
    ERR_PRINT_OFF;

    CHECK(engine.set_live_peer_ids(PackedInt64Array({11, 0, 22})));

    ERR_PRINT_ON;
    NETW_CHECK_EQ(engine.peer_bit_of(0), -1);
    NETW_CHECK_EQ(engine.peer_bit_of(11), 0);
    NETW_CHECK_EQ(engine.peer_bit_of(22), 1);
    NETW_CHECK_EQ(engine.known_peers().size(), 2);
}

TEST_CASE(
    "[Networked][Interest][Hosted] intent stated as peer ids is the mask the "
    "engine would have composed from the same ids"
) {
    Engine engine = fresh();
    engine.set_live_peer_ids(PackedInt64Array({11, 22, 33}));

    engine.set_intent_for_peers(ROOT, PackedInt64Array({11, 33}));
    engine.set_intent(
        CHILD,
        bits({engine.peer_bit_of(11), engine.peer_bit_of(33)})
    );
    commit(engine);

    CHECK(BitSet::equals(engine.row_of(ROOT), engine.row_of(CHILD)));
    NETW_CHECK_EQ(engine.admitted_peers(ROOT).size(), 2);
    CHECK(engine.admitted_peers(ROOT).has(11));
    CHECK(engine.admitted_peers(ROOT).has(33));
    CHECK_FALSE(engine.admitted_peers(ROOT).has(22));
}

TEST_CASE(
    "[Networked][Interest][Hosted] intent by peer id mints a bit for a peer "
    "the engine has never numbered, and refuses peer zero"
) {
    Engine engine = fresh();
    ERR_PRINT_OFF;

    engine.set_intent_for_peers(ROOT, PackedInt64Array({44, 0}));
    engine.set_intent_for_peers(0, PackedInt64Array({44}));

    ERR_PRINT_ON;
    NETW_CHECK_EQ(engine.peer_bit_of(44), 0);
    NETW_CHECK_EQ(engine.peer_bit_of(0), -1);
}

} // namespace TestEngine
