#include "support/netw_test.h"

#if defined(NETW_TIER_HOSTED)

#include "godot/node.hpp"
#include "godot/script.hpp"
#include "netw/script/cache.hpp"

namespace TestNetwCache {

using namespace godot;
using netw::script::Cache;
using netw::script::MethodArity;

#define SKC_CHECK_TEXT(m_actual, m_expected) \
    do { \
        const String skc_actual = (m_actual); \
        NETW_FORMAT_TEXT(skc_actual_text, skc_actual.utf8().get_data()); \
        CAPTURE(skc_actual_text); \
        CHECK(bool(skc_actual == String(m_expected))); \
    } while (0)

const char *LEAF_SCRIPT = "res://tests/support/chains/member_leaf.gd";

Ref<Script> a_script() {
    return ResourceLoader::get_singleton()->load(String(LEAF_SCRIPT));
}

String joined(const LocalVector<StringName> &p_names) {
    String out;
    for (uint32_t index = 0; index < p_names.size(); index++) {
        if (index > 0) {
            out += String(",");
        }
        out += String(p_names[index]);
    }
    return out;
}

String joined_types(const LocalVector<int> &p_types) {
    String out;
    for (uint32_t index = 0; index < p_types.size(); index++) {
        if (index > 0) {
            out += String(",");
        }
        out += String::num_int64(p_types[index]);
    }
    return out;
}

bool holds(const LocalVector<StringName> &p_names, const char *p_name) {
    for (uint32_t index = 0; index < p_names.size(); index++) {
        if (String(p_names[index]) == String(p_name)) {
            return true;
        }
    }
    return false;
}

Node *a_probe(const Ref<Script> &p_script) {
    Node *node = memnew(Node);
    node->set_script(p_script);
    return node;
}

TEST_CASE(
    "[Networked][Session][Declared] SKC1 methods, properties and signals are "
    "ordered by the TEXT of their names, never by declaration order and never "
    "by the interning order a StringName sort would compare, because an id "
    "minted from interning order names a different member on each peer"
) {
    Cache cache(a_script());

    SKC_CHECK_TEXT(
        joined(cache.text_ordered_methods()),
        "shared_call,yankee_leaf_call,zulu_base_call"
    );
    SKC_CHECK_TEXT(
        joined(cache.text_ordered_properties()),
        "alpha_base_field,bravo_leaf_field,yankee_leaf_field,zulu_base_field"
    );
    SKC_CHECK_TEXT(
        joined(cache.text_ordered_signals()),
        "alpha_event,mike_event,zulu_event"
    );
}

TEST_CASE(
    "[Networked][Session][Declared] SKC2 a method answers the argument types "
    "of its declaration and an arity of [total - defaults, total], so a "
    "default argument moves the minimum and leaves the maximum alone"
) {
    Cache cache(a_script());

    SKC_CHECK_TEXT(
        joined_types(cache.arg_types(StringName("yankee_leaf_call"))),
        "4,2"
    );
    const MethodArity leaf = cache.arity(StringName("yankee_leaf_call"));
    CHECK(leaf.declared);
    NETW_CHECK_EQ(leaf.minimum, 1);
    NETW_CHECK_EQ(leaf.maximum, 2);

    const MethodArity base = cache.arity(StringName("zulu_base_call"));
    CHECK(base.declared);
    NETW_CHECK_EQ(base.minimum, 1);
    NETW_CHECK_EQ(base.maximum, 1);

    SKC_CHECK_TEXT(
        joined_types(cache.signal_arg_types(StringName("alpha_event"))),
        "1,4"
    );
    SKC_CHECK_TEXT(
        joined_types(cache.signal_arg_types(StringName("mike_event"))),
        "3"
    );
}

TEST_CASE(
    "[Networked][Session][Declared] SKC3 the base script chain is walked, and "
    "where both halves declare a name the most-derived declaration is the one "
    "that answers"
) {
    Cache cache(a_script());

    const MethodArity shared = cache.arity(StringName("shared_call"));
    CHECK(shared.declared);
    NETW_CHECK_EQ(shared.minimum, 1);
    NETW_CHECK_EQ(shared.maximum, 2);

    SKC_CHECK_TEXT(
        joined_types(cache.signal_arg_types(StringName("zulu_event"))),
        "2"
    );
    SKC_CHECK_TEXT(
        joined_types(cache.arg_types(StringName("zulu_base_call"))),
        "2"
    );
}

TEST_CASE(
    "[Networked][Session][Declared] SKC4 a method, property or signal the "
    "script never declared answers empty rather than failing, because the "
    "reflection reader is asked about names a peer chose"
) {
    Cache cache(a_script());
    Node *node = a_probe(cache.script());

    CHECK(cache.arg_types(StringName("never_declared")).is_empty());
    const MethodArity unknown = cache.arity(StringName("never_declared"));
    CHECK_FALSE(unknown.declared);
    NETW_CHECK_EQ(unknown.minimum, 0);
    NETW_CHECK_EQ(unknown.maximum, 0);
    CHECK(cache.signal_arg_types(StringName("never_declared")).is_empty());
    NETW_CHECK_EQ(
        cache.prop_type(node, StringName("never_declared")),
        int(Variant::NIL)
    );

    memdelete(node);
}

TEST_CASE(
    "[Networked][Session][Declared] SKC5 the ordered property names are the "
    "script-declared variables alone, so the engine rows a script's property "
    "list also carries never take a property id"
) {
    Cache cache(a_script());

    NETW_CHECK_EQ(int(cache.text_ordered_properties().size()), 4);
    CHECK(holds(cache.text_ordered_properties(), "yankee_leaf_field"));
    CHECK(holds(cache.text_ordered_properties(), "alpha_base_field"));
    CHECK_FALSE(holds(cache.text_ordered_properties(), "probe_leaf.gd"));
    CHECK_FALSE(holds(cache.text_ordered_properties(), "probe_base.gd"));
}

TEST_CASE(
    "[Networked][Session][Declared] SKC6 every answer is computed once and "
    "then held, so a repeat ask returns what the first ask resolved rather "
    "than re-reading the object it was resolved from"
) {
    Cache cache(a_script());
    Node *node = a_probe(cache.script());

    const int resolved = cache.prop_type(node, StringName("yankee_leaf_field"));
    memdelete(node);
    NETW_CHECK_EQ(
        cache.prop_type(nullptr, StringName("yankee_leaf_field")),
        resolved
    );

    SKC_CHECK_TEXT(
        joined(cache.text_ordered_methods()),
        joined(cache.text_ordered_methods())
    );
    SKC_CHECK_TEXT(
        joined(cache.text_ordered_signals()),
        joined(cache.text_ordered_signals())
    );
}

TEST_CASE(
    "[Networked][Session][Declared] SKC7 the live node is what a property "
    "type is read from, and a scriptless cache answers an empty rpc config "
    "and no ordered names at all"
) {
    Cache cache(a_script());
    Node *node = a_probe(cache.script());

    NETW_CHECK_EQ(
        cache.prop_type(node, StringName("yankee_leaf_field")),
        int(Variant::VECTOR3)
    );
    NETW_CHECK_EQ(
        cache.prop_type(node, StringName("bravo_leaf_field")),
        int(Variant::FLOAT)
    );
    NETW_CHECK_EQ(
        cache.prop_type(node, StringName("alpha_base_field")),
        int(Variant::STRING)
    );
    memdelete(node);

    Cache scriptless((Ref<Script>()));
    CHECK(scriptless.rpc_config().is_empty());
    CHECK(scriptless.text_ordered_methods().is_empty());
    CHECK(scriptless.text_ordered_properties().is_empty());
    CHECK(scriptless.text_ordered_signals().is_empty());
    CHECK(scriptless.arg_types(StringName("shared_call")).is_empty());
}

#undef SKC_CHECK_TEXT

} // namespace TestNetwCache

#endif
