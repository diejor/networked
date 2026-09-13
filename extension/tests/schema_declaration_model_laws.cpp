#include "support/netw_test.h"

#include "netw/api/netw_multiplayer.hpp"
#include "netw/api/quantize.hpp"
#include "netw/schema_core.hpp"
#include "netw/schema_model.hpp"

namespace TestSchemaDeclarationModel {

using namespace godot;
using netw::NetwMultiplayer;
using netw::NetwQuantizeScalar;
using netw::NetwSchema;
using netw::NetwSchemaColumn;
namespace schema_model = netw::schema_model;
using netw::SchemaCore;

struct Registry {
    Registry() {
        schema_model::clear();
    }
    ~Registry() {
        schema_model::clear();
    }
};

Ref<NetwSchema> declare(const StringName &name) {
    return NetwSchema::declare(name);
}

Ref<NetwMultiplayer> session() {
    Ref<NetwMultiplayer> core;
    core.instantiate();
    return core;
}

TEST_CASE(
    "[Networked][Table][Hosted] SD1 the builder writes the registry and hands "
    "back plain column indices a static initializer can hold"
) {
    Registry registry;
    const Ref<NetwSchema> schema = declare("ModelMob");
    NETW_CHECK_EQ(schema->vector3("pos", Ref<netw::NetwQuantize>(), 1), 0);
    NETW_CHECK_EQ(schema->u16("hp", 1), 1);
    CHECK(bool(schema->get_schema_name() == StringName("ModelMob")));

    const Ref<NetwSchema> declared = schema_model::find("ModelMob");
    NETW_CHECK_EQ(int(declared.is_valid()), 1);
    if (declared.is_null()) {
        return;
    }
    const TypedArray<NetwSchemaColumn> columns = declared->get_columns();
    NETW_CHECK_EQ(columns.size(), 2);
    const Ref<NetwSchemaColumn> first = columns[0];
    CHECK(bool(first->get_key() == StringName("pos")));
    NETW_CHECK_EQ(first->get_type(), int(SchemaCore::VECTOR3));
}

TEST_CASE(
    "[Networked][Table][Hosted] SD2 every builder verb names the type it "
    "claims, so a column index means the same storage on both peers"
) {
    Registry registry;
    const Ref<NetwSchema> schema = declare("ModelTypes");
    const Ref<netw::NetwQuantize> none;
    const int indices[16] = {
        schema->f32("a", none, 1),
        schema->f64("b", none, 1),
        schema->i8("c", 1),
        schema->u8("d", 1),
        schema->i16("e", 1),
        schema->u16("f", 1),
        schema->i32("g", 1),
        schema->i64("h", 1),
        schema->boolean("i", 1),
        schema->vector2("j", none, 1),
        schema->vector3("k", none, 1),
        schema->vector4("l", none, 1),
        schema->color("m", none, 1),
        schema->quaternion("n", none, 1),
        schema->entity("o", 1),
        schema->variant("p", 1),
    };
    const int expected[16] = {
        SchemaCore::F32,
        SchemaCore::F64,
        SchemaCore::I8,
        SchemaCore::U8,
        SchemaCore::I16,
        SchemaCore::U16,
        SchemaCore::I32,
        SchemaCore::I64,
        SchemaCore::BOOL,
        SchemaCore::VECTOR2,
        SchemaCore::VECTOR3,
        SchemaCore::VECTOR4,
        SchemaCore::COLOR,
        SchemaCore::QUATERNION,
        SchemaCore::ENTITY,
        SchemaCore::VARIANT,
    };

    const Ref<NetwSchema> declared = schema_model::find("ModelTypes");
    NETW_CHECK_EQ(int(declared.is_valid()), 1);
    if (declared.is_null()) {
        return;
    }
    const TypedArray<NetwSchemaColumn> columns = declared->get_columns();
    NETW_CHECK_EQ(columns.size(), 16);
    if (columns.size() != 16) {
        return;
    }
    int agreeing = 0;
    for (int at = 0; at < 16; ++at) {
        const Ref<NetwSchemaColumn> column = columns[at];
        if (indices[at] == at && column->get_type() == expected[at]) {
            agreeing += 1;
        }
    }
    NETW_CHECK_EQ(agreeing, 16);
}

TEST_CASE(
    "[Networked][Table][Hosted] SD3 a script reload re-running its "
    "initializer extends one declaration rather than making a second"
) {
    Registry registry;
    const Ref<netw::NetwQuantize> none;
    const int first = declare("ModelReload")->vector3("pos", none, 1);
    const int again = declare("ModelReload")->vector3("pos", none, 1);

    NETW_CHECK_EQ(first, again);
    NETW_CHECK_EQ(schema_model::find("ModelReload")->get_columns().size(), 1);
    NETW_CHECK_EQ(schema_model::declarations().size(), 1);
}

TEST_CASE(
    "[Networked][Table][Hosted] SD4 a redeclared column whose shape moved is "
    "refused rather than shifting a column address"
) {
    Registry registry;
    const Ref<netw::NetwQuantize> none;
    const Ref<NetwSchema> schema = declare("ModelSkew");
    NETW_CHECK_EQ(schema->vector3("pos", none, 1), 0);

    NETW_CHECK_EQ(schema->vector2("pos", none, 1), -1);
    NETW_CHECK_EQ(schema->vector3("pos", none, 4), -1);
    NETW_CHECK_EQ(schema_model::find("ModelSkew")->get_columns().size(), 1);
}

TEST_CASE(
    "[Networked][Table][Hosted] SD5 adoption order is name order, because a "
    "wire id is the name-sorted position two peers compute without agreeing"
) {
    Registry registry;
    declare("ModelZulu");
    declare("ModelAlpha");
    declare("ModelMike");

    const TypedArray<NetwSchema> ordered = schema_model::declarations();
    NETW_CHECK_EQ(ordered.size(), 3);
    if (ordered.size() != 3) {
        return;
    }
    String names;
    for (int at = 0; at < 3; ++at) {
        const Ref<NetwSchema> row = ordered[at];
        names += String(row->get_schema_name()) + " ";
    }
    NETW_CHECK_EQ(int(names == String("ModelAlpha ModelMike ModelZulu ")), 1);
}

TEST_CASE(
    "[Networked][Table][Hosted] SD6 a holder outliving a registry reset puts "
    "its schema back, since a static initializer runs once and never again"
) {
    Registry registry;
    const Ref<netw::NetwQuantize> none;
    const Ref<NetwSchema> schema = declare("ModelHeld");
    schema->f32("a", none, 1);
    schema_model::clear();
    CHECK(schema_model::find("ModelHeld").is_null());

    schema->register_declaration();

    const Ref<NetwSchema> back = schema_model::find("ModelHeld");
    NETW_CHECK_EQ(int(back.is_valid()), 1);
    if (back.is_null()) {
        return;
    }
    NETW_CHECK_EQ(back->get_columns().size(), 1);
}

TEST_CASE(
    "[Networked][Table][Hosted] SD7 a declaration says for itself whether it "
    "wants a wire, and a stored one adopts a schema without a table"
) {
    Registry registry;
    const Ref<netw::NetwQuantize> none;
    declare("ModelWired")->f32("a", none, 1);
    CHECK(schema_model::find("ModelWired")->is_replicated());

    const Ref<NetwSchema> stored = declare("ModelStored");
    stored->f32("a", none, 1);
    stored->replicated(false);
    CHECK_FALSE(schema_model::find("ModelStored")->is_replicated());

    const Ref<NetwMultiplayer> core = session();
    CHECK(core->table_find_or_adopt("ModelWired").is_valid());
    CHECK_FALSE(core->table_find_or_adopt("ModelStored").is_valid());
    CHECK(core->schema_find_or_adopt("ModelStored").is_valid());
}

TEST_CASE(
    "[Networked][Table][Hosted] SD8 a declaration made after the session was "
    "built is adopted on demand, and an undeclared name mints nothing"
) {
    Registry registry;
    const Ref<NetwMultiplayer> core = session();
    const Ref<netw::NetwQuantize> none;

    CHECK_FALSE(core->schema_find_or_adopt("ModelLate").is_valid());
    CHECK_FALSE(core->table_find_or_adopt("ModelLate").is_valid());

    declare("ModelLate")->u16("hp", 1);

    const RID schema = core->schema_find_or_adopt("ModelLate");
    NETW_CHECK_EQ(int(schema.is_valid()), 1);
    if (!schema.is_valid()) {
        return;
    }
    NETW_CHECK_EQ(core->schema_get_column_count(schema), 1);
    CHECK(core->table_find_or_adopt("ModelLate").is_valid());

    CHECK_FALSE(core->schema_find_or_adopt("ModelAbsent").is_valid());
    CHECK_FALSE(core->table_find_or_adopt("ModelAbsent").is_valid());
}

TEST_CASE(
    "[Networked][Table][Hosted] SD9 the reliable knob rides the declaration "
    "onto the table the session mints from it"
) {
    Registry registry;
    const Ref<netw::NetwQuantize> none;
    declare("ModelRare")->u16("tier", 1);
    CHECK_FALSE(schema_model::find("ModelRare")->is_reliable());
    declare("ModelRare")->reliable(true);
    CHECK(schema_model::find("ModelRare")->is_reliable());

    const Ref<NetwMultiplayer> core = session();
    const RID table = core->table_find_or_adopt("ModelRare");
    NETW_CHECK_EQ(int(table.is_valid()), 1);
    if (!table.is_valid()) {
        return;
    }
    CHECK(core->get_table_core()->is_reliable(table));
}

TEST_CASE(
    "[Networked][Table][Hosted] SD10 a quantizer declared on the builder "
    "rides adoption into the shape two sessions seal from it"
) {
    Registry registry;
    const Ref<netw::NetwQuantize> none;
    Ref<NetwQuantizeScalar> quantizer;
    quantizer.instantiate();
    quantizer->limits(-512.0, 512.0);
    quantizer->step(0.03);
    declare("ModelPacked")->vector3("pos", quantizer, 1);
    declare("ModelRaw")->vector3("pos", none, 1);

    const Ref<NetwMultiplayer> here = session();
    const RID packed = here->schema_find_or_adopt("ModelPacked");
    const RID raw = here->schema_find_or_adopt("ModelRaw");
    NETW_CHECK_EQ(int(packed.is_valid() && raw.is_valid()), 1);
    if (!packed.is_valid() || !raw.is_valid()) {
        return;
    }
    NETW_CHECK_ORDER(here->schema_get_hash(packed), 0, !=);
    CHECK(here->get_schema_core()->column_quantizer(packed, 0).is_valid());
    CHECK(here->get_schema_core()->column_quantizer(raw, 0).is_null());

    const Ref<NetwMultiplayer> there = session();
    const RID other = there->table_find_or_adopt("ModelPacked");
    const RID mine = here->table_find_or_adopt("ModelPacked");
    NETW_CHECK_EQ(int(other.is_valid() && mine.is_valid()), 1);
    if (!other.is_valid() || !mine.is_valid()) {
        return;
    }
    NETW_CHECK_EQ(
        there->table_get_wire_hash(other),
        here->table_get_wire_hash(mine)
    );
}

} // namespace TestSchemaDeclarationModel
