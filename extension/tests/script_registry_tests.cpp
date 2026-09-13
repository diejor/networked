#include "support/netw_test.h"

#if defined(NETW_TIER_HOSTED)

#include <godot_cpp/classes/class_db_singleton.hpp>

#include "godot/callable.hpp"
#include "godot/object.hpp"
#include "godot/script.hpp"
#include "netw/api/despawn_config.hpp"
#include "netw/api/member_config.hpp"
#include "netw/api/persistence_config.hpp"
#include "netw/api/property_config.hpp"
#include "netw/scene_decl.hpp"
#include "netw/script/registry.hpp"

namespace TestNetwScriptRegistry {

using namespace godot;
using netw::NetwDespawnConfig;
using netw::NetwMemberConfig;
using netw::NetwPersistenceConfig;
using netw::NetwPropertyConfig;
using netw::SceneDecl;
namespace registry = netw::script::registry;

const char *CHAIN_BASE = "res://tests/support/chains/declaration_base.gd";
const char *HERITAGE_BASE = "res://tests/support/chains/heritage_base.gd";

Ref<Script> a_script() {
    const Ref<Script> made
        = ClassDBSingleton::get_singleton()->instantiate("GDScript");
    return made;
}

Ref<Script> a_script_extending(const char *p_base_path) {
    const Ref<Script> made = a_script();
    made->set_source_code(
        String("extends \"") + String(p_base_path) + String("\"\n")
    );
    made->reload();
    return made;
}

Ref<NetwPropertyConfig> a_property_config(
    const Ref<Script> &p_script,
    const StringName &p_member
) {
    Ref<NetwPropertyConfig> config;
    config.instantiate();
    config->set_context_script(p_script);
    config->set_context_name(p_member);
    config->set_context_type(1);
    return config;
}

Ref<NetwDespawnConfig> a_despawn_config() {
    Ref<NetwDespawnConfig> config;
    config.instantiate();
    return config;
}

bool alive(ObjectID p_id) {
    return netw::gd::object_of(p_id) != nullptr;
}

bool nothing_declared(const Variant &p_answer) {
    return p_answer.get_type() == Variant::NIL;
}

TEST_CASE(
    "[Networked][Session][Declared] SR1 a script nothing was declared on "
    "carries no book, and every reader answers nothing declared rather than "
    "refusing"
) {
    const Ref<Script> script = a_script();

    CHECK(nothing_declared(
        registry::member_config(
            script,
            registry::MEMBER_RPC,
            StringName("sr1_member")
        )
    ));
    CHECK(nothing_declared(
        registry::member_config(
            script,
            registry::MEMBER_PROPERTY,
            StringName("sr1_member")
        )
    ));
    NETW_CHECK_EQ(
        registry::member_configs(script, registry::MEMBER_SIGNAL).size(),
        0
    );
    NETW_CHECK_EQ(
        registry::member_configs(script, registry::MEMBER_SPAWN).size(),
        0
    );
    CHECK(nothing_declared(
        registry::script_config(script, registry::SCRIPT_DESPAWN)
    ));
    CHECK(nothing_declared(
        registry::script_config(script, registry::SCRIPT_PERSISTENCE)
    ));
    CHECK_FALSE(registry::scene_decl(script).declared);
}

TEST_CASE(
    "[Networked][Session][Declared] SR2 declaring on no script is a no-op and "
    "every reader still answers nothing declared, because a scriptless body "
    "carries its declaration beside itself instead"
) {
    const Ref<Script> absent;

    registry::declare_member(
        absent,
        registry::MEMBER_PROPERTY,
        StringName("sr2_member"),
        a_property_config(absent, StringName("sr2_member"))
    );
    registry::declare_script_config(
        absent,
        registry::SCRIPT_DESPAWN,
        a_despawn_config()
    );
    SceneDecl decl;
    decl.declared = true;
    registry::declare_scene(absent, decl);

    CHECK(nothing_declared(
        registry::member_config(
            absent,
            registry::MEMBER_PROPERTY,
            StringName("sr2_member")
        )
    ));
    NETW_CHECK_EQ(
        registry::member_configs(absent, registry::MEMBER_PROPERTY).size(),
        0
    );
    CHECK(nothing_declared(
        registry::script_config(absent, registry::SCRIPT_DESPAWN)
    ));
    CHECK_FALSE(registry::scene_decl(absent).declared);
}

TEST_CASE(
    "[Networked][Session][Declared] SR3 a first declaration creates the book "
    "and a second adds to the same one, so declaring a second member never "
    "forgets the first"
) {
    const Ref<Script> script = a_script();
    const Ref<NetwPropertyConfig> first
        = a_property_config(script, StringName("sr3_first"));
    const Ref<NetwPropertyConfig> second
        = a_property_config(script, StringName("sr3_second"));

    registry::declare_member(
        script,
        registry::MEMBER_PROPERTY,
        StringName("sr3_first"),
        first
    );
    registry::declare_member(
        script,
        registry::MEMBER_PROPERTY,
        StringName("sr3_second"),
        second
    );

    NETW_CHECK_EQ(
        registry::member_configs(script, registry::MEMBER_PROPERTY).size(),
        2
    );
    const Ref<NetwMemberConfig> read = registry::member_config(
        script,
        registry::MEMBER_PROPERTY,
        StringName("sr3_first")
    );
    CHECK(bool(read == first));
}

TEST_CASE(
    "[Networked][Session][Declared] SR4 the four member books are separate, so "
    "an RPC, a property, a signal and a spawn function sharing one name each "
    "answer their own declaration"
) {
    const Ref<Script> script = a_script();
    const StringName shared = StringName("sr4_name");
    const Ref<NetwPropertyConfig> declared = a_property_config(script, shared);

    registry::declare_member(
        script,
        registry::MEMBER_PROPERTY,
        shared,
        declared
    );

    const Ref<NetwMemberConfig> as_property
        = registry::member_config(script, registry::MEMBER_PROPERTY, shared);
    CHECK(bool(as_property == declared));
    CHECK(nothing_declared(
        registry::member_config(script, registry::MEMBER_RPC, shared)
    ));
    CHECK(nothing_declared(
        registry::member_config(script, registry::MEMBER_SIGNAL, shared)
    ));
    CHECK(nothing_declared(
        registry::member_config(script, registry::MEMBER_SPAWN, shared)
    ));
}

TEST_CASE(
    "[Networked][Session][Declared] SR5 a despawn and a persistence "
    "declaration are found by walking the base scripts, and a script carrying "
    "its own answers that one rather than the inherited one"
) {
    const Ref<Script> leaf = a_script_extending(CHAIN_BASE);
    const Ref<Script> base = leaf->get_base_script();
    CHECK(base.is_valid());

    const Ref<NetwDespawnConfig> base_despawn = a_despawn_config();
    Ref<NetwPersistenceConfig> base_persistence;
    base_persistence.instantiate();
    registry::declare_script_config(
        base,
        registry::SCRIPT_DESPAWN,
        base_despawn
    );
    registry::declare_script_config(
        base,
        registry::SCRIPT_PERSISTENCE,
        base_persistence
    );

    const Ref<NetwDespawnConfig> inherited
        = registry::script_config(leaf, registry::SCRIPT_DESPAWN);
    CHECK(bool(inherited == base_despawn));
    const Ref<NetwPersistenceConfig> inherited_persistence
        = registry::script_config(leaf, registry::SCRIPT_PERSISTENCE);
    CHECK(bool(inherited_persistence == base_persistence));

    const Ref<NetwDespawnConfig> leaf_despawn = a_despawn_config();
    registry::declare_script_config(
        leaf,
        registry::SCRIPT_DESPAWN,
        leaf_despawn
    );

    const Ref<NetwDespawnConfig> derived
        = registry::script_config(leaf, registry::SCRIPT_DESPAWN);
    CHECK(bool(derived == leaf_despawn));
    const Ref<NetwDespawnConfig> at_base
        = registry::script_config(base, registry::SCRIPT_DESPAWN);
    CHECK(bool(at_base == base_despawn));
}

TEST_CASE(
    "[Networked][Session][Declared] SR6 a member declaration and a scene "
    "declaration are NOT walked to, so a base script's members and scene "
    "declaration answer for the base alone"
) {
    const Ref<Script> leaf = a_script_extending(HERITAGE_BASE);
    const Ref<Script> base = leaf->get_base_script();
    CHECK(base.is_valid());
    const StringName member = StringName("sr6_member");

    registry::declare_member(
        base,
        registry::MEMBER_PROPERTY,
        member,
        a_property_config(base, member)
    );
    SceneDecl decl;
    decl.declared = true;
    decl.label = StringName("sr6_base");
    registry::declare_scene(base, decl);

    CHECK(nothing_declared(
        registry::member_config(leaf, registry::MEMBER_PROPERTY, member)
    ));
    NETW_CHECK_EQ(
        registry::member_configs(leaf, registry::MEMBER_PROPERTY).size(),
        0
    );
    CHECK_FALSE(registry::scene_decl(leaf).declared);
    CHECK(registry::scene_decl(base).declared);
    CHECK(registry::scene_decl(base).label == StringName("sr6_base"));
}

TEST_CASE(
    "[Networked][Session][Declared] SR7 two scripts each declared their own "
    "scene, and a script asked for its declaration answers only its own"
) {
    const Ref<Script> first = a_script();
    const Ref<Script> second = a_script();
    SceneDecl first_decl;
    first_decl.declared = true;
    first_decl.label = StringName("Arena");
    first_decl.isolation = 1;
    SceneDecl second_decl;
    second_decl.declared = true;
    second_decl.label = StringName("Lobby");

    registry::declare_scene(first, first_decl);
    registry::declare_scene(second, second_decl);

    CHECK(registry::scene_decl(first).declared);
    CHECK(registry::scene_decl(first).label == StringName("Arena"));
    NETW_CHECK_EQ(registry::scene_decl(first).isolation, 1);
    CHECK(registry::scene_decl(second).declared);
    CHECK(registry::scene_decl(second).label == StringName("Lobby"));
    NETW_CHECK_EQ(registry::scene_decl(second).isolation, 0);
}

TEST_CASE(
    "[Networked][Session][Declared] SR8 a script nothing was declared on is "
    "freed when the last reference to it drops, which is what makes the two "
    "laws below a measurement rather than a hope"
) {
    ObjectID script_id;
    {
        const Ref<Script> script = a_script();
        script_id = netw::gd::instance_id(script.ptr());
        CHECK(alive(script_id));
    }
    CHECK_FALSE(alive(script_id));
}

TEST_CASE(
    "[Networked][Session][Declared] SR9 a script and the member configs "
    "declared on it are both freed when the last reference to the script "
    "drops, because a config names its declaring script without owning it"
) {
    ObjectID script_id;
    ObjectID config_id;
    {
        const Ref<Script> script = a_script();
        script_id = netw::gd::instance_id(script.ptr());
        const Ref<NetwPropertyConfig> config
            = a_property_config(script, StringName("sr9_member"));
        config_id = netw::gd::instance_id(config.ptr());
        registry::declare_member(
            script,
            registry::MEMBER_PROPERTY,
            StringName("sr9_member"),
            config
        );
        CHECK(bool(config->get_context_script() == script));
        CHECK(alive(script_id));
        CHECK(alive(config_id));
    }
    CHECK_FALSE(alive(script_id));
    CHECK_FALSE(alive(config_id));
}

TEST_CASE(
    "[Networked][Session][Declared] SR10 a script declared with a scene is "
    "freed when the last reference to it drops, because a scene declaration "
    "is plain data carried on the script rather than an object of its own"
) {
    ObjectID script_id;
    {
        const Ref<Script> script = a_script();
        script_id = netw::gd::instance_id(script.ptr());
        SceneDecl decl;
        decl.declared = true;
        decl.label = StringName("sr10");
        registry::declare_scene(script, decl);
        CHECK(registry::scene_decl(script).declared);
        CHECK(alive(script_id));
    }
    CHECK_FALSE(alive(script_id));
}

} // namespace TestNetwScriptRegistry

#endif
