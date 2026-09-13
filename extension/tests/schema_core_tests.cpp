#include "support/netw_test.h"

#include "netw/api/quantize.hpp"
#include "netw/api/schema_core.hpp"
#include "netw/handle_ledger.hpp"

namespace TestSchemaCore {

using namespace godot;
using netw::NetwHandleLedger;
using netw::NetwQuantizeScalar;
using netw::SchemaCore;
using netw::table::SchemaColumn;
using netw::table::SchemaRecord;

SchemaRecord make_record(const StringName &name) {
    SchemaRecord record;
    record.name = name;
    return record;
}

TEST_CASE(
    "[Networked][Table][Hosted] Declaration order is the column address"
) {
    SchemaRecord record = make_record("Mob");
    NETW_CHECK_EQ(
        SchemaCore::append_column(&record, "pos", SchemaCore::VECTOR3, 1),
        0
    );
    NETW_CHECK_EQ(
        SchemaCore::append_column(&record, "hp", SchemaCore::U16, 1),
        1
    );
    NETW_CHECK_EQ(
        SchemaCore::append_column(&record, "cooldown", SchemaCore::F32, 8),
        2
    );

    NETW_CHECK_EQ(record.column_count(), 3);
    CHECK(bool(record.at(1)->key == StringName("hp")));
    NETW_CHECK_EQ(record.at(2)->stride, 8);
    NETW_CHECK_EQ(
        SchemaCore::append_column(&record, "hp", SchemaCore::U16, 1),
        -1
    );
}

TEST_CASE("[Networked][Table][Hosted] A column refuses what it cannot store") {
    SchemaRecord record = make_record("Refuse");
    NETW_CHECK_EQ(
        SchemaCore::append_column(&record, "", SchemaCore::U16, 1),
        -1
    );
    NETW_CHECK_EQ(
        SchemaCore::append_column(&record, "k", SchemaCore::U16, 0),
        -1
    );
    NETW_CHECK_EQ(SchemaCore::append_column(&record, "k", -1, 1), -1);
    NETW_CHECK_EQ(
        SchemaCore::append_column(
            &record,
            "k",
            SchemaCore::COLUMN_TYPE_COUNT,
            1
        ),
        -1
    );
    NETW_CHECK_EQ(record.column_count(), 0);
}

TEST_CASE("[Networked][Table][Hosted] Sealing fixes the order and the hash") {
    SchemaRecord record = make_record("Sealed");
    SchemaCore::append_column(&record, "hp", SchemaCore::U16, 1);
    NETW_CHECK_EQ(record.shape_hash, 0);
    CHECK_FALSE(record.sealed);

    NETW_CHECK_EQ(SchemaCore::fix(&record), OK);

    CHECK(record.sealed);
    CHECK(record.shape_hash != 0);
    NETW_CHECK_EQ(
        SchemaCore::append_column(&record, "mana", SchemaCore::U16, 1),
        -1
    );
    NETW_CHECK_EQ(record.column_count(), 1);
    Ref<NetwQuantizeScalar> quantizer;
    quantizer.instantiate();
    SchemaCore::assign_quantizer(&record, 0, quantizer);
    CHECK(record.at(0)->quantizer.is_null());
}

TEST_CASE(
    "[Networked][Table][Hosted] The shape hash folds every wire-visible field"
) {
    SchemaRecord base = make_record("Shape");
    SchemaCore::append_column(&base, "pos", SchemaCore::VECTOR3, 1);
    SchemaCore::fix(&base);

    SchemaRecord same = make_record("Shape");
    SchemaCore::append_column(&same, "pos", SchemaCore::VECTOR3, 1);
    SchemaCore::fix(&same);
    NETW_CHECK_EQ(same.shape_hash, base.shape_hash);

    SchemaRecord renamed = make_record("Shape2");
    SchemaCore::append_column(&renamed, "pos", SchemaCore::VECTOR3, 1);
    SchemaCore::fix(&renamed);
    CHECK(renamed.shape_hash != base.shape_hash);

    SchemaRecord rekeyed = make_record("Shape");
    SchemaCore::append_column(&rekeyed, "position", SchemaCore::VECTOR3, 1);
    SchemaCore::fix(&rekeyed);
    CHECK(rekeyed.shape_hash != base.shape_hash);

    SchemaRecord retyped = make_record("Shape");
    SchemaCore::append_column(&retyped, "pos", SchemaCore::VECTOR2, 1);
    SchemaCore::fix(&retyped);
    CHECK(retyped.shape_hash != base.shape_hash);

    SchemaRecord restrided = make_record("Shape");
    SchemaCore::append_column(&restrided, "pos", SchemaCore::VECTOR3, 2);
    SchemaCore::fix(&restrided);
    CHECK(restrided.shape_hash != base.shape_hash);

    SchemaRecord packed = make_record("Shape");
    SchemaCore::append_column(&packed, "pos", SchemaCore::VECTOR3, 1);
    Ref<NetwQuantizeScalar> quantizer;
    quantizer.instantiate();
    SchemaCore::assign_quantizer(&packed, 0, quantizer);
    SchemaCore::fix(&packed);
    CHECK(packed.shape_hash != base.shape_hash);

    CHECK(base.shape_hash >= 0);
    CHECK(base.shape_hash <= 0xFFFF);
}

TEST_CASE(
    "[Networked][Table][Hosted] A column's quantizer tag is its class and width"
) {
    SchemaColumn raw;
    raw.key = "pos";
    raw.type = SchemaCore::VECTOR3;
    CHECK(bool(SchemaCore::quantizer_tag(&raw) == String("raw")));
    CHECK(bool(SchemaCore::quantizer_tag(nullptr) == String("raw")));

    Ref<NetwQuantizeScalar> quantizer;
    quantizer.instantiate();
    quantizer->limits(-512.0, 512.0);
    quantizer->step(0.03);
    raw.quantizer = quantizer;

    const int64_t width = quantizer->total_bits(
        static_cast<Variant::Type>(
            SchemaCore::element_type(SchemaCore::VECTOR3)
        )
    );
    const String expected
        = String("NetwQuantizeScalar/") + String::num_int64(width);
    NETW_FORMAT_TEXT(
        tag,
        String(SchemaCore::quantizer_tag(&raw)).utf8().get_data()
    );
    CAPTURE(tag);
    CHECK(bool(SchemaCore::quantizer_tag(&raw) == expected));
}

TEST_CASE(
    "[Networked][Table][Hosted] A replayed declaration matches or refuses"
) {
    SchemaRecord record = make_record("Reload");
    SchemaCore::append_column(&record, "pos", SchemaCore::VECTOR3, 1);
    SchemaCore::append_column(&record, "hp", SchemaCore::U16, 1);
    SchemaCore::fix(&record);
    const int sealed_hash = record.shape_hash;

    SchemaCore::open_redeclare(&record);
    NETW_CHECK_EQ(
        SchemaCore::append_column(&record, "pos", SchemaCore::VECTOR3, 1),
        0
    );
    NETW_CHECK_EQ(
        SchemaCore::append_column(&record, "hp", SchemaCore::U16, 1),
        1
    );
    NETW_CHECK_EQ(SchemaCore::fix(&record), OK);
    NETW_CHECK_EQ(record.shape_hash, sealed_hash);
    NETW_CHECK_EQ(record.column_count(), 2);

    SchemaCore::open_redeclare(&record);
    NETW_CHECK_EQ(
        SchemaCore::append_column(&record, "pos", SchemaCore::VECTOR2, 1),
        -1
    );
    NETW_CHECK_EQ(SchemaCore::fix(&record), ERR_UNCONFIGURED);

    SchemaCore::open_redeclare(&record);
    SchemaCore::append_column(&record, "pos", SchemaCore::VECTOR3, 1);
    NETW_CHECK_EQ(SchemaCore::fix(&record), ERR_UNCONFIGURED);

    SchemaCore::open_redeclare(&record);
    SchemaCore::append_column(&record, "pos", SchemaCore::VECTOR3, 1);
    SchemaCore::append_column(&record, "hp", SchemaCore::U16, 1);
    NETW_CHECK_EQ(
        SchemaCore::append_column(&record, "mana", SchemaCore::U16, 1),
        -1
    );
    NETW_CHECK_EQ(SchemaCore::fix(&record), ERR_UNCONFIGURED);
    NETW_CHECK_EQ(record.column_count(), 2);

    NETW_CHECK_EQ(SchemaCore::fix(&record), OK);
}

TEST_CASE("[Networked][Table][Hosted] The session keys records by RID") {
    SchemaCore held_core;
    SchemaCore *const core = &held_core;
    NetwHandleLedger held;
    NetwHandleLedger *const ledger = &held;
    const RID handle = ledger->rid_create();

    core->declare(handle, "Keyed");
    NETW_CHECK_EQ(core->add_column(handle, "pos", SchemaCore::VECTOR3, 1), 0);
    NETW_CHECK_EQ(core->add_column(handle, "cooldown", SchemaCore::F32, 4), 1);
    NETW_CHECK_EQ(core->seal(handle), OK);

    CHECK(core->is_valid(handle));
    CHECK(bool(core->name_of(handle) == StringName("Keyed")));
    CHECK(bool(core->find("Keyed") == handle));
    NETW_CHECK_EQ(core->column_count(handle), 2);
    CHECK(bool(core->column_key(handle, 0) == StringName("pos")));
    NETW_CHECK_EQ(core->column_type(handle, 1), SchemaCore::F32);
    NETW_CHECK_EQ(core->column_stride(handle, 1), 4);
    NETW_CHECK_EQ(core->find_column(handle, "cooldown"), 1);
    NETW_CHECK_EQ(core->find_column(handle, "absent"), -1);
    CHECK(core->has_stride(handle));
    CHECK_FALSE(core->has_variant(handle));
    NETW_CHECK_EQ(core->hash_of(handle), core->record_of(handle)->shape_hash);
}

TEST_CASE(
    "[Networked][Table][Hosted] An unknown handle is answered, not crashed"
) {
    SchemaCore held_core;
    SchemaCore *const core = &held_core;
    const RID absent;

    CHECK_FALSE(core->is_valid(absent));
    CHECK(bool(core->name_of(absent) == StringName()));
    NETW_CHECK_EQ(core->hash_of(absent), 0);
    NETW_CHECK_EQ(core->column_count(absent), 0);
    CHECK(bool(core->column_key(absent, 0) == StringName()));
    NETW_CHECK_EQ(core->column_type(absent, 0), -1);
    NETW_CHECK_EQ(core->column_stride(absent, 0), 0);
    CHECK(core->column_quantizer(absent, 0).is_null());
    NETW_CHECK_EQ(core->find_column(absent, "k"), -1);
    CHECK_FALSE(core->has_variant(absent));
    CHECK_FALSE(core->has_stride(absent));
    CHECK(core->record_of(absent) == nullptr);
    NETW_CHECK_EQ(core->seal(absent), ERR_DOES_NOT_EXIST);
    NETW_CHECK_EQ(core->add_column(absent, "k", SchemaCore::U16, 1), -1);
    CHECK_FALSE(core->find("never declared").is_valid());
}

TEST_CASE(
    "[Networked][Table][Hosted] The self-describing tier is what a table "
    "refuses"
) {
    SchemaCore held_core;
    SchemaCore *const core = &held_core;
    NetwHandleLedger held;
    NetwHandleLedger *const ledger = &held;
    const RID handle = ledger->rid_create();
    core->declare(handle, "Loose");
    core->add_column(handle, "blob", SchemaCore::VARIANT, 1);
    core->seal(handle);

    CHECK(core->has_variant(handle));
    NETW_CHECK_EQ(
        SchemaCore::type_from_variant(Variant::STRING),
        SchemaCore::VARIANT
    );
    NETW_CHECK_EQ(
        SchemaCore::type_from_variant(Variant::NIL),
        SchemaCore::VARIANT
    );
    NETW_CHECK_EQ(SchemaCore::type_from_variant(Variant::INT), SchemaCore::I64);
    NETW_CHECK_EQ(
        SchemaCore::type_from_variant(Variant::FLOAT),
        SchemaCore::F64
    );
    NETW_CHECK_EQ(
        SchemaCore::type_from_variant(Variant::QUATERNION),
        SchemaCore::QUATERNION
    );
}

TEST_CASE("[Networked][Table][Hosted] Every column type has a storage shape") {
    for (int type = 0; type < SchemaCore::COLUMN_TYPE_COUNT; type++) {
        CAPTURE(type);
        const Variant storage = SchemaCore::make_storage(type);
        NETW_CHECK_EQ(storage.get_type(), SchemaCore::storage_type(type));
    }
    NETW_CHECK_EQ(
        SchemaCore::storage_type(SchemaCore::VARIANT),
        Variant::ARRAY
    );
    NETW_CHECK_EQ(SchemaCore::element_type(SchemaCore::VARIANT), Variant::NIL);
    NETW_CHECK_EQ(SchemaCore::storage_type(SchemaCore::COLUMN_TYPE_COUNT), -1);
    NETW_CHECK_EQ(SchemaCore::element_type(SchemaCore::COLUMN_TYPE_COUNT), -1);
    NETW_CHECK_EQ(SchemaCore::storage_type(-1), -1);
    CHECK(SchemaCore::make_storage(-1).get_type() == Variant::NIL);
}

TEST_CASE("[Networked][Table][Hosted] Re-declaring a name reaches its record") {
    SchemaCore held_core;
    SchemaCore *const core = &held_core;
    NetwHandleLedger held;
    NetwHandleLedger *const ledger = &held;
    const RID handle = ledger->rid_create();
    core->declare(handle, "Twice");
    core->add_column(handle, "hp", SchemaCore::U16, 1);
    core->seal(handle);

    core->declare(handle, "Twice");
    NETW_CHECK_EQ(core->add_column(handle, "hp", SchemaCore::U16, 1), 0);
    NETW_CHECK_EQ(core->seal(handle), OK);
    NETW_CHECK_EQ(core->column_count(handle), 1);
}

} // namespace TestSchemaCore
