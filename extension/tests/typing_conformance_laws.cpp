#include "support/netw_test.h"

#if defined(NETW_GDEXTENSION)

#include <godot_cpp/classes/class_db_singleton.hpp>

#include "godot/templates.hpp"
#include "godot/variant.hpp"
#include "support/published_classes.h"

namespace TestNetwTypingConformance {

using namespace godot;

bool names_a_concrete_class(const String &p_class_name) {
    return !p_class_name.is_empty() && p_class_name != String("Object")
        && p_class_name != String("RefCounted")
        && p_class_name != String("Variant");
}

bool is_layout_row(int64_t p_usage) {
    return (p_usage
            & (PROPERTY_USAGE_GROUP | PROPERTY_USAGE_SUBGROUP
               | PROPERTY_USAGE_CATEGORY))
        != 0;
}

String untyped_property(const String &p_class, const Dictionary &p_property) {
    const int64_t usage = p_property.get("usage", int64_t(0));
    if (is_layout_row(usage)) {
        return String();
    }
    const int64_t type = p_property.get("type", int64_t(0));
    const String named = p_property.get("class_name", String());
    const String where
        = p_class + String(".") + String(p_property.get("name", ""));
    if (type == int64_t(Variant::OBJECT)) {
        return names_a_concrete_class(named)
            ? String()
            : where + String(" is OBJECT but names '") + named + String("'");
    }
    if (type == int64_t(Variant::NIL)) {
        return (usage & PROPERTY_USAGE_NIL_IS_VARIANT) != 0 ? String()
                                                            : where
                + String(" is NIL without NIL_IS_VARIANT, so a typed getter "
                         "reads as Variant");
    }
    return String();
}

String untyped_return(const String &p_class, const Dictionary &p_method) {
    const Dictionary answered = p_method.get("return", Dictionary());
    if (int64_t(answered.get("type", int64_t(0))) != int64_t(Variant::OBJECT)) {
        return String();
    }
    const String named = answered.get("class_name", String());
    if (names_a_concrete_class(named)) {
        return String();
    }
    return p_class + String(".") + String(p_method.get("name", ""))
        + String("() answers '") + named + String("'");
}

PackedStringArray offenders() {
    PackedStringArray out;
    ClassDBSingleton *classes = ClassDBSingleton::get_singleton();
    const PackedStringArray published = netw_test::published_classes();
    for (int at = 0; at < published.size(); ++at) {
        const String named = published[at];
        const TypedArray<Dictionary> properties
            = classes->class_get_property_list(named, true);
        for (int row = 0; row < properties.size(); ++row) {
            const String verdict = untyped_property(named, properties[row]);
            if (!verdict.is_empty()) {
                out.push_back(verdict);
            }
        }
        const TypedArray<Dictionary> methods
            = classes->class_get_method_list(named, true);
        for (int row = 0; row < methods.size(); ++row) {
            const String verdict = untyped_return(named, methods[row]);
            if (!verdict.is_empty()) {
                out.push_back(verdict);
            }
        }
    }
    return out;
}

TEST_CASE(
    "[Networked][Conformance][Hosted] TC1 the registration set the library "
    "publishes is what this law reads, so a class registered tomorrow is "
    "judged with no edit here"
) {
    const PackedStringArray published = netw_test::published_classes();
    CHECK_FALSE(published.is_empty());
    CHECK(published.has(String("NetwMultiplayer")));
    CHECK(published.has(String("NetwEntity")));
    CHECK_FALSE(published.has(String("Node")));
    CHECK_FALSE(published.has(String("NetwTestAuthFlow")));
}

TEST_CASE(
    "[Networked][Conformance][Hosted] TC2 every published object member "
    "resolves to a concrete class in GDScript: an OBJECT property and an "
    "object-returning method name the class they answer, and a NIL property "
    "declares itself a Variant rather than lying about a typed getter"
) {
    const PackedStringArray found = offenders();
    for (int at = 0; at < found.size(); ++at) {
        NETW_FORMAT_TEXT(untyped, String(found[at]).utf8().get_data());
        MESSAGE(untyped);
    }
    NETW_CHECK_EQ(int(found.size()), 0);
}

} // namespace TestNetwTypingConformance

#endif
