#include "support/netw_test.h"

#include "godot/node.hpp"
#include "godot/scene_tree.hpp"
#include "netw/api/entity.hpp"
#include "netw/comp_table.hpp"

namespace TestNetwEntityComponentLaws {

using namespace godot;

struct Owned {
    Node *owner = nullptr;
    Ref<netw::NetwEntity> entity;
};

Owned an_entity() {
    Owned made;
    made.owner = memnew(Node);
    made.owner->set_name("Owner");
    made.entity = netw::NetwEntity::ensure(made.owner);
    return made;
}

Node *a_child(Node *p_parent, const char *p_name) {
    Node *made = memnew(Node);
    made->set_name(p_name);
    p_parent->add_child(made);
    return made;
}

TEST_CASE(
    "[Networked][Entity][Hosted] CM1 a registered component takes an id off "
    "its path, and the entity's own owner is never a component"
) {
    Owned made = an_entity();
    Node *gun = a_child(made.owner, "Gun");

    made.entity->register_component(gun);
    made.entity->register_component(made.owner);
    made.entity->hydrate_components();

    CHECK(made.entity->has_comp_path(NodePath("Gun")));
    NETW_CHECK_EQ(made.entity->comp_table().id_for_path(String("Gun")), 1);
    CHECK(String(made.entity->comp_table().path_for_id(1)) == String("Gun"));
    NETW_CHECK_EQ(made.entity->comp_table().id_for_path(String("Owner")), 0);

    memdelete(made.owner);
}

TEST_CASE(
    "[Networked][Entity][Hosted] CM2 registering one component twice leaves "
    "one id, because a second registration is not a second component"
) {
    Owned made = an_entity();
    Node *gun = a_child(made.owner, "Gun");

    made.entity->register_component(gun);
    made.entity->register_component(gun);
    made.entity->hydrate_components();

    NETW_CHECK_EQ(made.entity->comp_table().id_for_path(String("Gun")), 1);
    NETW_CHECK_EQ(made.entity->comp_table().path_for_id(2).is_empty(), true);

    memdelete(made.owner);
}

TEST_CASE(
    "[Networked][Entity][Hosted] CM3 an authority adopts its own digest as "
    "the wire's, so a server never poisons itself"
) {
    Owned made = an_entity();
    a_child(made.owner, "Gun");
    made.entity->register_component(made.owner->get_child(0));
    made.entity->comp_table().set_wire_hash(9999);

    made.entity->hydrate_components();

    NETW_CHECK_EQ(made.entity->get_comps_poisoned(), false);
    NETW_CHECK_EQ(
        made.entity->comp_table().get_wire_hash(),
        made.entity->comp_table().get_table_hash()
    );

    memdelete(made.owner);
}

TEST_CASE(
    "[Networked][Entity][Hosted] CM4 the same structure digests the same "
    "twice, so a peer that agrees computes an equal hash"
) {
    Owned first = an_entity();
    Owned second = an_entity();
    first.entity->register_component(a_child(first.owner, "Gun"));
    second.entity->register_component(a_child(second.owner, "Gun"));

    first.entity->hydrate_components();
    second.entity->hydrate_components();

    NETW_CHECK_EQ(
        first.entity->comp_table().get_table_hash(),
        second.entity->comp_table().get_table_hash()
    );
    NETW_CHECK_EQ(first.entity->comp_table().get_table_hash() != 0, true);

    memdelete(first.owner);
    memdelete(second.owner);
}

TEST_CASE(
    "[Networked][Entity][Hosted][SceneTree] CM5 a component that registers "
    "from its own tree_entered still rides a compact id, because the table "
    "reads the entity's structure only once the whole subtree has entered"
) {
    Node *owner = memnew(Node);
    owner->set_name("Owner");
    Node *gun = memnew(Node);
    gun->set_name("Gun");
    owner->add_child(gun);
    const Ref<netw::NetwEntity> entity = netw::NetwEntity::ensure(owner);
    entity->set_entity_id(StringName("gunner"));
    gun->connect(
        StringName("tree_entered"),
        callable_mp(entity.ptr(), &netw::NetwEntity::register_component)
            .bind(gun),
        Object::CONNECT_ONE_SHOT
    );

    netw::gd::scene_root()->add_child(owner);

    CHECK(entity->has_comp_path(NodePath("Gun")));
    NETW_CHECK_EQ(entity->comp_of(gun), 1);

    netw::gd::scene_root()->remove_child(owner);
    memdelete(owner);
}

TEST_CASE(
    "[Networked][Entity][Hosted][SceneTree] CM6 a component that joins after "
    "the entity is ready addresses by path forever, including across a "
    "re-entry, because the digest a peer agreed to cannot change under it"
) {
    Node *owner = memnew(Node);
    owner->set_name("Owner");
    const Ref<netw::NetwEntity> entity = netw::NetwEntity::ensure(owner);
    entity->set_entity_id(StringName("gunner"));
    netw::gd::scene_root()->add_child(owner);
    const int64_t agreed = entity->comp_table().get_table_hash();

    Node *gun = memnew(Node);
    gun->set_name("Gun");
    owner->add_child(gun);
    entity->register_component(gun);

    NETW_CHECK_EQ(entity->comp_of(gun), 255);
    NETW_CHECK_EQ(entity->comp_table().get_table_hash(), agreed);

    netw::gd::scene_root()->remove_child(owner);
    netw::gd::scene_root()->add_child(owner);

    NETW_CHECK_EQ(entity->comp_of(gun), 255);
    NETW_CHECK_EQ(entity->comp_table().get_table_hash(), agreed);

    netw::gd::scene_root()->remove_child(owner);
    memdelete(owner);
}

} // namespace TestNetwEntityComponentLaws
