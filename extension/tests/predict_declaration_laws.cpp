#include "support/netw_test.h"

#include "netw/predict/engine.hpp"
#include "netw/predict/journal.hpp"
#include "netw/quantize.hpp"

namespace TestNetwPredictDeclarationLaws {

using namespace godot;
using namespace netw;
using namespace netw::predict;

Ref<NetwPredictionEngine> pool() {
    Ref<NetwPredictionEngine> out;
    out.instantiate();
    return out;
}

Ref<NetwPredictDeclaration> declaration(const PackedStringArray &p_keys) {
    Ref<NetwPredictDeclaration> out;
    out.instantiate();
    for (int at = 0; at < p_keys.size(); ++at) {
        out->append_field(
            StringName(p_keys[at]),
            int(PropertyClass::CAUSAL),
            StringName(),
            0.0,
            false,
            false,
            -1.0,
            -1.0,
            false
        );
    }
    return out;
}

PackedStringArray keys() {
    PackedStringArray out;
    out.push_back("velocity");
    out.push_back("angle");
    out.push_back("basis");
    return out;
}

int32_t fingerprint(const char *p_text) {
    const PackedByteArray bytes = String(p_text).to_utf8_buffer();
    return fnv1a(bytes.ptr(), bytes.size());
}

TEST_CASE(
    "[Networked][Predict][Hosted][Declaration] a slot is minted once and "
    "never handed out again"
) {
    const Ref<NetwPredictionEngine> engine = pool();
    const Ref<NetwPredictDeclaration> declared = declaration(keys());
    const int64_t first = engine->open(declared);
    const int64_t second = engine->open(declared);

    CHECK(first > 0);
    CHECK(second != first);
    NETW_CHECK_EQ(engine->open_count(), 2);
    CHECK(engine->is_open(first));

    engine->close(first);
    CHECK_FALSE(engine->is_open(first));
    NETW_CHECK_EQ(engine->open_count(), 1);
    CHECK(engine->open(declared) != first);
}

TEST_CASE(
    "[Networked][Predict][Hosted][Declaration] the field table is the "
    "declaration order and answers both ways"
) {
    const Ref<NetwPredictionEngine> engine = pool();
    const PackedStringArray declared = keys();
    const int64_t slot = engine->open(declaration(declared));

    NETW_CHECK_EQ(engine->field_count(slot), declared.size());
    for (int at = 0; at < declared.size(); ++at) {
        const StringName key = StringName(declared[at]);
        CHECK(engine->field_name(slot, at) == key);
        NETW_CHECK_EQ(engine->field_slot(slot, key), at);
    }
    NETW_CHECK_EQ(engine->field_slot(slot, StringName("never_declared")), -1);
}

TEST_CASE(
    "[Networked][Predict][Hosted][Declaration] a rewire onto nothing leaves "
    "no table standing"
) {
    const Ref<NetwPredictionEngine> engine = pool();
    const int64_t slot = engine->open(declaration(keys()));
    NETW_CHECK_EQ(engine->field_count(slot), 3);

    Ref<NetwPredictDeclaration> empty;
    empty.instantiate();
    engine->rewire(slot, empty);

    NETW_CHECK_EQ(engine->field_count(slot), 0);
    NETW_CHECK_EQ(engine->field_slot(slot, StringName("velocity")), -1);
}

TEST_CASE(
    "[Networked][Predict][Hosted][Declaration] a table is per slot rather "
    "than per pool"
) {
    const Ref<NetwPredictionEngine> engine = pool();
    PackedStringArray other;
    other.push_back("depth");
    const int64_t wide = engine->open(declaration(keys()));
    const int64_t narrow = engine->open(declaration(other));

    NETW_CHECK_EQ(engine->field_count(wide), 3);
    NETW_CHECK_EQ(engine->field_count(narrow), 1);
    NETW_CHECK_EQ(engine->field_slot(narrow, StringName("velocity")), -1);
    NETW_CHECK_EQ(engine->field_slot(wide, StringName("depth")), -1);
}

TEST_CASE(
    "[Networked][Predict][Hosted][Declaration] the fingerprint is stable, "
    "sensitive and inside a signed 32 bit column"
) {
    NETW_CHECK_EQ(fingerprint("a declared payload"), fingerprint(
        "a declared payload"
    ));
    CHECK(fingerprint("a declared payload") != fingerprint(
        "a declared payloae"
    ));
    NETW_CHECK_EQ(fnv1a(nullptr, 0), fnv1a(nullptr, 0));

    const PackedByteArray wide = String("0123456789abcdef").to_utf8_buffer();
    const int32_t folded = fnv1a(wide.ptr(), wide.size());
    NETW_CHECK_EQ(int64_t(folded), int64_t(int32_t(folded)));
}

Ref<NetwQuantizeFixed> coarse() {
    Ref<NetwQuantizeFixed> out;
    out.instantiate();
    out->set_resolution_step(0.5);
    out->set_min_limit(-16.0);
    out->set_max_limit(16.0);
    return out;
}

Ref<NetwPredictDeclaration> quantized_declaration() {
    Ref<NetwPredictDeclaration> out;
    out.instantiate();
    out->append_field(
        StringName("speed"),
        int(PropertyClass::CAUSAL),
        StringName(),
        0.0,
        false,
        false,
        -1.0,
        -1.0,
        false,
        coarse(),
        int(Variant::FLOAT)
    );
    return out;
}

TEST_CASE(
    "[Networked][Predict][Hosted][Declaration] a declared value is recorded "
    "as what survived its own codec"
) {
    const Ref<NetwPredictionEngine> engine = pool();
    const int64_t slot = engine->open(quantized_declaration());
    engine->rewire(slot, quantized_declaration());
    Dictionary live;
    live[StringName("speed")] = 1.3;

    const Dictionary canonical = engine->canonicalize_state(slot, live);

    CHECK(canonical.has(StringName("speed")));
    NETW_CHECK_ORDER(double(canonical[StringName("speed")]), 1.3, !=);
    NETW_CHECK_CLOSE(double(canonical[StringName("speed")]), 1.5, 0.001);
    NETW_CHECK_EQ(
        int64_t(engine->canonicalize_state(slot, canonical).size()),
        int64_t(1)
    );
    CHECK(
        double(engine->canonicalize_state(slot, canonical)[
            StringName("speed")
        ]) == double(canonical[StringName("speed")])
    );
}

TEST_CASE(
    "[Networked][Predict][Hosted][Declaration] an undeclared field crosses "
    "the canonical form untouched"
) {
    const Ref<NetwPredictionEngine> engine = pool();
    const int64_t slot = engine->open(quantized_declaration());
    engine->rewire(slot, quantized_declaration());
    Dictionary live;
    live[StringName("speed")] = 1.3;
    live[StringName("unnamed")] = 1.3;

    const Dictionary canonical = engine->canonicalize_state(slot, live);

    NETW_CHECK_CLOSE(double(canonical[StringName("unnamed")]), 1.3, 0.0);
}

TEST_CASE(
    "[Networked][Predict][Hosted][Declaration] the bytes are the fields the "
    "payload names, in declaration order"
) {
    const Ref<NetwPredictionEngine> engine = pool();
    const int64_t slot = engine->open(quantized_declaration());
    engine->rewire(slot, quantized_declaration());
    Dictionary live;
    live[StringName("speed")] = 1.3;
    Dictionary same;
    same[StringName("speed")] = 1.5;

    const PackedByteArray bytes = engine->canonical_state_bytes(slot, live);

    CHECK_FALSE(bytes.is_empty());
    CHECK(bytes == engine->canonical_state_bytes(slot, same));
    CHECK(
        engine->canonical_state_bytes(slot, Dictionary()) == PackedByteArray()
    );
}

TEST_CASE(
    "[Networked][Predict][Hosted][Declaration] the input codec is the input "
    "declaration's, not the state's"
) {
    const Ref<NetwPredictionEngine> engine = pool();
    const int64_t slot = engine->open(declaration(keys()));
    Ref<NetwPredictDeclaration> input;
    input.instantiate();
    input->append_field(
        StringName("throttle"),
        int(PropertyClass::CAUSAL),
        StringName(),
        0.0,
        false,
        false,
        -1.0,
        -1.0,
        false,
        coarse(),
        int(Variant::FLOAT)
    );
    engine->rewire(slot, declaration(keys()), input);
    Dictionary command;
    command[StringName("throttle")] = 1.3;

    NETW_CHECK_CLOSE(
        double(engine->canonicalize_input(slot, command)[
            StringName("throttle")
        ]),
        1.5,
        0.001
    );
    NETW_CHECK_CLOSE(
        double(engine->canonicalize_state(slot, command)[
            StringName("throttle")
        ]),
        1.3,
        0.0
    );
    CHECK_FALSE(engine->canonical_input_bytes(slot, command).is_empty());
    CHECK(engine->canonical_state_bytes(slot, command) == PackedByteArray());
}

TEST_CASE(
    "[Networked][Predict][Hosted][Declaration] a rewire onto no input "
    "declaration forgets the previous one"
) {
    const Ref<NetwPredictionEngine> engine = pool();
    const int64_t slot = engine->open(declaration(keys()));
    Ref<NetwPredictDeclaration> input;
    input.instantiate();
    input->append_field(
        StringName("throttle"),
        int(PropertyClass::CAUSAL),
        StringName(),
        0.0,
        false,
        false,
        -1.0,
        -1.0,
        false,
        coarse(),
        int(Variant::FLOAT)
    );
    engine->rewire(slot, declaration(keys()), input);
    Dictionary command;
    command[StringName("throttle")] = 1.3;
    CHECK_FALSE(engine->canonical_input_bytes(slot, command).is_empty());

    engine->rewire(slot, declaration(keys()));

    CHECK(engine->canonical_input_bytes(slot, command) == PackedByteArray());
}

TEST_CASE("[Networked][Predict][Hosted][Declaration] the pass order is the "
          "declared order key, ascending") {
    const Ref<NetwPredictionEngine> engine = pool();
    const int64_t first = engine->open(declaration(keys()));
    const int64_t second = engine->open(declaration(keys()));
    const int64_t third = engine->open(declaration(keys()));
    engine->set_order_key(first, 30);
    engine->set_order_key(second, 10);
    engine->set_order_key(third, 20);

    const PackedInt64Array order = engine->ordered_slots();

    NETW_CHECK_EQ(int64_t(order.size()), int64_t(3));
    NETW_CHECK_EQ(order[0], second);
    NETW_CHECK_EQ(order[1], third);
    NETW_CHECK_EQ(order[2], first);
}

TEST_CASE("[Networked][Predict][Hosted][Declaration] an unkeyed slot steps "
          "after every keyed one") {
    const Ref<NetwPredictionEngine> engine = pool();
    const int64_t unkeyed = engine->open(declaration(keys()));
    const int64_t keyed = engine->open(declaration(keys()));
    engine->set_order_key(keyed, 99);

    const PackedInt64Array order = engine->ordered_slots();

    NETW_CHECK_EQ(order[0], keyed);
    NETW_CHECK_EQ(order[1], unkeyed);
    NETW_CHECK_EQ(engine->order_key_of(unkeyed), -1);
}

TEST_CASE("[Networked][Predict][Hosted][Declaration] a closed slot leaves the "
          "order") {
    const Ref<NetwPredictionEngine> engine = pool();
    const int64_t first = engine->open(declaration(keys()));
    const int64_t second = engine->open(declaration(keys()));
    engine->set_order_key(first, 1);
    engine->set_order_key(second, 2);

    engine->close(first);

    const PackedInt64Array order = engine->ordered_slots();

    NETW_CHECK_EQ(int64_t(order.size()), int64_t(1));
    NETW_CHECK_EQ(order[0], second);
}

} // namespace TestNetwPredictDeclarationLaws
