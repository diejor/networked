#include "support/netw_test.h"

#include "netw/api/netw_multiplayer.hpp"
#include "netw/api/property_set.hpp"

namespace TestNetwSessionPropertySet {

using namespace godot;
using netw::NetwMultiplayer;
using netw::NetwPropertySet;

bool same_rid(const RID &p_a, const RID &p_b) {
    return p_a == p_b;
}

bool differ(int64_t p_a, int64_t p_b) {
    return p_a != p_b;
}

struct Declared {
    Ref<NetwMultiplayer> session;
    RID schema;
};

Declared declare_schema() {
    Declared out;
    out.session.instantiate();
    out.schema = out.session->schema_create(StringName("probe"));
    out.session->schema_add_column(
        out.schema,
        StringName("position"),
        NetwMultiplayer::COLUMN_VECTOR3,
        1
    );
    out.session->schema_seal(out.schema);
    return out;
}

TEST_CASE(
    "[Networked][Session][Hosted] L1 a property set is minted only for a "
    "record kind the wire knows, so an invented kind mints no handle"
) {
    Declared world = declare_schema();

    const RID set = world.session->property_set_create(
        world.schema,
        NetwMultiplayer::RECORD_KIND_STATE
    );
    CHECK(set.is_valid());

    SUBCASE("the set remembers the schema it was cut from") {
        CHECK(
            same_rid(world.session->property_set_get_schema(set), world.schema)
        );
    }

    SUBCASE("a record kind nobody defined mints nothing") {
        CHECK_FALSE(world.session
                        ->property_set_create(
                            world.schema,
                            NetwMultiplayer::RecordKind(99)
                        )
                        .is_valid());
    }

    SUBCASE("a schema nobody declared cuts no set") {
        CHECK_FALSE(
            world.session
                ->property_set_create(RID(), NetwMultiplayer::RECORD_KIND_STATE)
                .is_valid()
        );
    }
}

TEST_CASE(
    "[Networked][Session][Hosted] L2 a column joins a set once, so a second "
    "add of the same schema column is refused rather than duplicated"
) {
    Declared world = declare_schema();
    const RID set = world.session->property_set_create(
        world.schema,
        NetwMultiplayer::RECORD_KIND_STATE
    );

    NETW_CHECK_EQ(world.session->property_set_add_column(set, 0), 0);

    SUBCASE("the same schema column is refused the second time") {
        NETW_CHECK_EQ(world.session->property_set_add_column(set, 0), -1);
    }

    SUBCASE("a schema column that does not exist is refused") {
        NETW_CHECK_EQ(world.session->property_set_add_column(set, 42), -1);
    }
}

TEST_CASE(
    "[Networked][Session][Hosted] L3 a sealed property set refuses every "
    "further write, so a wire hash can never describe a set that later moved"
) {
    Declared world = declare_schema();
    const RID set = world.session->property_set_create(
        world.schema,
        NetwMultiplayer::RECORD_KIND_STATE
    );
    world.session->property_set_add_column(set, 0);

    NETW_CHECK_EQ(world.session->property_set_seal(set), OK);
    const int64_t sealed_hash = world.session->property_set_get_wire_hash(set);
    CHECK(differ(sealed_hash, 0));

    SUBCASE("a column cannot join after the seal") {
        NETW_CHECK_EQ(world.session->property_set_add_column(set, 0), -1);
        NETW_CHECK_EQ(
            world.session->property_set_get_wire_hash(set),
            sealed_hash
        );
    }

    SUBCASE("a set parameter cannot move after the seal") {
        world.session->property_set_set_param(
            set,
            NetwMultiplayer::SET_PARAM_RELIABLE,
            true
        );
        NETW_CHECK_EQ(
            world.session->property_set_get_wire_hash(set),
            sealed_hash
        );
    }

    SUBCASE("sealing a set nobody cut is refused") {
        NETW_CHECK_EQ(
            world.session->property_set_seal(RID()),
            ERR_DOES_NOT_EXIST
        );
    }
}

TEST_CASE(
    "[Networked][Session][Hosted] L4 marking a column retained also marks it "
    "watched, because a retained lane is read by watching it"
) {
    Declared world = declare_schema();
    const RID set = world.session->property_set_create(
        world.schema,
        NetwMultiplayer::RECORD_KIND_STATE
    );
    const int field = world.session->property_set_add_column(set, 0);

    world.session->property_set_set_column_param(
        set,
        field,
        NetwMultiplayer::COLUMN_PARAM_LANE,
        NetwPropertySet::RETAINED
    );

    const TypedArray<netw::NetwPropertySetColumn> held
        = world.session->property_set_record(set)->get_columns();
    const Ref<netw::NetwPropertySetColumn> column = held[field];
    NETW_CHECK_EQ(column->get_lane(), NetwPropertySet::RETAINED);
    CHECK(column->get_watch());
}

} // namespace TestNetwSessionPropertySet
