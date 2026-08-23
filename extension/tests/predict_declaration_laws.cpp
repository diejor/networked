#include "support/netw_test.h"

#include "netw/predict/engine.hpp"
#include "netw/predict/journal.hpp"
#include "netw/api/quantize.hpp"

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

Ref<NetwPredictDeclaration> typed_input() {
    Ref<NetwPredictDeclaration> out;
    out.instantiate();
    const int TYPES[] = {
        int(Variant::VECTOR2),
        int(Variant::BOOL),
        int(Variant::COLOR),
        int(Variant::NIL),
        int(Variant::OBJECT),
    };
    const char *KEYS[] = { "motion", "bombing", "tint", "untyped", "target" };
    for (int at = 0; at < 5; ++at) {
        out->append_field(
            StringName(KEYS[at]),
            int(PropertyClass::CAUSAL),
            StringName(),
            0.0,
            false,
            false,
            -1.0,
            -1.0,
            false,
            Ref<NetwQuantize>(),
            TYPES[at]
        );
    }
    return out;
}

TEST_CASE(
    "[Networked][Predict][Hosted][Declaration] a coast command zeroes every "
    "field its declaration typed, and names no field it did not"
) {
    const Ref<NetwPredictionEngine> engine = pool();
    const int64_t slot = engine->open(Ref<NetwPredictDeclaration>());
    engine->rewire(slot, declaration(keys()), typed_input());

    const Dictionary coast = engine->coast_command(slot);

    NETW_CHECK_EQ(int(coast.size()), 3);
    CHECK(Vector2(coast[StringName("motion")]) == Vector2());
    CHECK_FALSE(bool(coast[StringName("bombing")]));
    // A transparent alpha rather than Color's own default of opaque black, so
    // a declared colour coasts to nothing the way every other type does.
    CHECK(Color(coast[StringName("tint")]) == Color(0.0, 0.0, 0.0, 0.0));

    // A field whose declaration carries no type is omitted rather than
    // guessed, and so is one whose type has no zero. The row commands nothing,
    // and a value invented for either is a command like any other.
    CHECK_FALSE(coast.has(StringName("untyped")));
    CHECK_FALSE(coast.has(StringName("target")));

    // The STATE declaration is a different codec, so a coast never names a
    // field the owner does not author.
    CHECK_FALSE(coast.has(StringName("velocity")));

    CHECK(engine->coast_command(slot + 9000).is_empty());
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

int64_t seated(
    const Ref<NetwPredictionEngine> &p_pool,
    int p_schedule,
    int p_role,
    int64_t p_order_key,
    int p_island = NetwPredictionEngine::ISLAND_NONE
) {
    const int64_t slot = p_pool->open(declaration(keys()));
    // Checked, because a REFUSED configure leaves the slot on its defaults and
    // a phase law would then read a roster nobody seated.
    REQUIRE(p_pool->configure(
        slot,
        p_schedule,
        p_role,
        int(CorrectionMode::SNAP),
        int(RestoreMode::EXACT),
        6,
        p_island
    ));
    p_pool->set_order_key(slot, p_order_key);
    return slot;
}

TEST_CASE("[Networked][Predict][Hosted][Declaration] a phase names the slots "
          "it steps, in the declared order, and never one it must not reach") {
    const Ref<NetwPredictionEngine> engine = pool();
    // A joint group shares one floor, so it can only be seated on a schedule
    // whose replay is the same run twice.
    const int64_t joint = seated(
        engine,
        int(Schedule::TICK),
        int(Role::PREDICT),
        5,
        NetwPredictionEngine::ISLAND_JOINT
    );
    const int64_t tick_predict
        = seated(engine, int(Schedule::TICK), int(Role::PREDICT), 10);
    const int64_t frame_predict
        = seated(engine, int(Schedule::FRAME), int(Role::PREDICT), 15);
    const int64_t frame_consume
        = seated(engine, int(Schedule::FRAME), int(Role::CONSUME), 20);
    const int64_t frame_remote
        = seated(engine, int(Schedule::FRAME), int(Role::REMOTE), 30);
    // A promoted remote has no timeline of its own, so the pool refuses to
    // seat one outside an island at all.
    const int64_t frame_simulate = seated(
        engine,
        int(Schedule::FRAME),
        int(Role::SIMULATE),
        40,
        NetwPredictionEngine::ISLAND_DECLARED
    );

    // The island phase is per schedule tier: a FRAME slot is not committed by
    // the TICK tier's pass and the two never see each other's roster.
    const PackedInt64Array island_tick
        = engine->pass_slots(NetwPredictionEngine::PASS_ISLAND_TICK);
    NETW_CHECK_EQ(int(island_tick.size()), 2);
    NETW_CHECK_EQ(island_tick[0], joint);
    NETW_CHECK_EQ(island_tick[1], tick_predict);

    const PackedInt64Array island_frame
        = engine->pass_slots(NetwPredictionEngine::PASS_ISLAND_FRAME);
    NETW_CHECK_EQ(int(island_frame.size()), 2);
    NETW_CHECK_EQ(island_frame[0], frame_predict);
    NETW_CHECK_EQ(island_frame[1], frame_consume);

    // Only a group member is carried by the group's pass. Running it for a
    // slot that declared no island would double-write the body the ladder
    // already answers for.
    const PackedInt64Array joint_pass
        = engine->pass_slots(NetwPredictionEngine::PASS_JOINT);
    NETW_CHECK_EQ(int(joint_pass.size()), 1);
    NETW_CHECK_EQ(joint_pass[0], joint);

    // A remote display never steps until it is authoring its own fallback.
    const PackedInt64Array frame
        = engine->pass_slots(NetwPredictionEngine::PASS_FRAME);
    NETW_CHECK_EQ(int(frame.size()), 3);
    NETW_CHECK_EQ(frame[0], frame_predict);
    NETW_CHECK_EQ(frame[1], frame_consume);
    NETW_CHECK_EQ(frame[2], frame_simulate);

    engine->enter_quarantine(
        frame_remote,
        4,
        false,
        int(netw::predict::Attribution::CONTACT),
        false
    );
    const PackedInt64Array latched
        = engine->pass_slots(NetwPredictionEngine::PASS_FRAME);
    NETW_CHECK_EQ(int(latched.size()), 4);
    NETW_CHECK_EQ(latched[2], frame_remote);

    // Only an owner records the frame it drove, and only under FRAME.
    const PackedInt64Array finalize
        = engine->pass_slots(NetwPredictionEngine::PASS_FINALIZE_FRAME);
    NETW_CHECK_EQ(int(finalize.size()), 1);
    NETW_CHECK_EQ(finalize[0], frame_predict);

    // A closed slot leaves every phase with the order.
    engine->close(joint);
    NETW_CHECK_EQ(
        int(engine->pass_slots(NetwPredictionEngine::PASS_JOINT).size()),
        0
    );
    NETW_CHECK_EQ(
        int(engine->pass_slots(NetwPredictionEngine::PASS_ISLAND_TICK).size()),
        1
    );
}

} // namespace TestNetwPredictDeclarationLaws
