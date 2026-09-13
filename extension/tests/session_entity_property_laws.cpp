#include "support/netw_test.h"

#include "netw/api/netw_multiplayer.hpp"

namespace TestNetwSessionEntityProperty {

using namespace godot;
using netw::NetwMultiplayer;

struct Declared {
    Ref<NetwMultiplayer> session;
    RID schema;
    RID set;
};

Declared cut_set() {
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
    out.set = out.session->property_set_create(
        out.schema,
        NetwMultiplayer::RECORD_KIND_STATE
    );
    out.session->property_set_add_column(out.set, 0);
    return out;
}

TEST_CASE(
    "[Networked][Session][Hosted] L1 a property set joins an entity only "
    "after it is sealed, so a set still being cut cannot go on the wire"
) {
    Declared world = cut_set();
    const RID entity = world.session->entity_create();

    NETW_CHECK_EQ(
        world.session->entity_add_property_set(entity, world.set, 0),
        ERR_INVALID_DATA
    );

    SUBCASE("and after sealing it is the entity that is missing") {
        world.session->property_set_seal(world.set);
        NETW_CHECK_EQ(
            world.session->entity_add_property_set(entity, world.set, 0),
            ERR_UNAVAILABLE
        );
    }

    SUBCASE("a set nobody cut is refused before the seal is even read") {
        NETW_CHECK_EQ(
            world.session->entity_add_property_set(entity, RID(), 0),
            ERR_DOES_NOT_EXIST
        );
    }

    SUBCASE("an entity nobody minted is refused too") {
        world.session->property_set_seal(world.set);
        NETW_CHECK_EQ(
            world.session->entity_add_property_set(RID(), world.set, 0),
            ERR_DOES_NOT_EXIST
        );
    }
}

TEST_CASE(
    "[Networked][Session][Hosted] L2 reading a property off an entity that "
    "attached no set answers nothing rather than a zero of the right type"
) {
    Declared world = cut_set();
    const RID entity = world.session->entity_create();

    NETW_CHECK_EQ(
        world.session->entity_get_property(entity, 0, 0).get_type(),
        Variant::NIL
    );
    NETW_CHECK_EQ(
        world.session->entity_get_property(entity, 0, 99).get_type(),
        Variant::NIL
    );

    SUBCASE("removing a set that was never attached is a no-op") {
        world.session->entity_remove_property_set(entity, 0);
        NETW_CHECK_EQ(
            world.session->entity_get_property(entity, 0, 0).get_type(),
            Variant::NIL
        );
    }
}

} // namespace TestNetwSessionEntityProperty
