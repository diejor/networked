#include "support/netw_test.h"

#if defined(NETW_TIER_HOSTED)

#include <godot_cpp/classes/class_db_singleton.hpp>

#include "godot/callable.hpp"
#include "godot/node.hpp"
#include "godot/script.hpp"
#include "netw/api/interpolate.hpp"
#include "netw/api/property_config.hpp"
#include "netw/script/model.hpp"

namespace TestNetwScriptModel {

using namespace godot;
using netw::NetwInterpolate;
using netw::NetwPropertyConfig;
namespace script_model = netw::script::model;

const char *LEAF_SCRIPT = "res://tests/support/chains/member_leaf.gd";

#define SCM_CHECK_TEXT(m_actual, m_expected) \
    do { \
        const String scm_actual = (m_actual); \
        NETW_FORMAT_TEXT(scm_actual_text, scm_actual.utf8().get_data()); \
        CAPTURE(scm_actual_text); \
        CHECK(bool(scm_actual == String(m_expected))); \
    } while (0)

Ref<Script> a_leaf_script() {
    return ResourceLoader::get_singleton()->load(String(LEAF_SCRIPT));
}

Ref<Script> a_bare_script() {
    const Ref<Script> made
        = ClassDBSingleton::get_singleton()->instantiate("GDScript");
    made->set_source_code(String("extends Node\n"));
    made->reload();
    return made;
}

Node *a_body(const Ref<Script> &p_script) {
    Node *node = memnew(Node);
    if (p_script.is_valid()) {
        node->set_script(p_script);
    }
    return node;
}

Ref<NetwPropertyConfig> a_declaration(
    const Ref<Script> &p_script,
    const StringName &p_member
) {
    Ref<NetwPropertyConfig> config;
    config.instantiate();
    config->set_context_script(p_script);
    config->set_context_name(p_member);
    config->set_context_type(1);
    script_model::declare_property_config(p_script, p_member, config);
    return config;
}

Ref<NetwPropertyConfig> answered_config(
    const Dictionary &p_answered,
    const StringName &p_member
) {
    return Ref<NetwPropertyConfig>(p_answered.get(p_member, Variant()));
}

TEST_CASE(
    "[Networked][Session][Declared] SCM1 a node overlay entry never undeclares "
    "the field its script declared, because a declaration is a fact about "
    "every instance and the overlay is what one body says for itself"
) {
    const Ref<Script> script = a_bare_script();
    Node *body = a_body(script);
    const StringName member("scm1_field");
    const Ref<NetwPropertyConfig> declared = a_declaration(script, member);

    const Ref<NetwPropertyConfig> overlaid
        = script_model::configure_node_property(body, member);

    CHECK(overlaid.is_valid());
    CHECK(bool(overlaid != declared));
    const Dictionary answered = script_model::get_node_property_configs(body);
    NETW_CHECK_EQ(answered.size(), 1);
    CHECK(bool(answered_config(answered, member) == declared));

    memdelete(body);
}

TEST_CASE(
    "[Networked][Session][Declared] SCM2 a field no script declared is "
    "answered "
    "by the overlay whole, which is the scriptless path where the overlay IS "
    "the declaration"
) {
    Node *body = a_body(Ref<Script>());
    const StringName member("scm2_field");

    const Ref<NetwPropertyConfig> made
        = script_model::configure_node_property(body, member);
    made->set_in_state_set(true);

    const Dictionary answered = script_model::get_node_property_configs(body);
    NETW_CHECK_EQ(answered.size(), 1);
    const Ref<NetwPropertyConfig> got = answered_config(answered, member);
    CHECK(bool(got == made));
    CHECK(bool(got.is_valid() && got->get_in_state_set()));

    memdelete(body);
}

TEST_CASE(
    "[Networked][Session][Declared] SCM3 a forward-model rule is a per-body "
    "delta beside the declaration, so two bodies of one script each answer "
    "with the rule bound to themselves"
) {
    const Ref<Script> script = a_bare_script();
    Node *first = a_body(script);
    Node *second = a_body(script);
    const StringName member("scm3_field");
    a_declaration(script, member);

    script_model::bind_node_property_carry(
        first,
        member,
        Callable(first, "get_name")
    );
    script_model::bind_node_property_carry(
        second,
        member,
        Callable(second, "get_name")
    );

    CHECK(
        bool(
            script_model::get_node_property_carry(first, member).get_object()
            == first
        )
    );
    CHECK(
        bool(
            script_model::get_node_property_carry(second, member).get_object()
            == second
        )
    );
    CHECK_FALSE(
        script_model::get_node_property_carry(first, StringName("scm3_other"))
            .is_valid()
    );
    const Dictionary answered = script_model::get_node_property_configs(first);
    NETW_CHECK_EQ(answered.size(), 1);

    memdelete(first);
    memdelete(second);
}

TEST_CASE(
    "[Networked][Session][Declared] SCM4 a dropped body's overlay answers "
    "nothing, and a freed body's entry can never answer for the next body "
    "because the engine never hands a freed instance id out again"
) {
    Node *body = a_body(Ref<Script>());
    const StringName member("scm4_field");
    const uint64_t was = uint64_t(body->get_instance_id());

    script_model::bind_node_property_carry(
        body,
        member,
        Callable(body, "get_name")
    );
    script_model::clear_node_overlay(body);
    CHECK_FALSE(script_model::get_node_property_carry(body, member).is_valid());

    script_model::bind_node_property_carry(
        body,
        member,
        Callable(body, "get_name")
    );
    memdelete(body);

    Node *next = a_body(Ref<Script>());
    CHECK(bool(uint64_t(next->get_instance_id()) != was));
    CHECK_FALSE(script_model::get_node_property_carry(next, member).is_valid());
    CHECK(script_model::get_node_property_configs(next).is_empty());

    memdelete(next);
    script_model::sweep_dead_overlays();
}

TEST_CASE(
    "[Networked][Session][Declared] SCM5 a 1-byte member id round-trips to the "
    "same name in TEXT order, which is the only order two peers that never "
    "exchange the name table both derive"
) {
    const Ref<Script> script = a_leaf_script();
    const char *IN_TEXT_ORDER[] = {
        "alpha_base_field",
        "bravo_leaf_field",
        "yankee_leaf_field",
        "zulu_base_field",
    };

    for (int64_t id = 1; id <= 4; id++) {
        const StringName named
            = script_model::get_property_name_by_id(script, id);
        SCM_CHECK_TEXT(String(named), IN_TEXT_ORDER[id - 1]);
        NETW_CHECK_EQ(script_model::get_property_id(script, named), id);
    }
    SCM_CHECK_TEXT(
        String(script_model::get_property_name_by_id(script, 0)),
        ""
    );
    SCM_CHECK_TEXT(
        String(script_model::get_property_name_by_id(script, 5)),
        ""
    );
    NETW_CHECK_EQ(
        script_model::get_property_id(script, StringName("never_declared")),
        0
    );
}

TEST_CASE(
    "[Networked][Session][Declared] SCM6 an argument count is judged against "
    "the declared arity, and a default argument moves the minimum alone"
) {
    const Ref<Script> script = a_leaf_script();
    const StringName method("shared_call");

    CHECK_FALSE(script_model::validate_argument_count(script, method, 0));
    CHECK(script_model::validate_argument_count(script, method, 1));
    CHECK(script_model::validate_argument_count(script, method, 2));
    CHECK_FALSE(script_model::validate_argument_count(script, method, 3));
    CHECK_FALSE(
        script_model::validate_argument_count(
            script,
            StringName("never_declared"),
            0
        )
    );
}

TEST_CASE(
    "[Networked][Session][Declared] SCM7 a curve is answered off the body "
    "before the script, so a runtime-declared display track overrides an "
    "authored one on one body without touching its siblings"
) {
    const Ref<Script> script = a_bare_script();
    Node *body = a_body(script);
    const StringName member("scm7_field");
    Ref<NetwInterpolate> authored;
    authored.instantiate();
    Ref<NetwInterpolate> per_instance;
    per_instance.instantiate();

    Array authored_specs;
    authored_specs.push_back(authored);
    a_declaration(script, member)->set_interpolators(authored_specs);
    Array instance_specs;
    instance_specs.push_back(per_instance);
    script_model::configure_node_property(body, member)
        ->set_interpolators(instance_specs);

    CHECK(
        bool(
            script_model::get_node_property_interpolator(body, member)
            == per_instance
        )
    );
    CHECK(
        bool(
            script_model::get_property_interpolator(script, member) == authored
        )
    );

    memdelete(body);
}

} // namespace TestNetwScriptModel

#endif
