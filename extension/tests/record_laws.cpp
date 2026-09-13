#include "support/netw_test.h"

#include "godot/object.hpp"
#include "netw/api/record.hpp"

namespace TestNetwRecordLaws {

using namespace godot;
using netw::DictionaryRecord;
using netw::NetwRecord;
using netw::Serde;

Ref<DictionaryRecord> fresh() {
    Ref<DictionaryRecord> record;
    record.instantiate();
    return record;
}

Dictionary property_named(const Ref<NetwRecord> &p_record, const char *p_name) {
    const Array listed = netw::gd::property_list(p_record.ptr());
    for (int at = 0; at < listed.size(); at++) {
        const Dictionary entry = listed[at];
        if (StringName(entry["name"]) == StringName(p_name)) {
            return entry;
        }
    }
    return Dictionary();
}

TEST_CASE(
    "[Networked][Data][Hosted] R1 a stored name reads and writes as a "
    "property, and the resource's own names never enter the record"
) {
    const Ref<DictionaryRecord> record = fresh();

    record->set("health", 75);
    CHECK(record->has_value("health"));
    NETW_CHECK_EQ(int(record->get("health")), 75);

    record->set("resource_name", "named");
    CHECK_FALSE(record->has_value("resource_name"));
    const bool named = String(record->get_name()) == String("named");
    CHECK(named);

    Dictionary values;
    values["a"] = 1;
    record->set("data", values);
    CHECK_FALSE(record->has_value("data"));
    NETW_CHECK_EQ(record->get_data().size(), 1);
}

TEST_CASE(
    "[Networked][Data][Hosted] R2 the property list names every stored key "
    "with the type its value carries"
) {
    const Ref<DictionaryRecord> record = fresh();
    record->set_value("health", 75);
    record->set_value("place", Vector2(1, 2));

    const Dictionary health = property_named(record, "health");
    const Dictionary place = property_named(record, "place");

    CHECK_FALSE(health.is_empty());
    CHECK_FALSE(place.is_empty());
    if (!health.is_empty() && !place.is_empty()) {
        NETW_CHECK_EQ(int(health["type"]), int(Variant::INT));
        NETW_CHECK_EQ(int(place["type"]), int(Variant::VECTOR2));
    }
}

TEST_CASE(
    "[Networked][Data][Hosted] R3 the names a record answers are StringNames, "
    "in the order the storage holds them"
) {
    const Ref<DictionaryRecord> record = fresh();
    record->set_value("first", 1);
    record->set_value("second", 2);

    const TypedArray<StringName> names = record->get_property_names();

    NETW_CHECK_EQ(names.size(), 2);
    NETW_CHECK_EQ(names.get_typed_builtin(), int64_t(Variant::STRING_NAME));
    if (names.size() == 2) {
        const bool leads = StringName(names[0]) == StringName("first");
        const bool trails = StringName(names[1]) == StringName("second");
        CHECK(leads);
        CHECK(trails);
    }
}

TEST_CASE(
    "[Networked][Data][Hosted] R4 a dictionary record is local to the scene "
    "that instances it"
) {
    const Ref<DictionaryRecord> record = fresh();

    CHECK(record->is_local_to_scene());
}

TEST_CASE(
    "[Networked][Data][Hosted] R5 a record with nothing stored refuses to "
    "start an iteration, and one with keys walks them once"
) {
    const Ref<DictionaryRecord> record = fresh();
    const Array cursor;

    CHECK_FALSE(record->iterate_init(cursor));

    record->set_value("only", 1);
    CHECK(record->iterate_init(cursor));
    const bool walked
        = StringName(record->iterate_get(Variant())) == StringName("only");
    CHECK(walked);
    CHECK_FALSE(record->iterate_next(cursor));
}

TEST_CASE(
    "[Networked][Data][Hosted] R6 a Serde with no implementation answers no "
    "bytes and ignores the bytes it is given"
) {
    Ref<Serde> plain;
    plain.instantiate();

    PackedByteArray given;
    given.push_back(7);
    plain->deserialize(given);

    CHECK(plain->serialize().is_empty());
}

TEST_CASE(
    "[Networked][Data][Hosted] R8 the bytes a record answers rebuild it whole, "
    "and the rebuilt record carries the types it was given"
) {
    const Ref<DictionaryRecord> record = fresh();
    record->set_value("health", 75);
    record->set_value("place", Vector2(1, 2));

    const Ref<DictionaryRecord> copy = fresh();
    copy->deserialize(record->serialize());

    NETW_CHECK_EQ(copy->to_dict().size(), 2);
    NETW_CHECK_EQ(int(copy->get_value("health")), 75);
    const bool placed
        = Vector2(copy->get_value("place")).is_equal_approx(Vector2(1, 2));
    CHECK(placed);
    CHECK_FALSE(copy->is_empty());
}

TEST_CASE(
    "[Networked][Data][Hosted] R7 an absent name answers the fallback it was "
    "handed and a stored null answers the null, so has_value is what "
    "separates the two and the fallback never masks a value that is there"
) {
    const Ref<DictionaryRecord> record = fresh();

    CHECK_FALSE(record->has_value("missing"));
    CHECK(record->get_value("missing").get_type() == Variant::NIL);
    NETW_CHECK_EQ(int(record->get_value("missing", 42)), 42);

    record->set_value("missing", Variant());
    CHECK(record->has_value("missing"));
    CHECK(record->get_value("missing", 42).get_type() == Variant::NIL);
}

TEST_CASE(
    "[Networked][Data][Hosted] R9 the dictionary a record answers rebuilds "
    "another record through from_dict, so the pair round trips without either "
    "side reaching for the byte form"
) {
    const Ref<DictionaryRecord> record = fresh();
    record->set_value("x", 10);
    record->set_value("y", 20);

    const Ref<DictionaryRecord> copy = fresh();
    CHECK(copy->is_empty());
    copy->from_dict(record->to_dict());

    CHECK_FALSE(copy->is_empty());
    NETW_CHECK_EQ(int(copy->get_value("x")), 10);
    NETW_CHECK_EQ(int(copy->get_value("y")), 20);
}

} // namespace TestNetwRecordLaws
