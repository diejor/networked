#include "support/netw_test.h"

#if defined(NETW_GDEXTENSION)

#include <godot_cpp/classes/class_db_singleton.hpp>

#include "godot/templates.hpp"
#include "godot/variant.hpp"

namespace TestNetwSessionSubstitution {

using namespace godot;

const char *STOCK = "SceneMultiplayer";
const char *SUBSTITUTE = "NetwMultiplayer";

bool is_layout_row(int64_t p_usage) {
    return (p_usage
            & (PROPERTY_USAGE_GROUP | PROPERTY_USAGE_SUBGROUP
               | PROPERTY_USAGE_CATEGORY))
        != 0;
}

String type_name(int64_t p_type) {
    return Variant::get_type_name(Variant::Type(p_type));
}

String signature(const Dictionary &p_method) {
    String out = String(p_method.get("name", "")) + String("(");
    const Array arguments = p_method.get("args", Array());
    for (int at = 0; at < arguments.size(); ++at) {
        const Dictionary argument = arguments[at];
        if (at > 0) {
            out += String(", ");
        }
        out += String(argument.get("name", "")) + String(": ")
            + type_name(argument.get("type", int64_t(0)));
    }
    const Array defaults = p_method.get("default_args", Array());
    out += String(") default ") + String::num_int64(defaults.size());
    const Dictionary answered = p_method.get("return", Dictionary());
    return out + String(" -> ") + type_name(answered.get("type", int64_t(0)));
}

Dictionary method_named(const String &p_class, const String &p_name) {
    const TypedArray<Dictionary> methods
        = ClassDBSingleton::get_singleton()->class_get_method_list(
            p_class,
            false
        );
    for (int row = 0; row < methods.size(); ++row) {
        const Dictionary found = methods[row];
        if (String(found.get("name", "")) == p_name) {
            return found;
        }
    }
    return Dictionary();
}

void collect_methods(PackedStringArray &r_gaps) {
    ClassDBSingleton *classes = ClassDBSingleton::get_singleton();
    const TypedArray<Dictionary> stock
        = classes->class_get_method_list(STOCK, true);
    for (int row = 0; row < stock.size(); ++row) {
        const Dictionary declared = stock[row];
        const String named = declared.get("name", "");
        if (!classes->class_has_method(SUBSTITUTE, named)) {
            r_gaps.push_back(
                String(SUBSTITUTE) + String(" publishes no ") + named
                + String("(), so a caller that reaches for ") + String(STOCK)
                + String("'s spelling finds nothing")
            );
            continue;
        }
        const String mine = signature(method_named(SUBSTITUTE, named));
        const String theirs = signature(declared);
        if (mine != theirs) {
            r_gaps.push_back(
                String(STOCK) + String(" declares ") + theirs + String(" and ")
                + String(SUBSTITUTE) + String(" declares ") + mine
            );
        }
    }
}

void collect_properties(PackedStringArray &r_gaps) {
    ClassDBSingleton *classes = ClassDBSingleton::get_singleton();
    const TypedArray<Dictionary> stock
        = classes->class_get_property_list(STOCK, true);
    for (int row = 0; row < stock.size(); ++row) {
        const Dictionary declared = stock[row];
        if (is_layout_row(declared.get("usage", int64_t(0)))) {
            continue;
        }
        const String named = declared.get("name", "");
        const String setter
            = classes->class_get_property_setter(SUBSTITUTE, named);
        const String getter
            = classes->class_get_property_getter(SUBSTITUTE, named);
        if (setter.is_empty() && getter.is_empty()) {
            r_gaps.push_back(
                String(SUBSTITUTE) + String(" publishes no ") + named
                + String(" property")
            );
            continue;
        }
        const String want_setter
            = classes->class_get_property_setter(STOCK, named);
        const String want_getter
            = classes->class_get_property_getter(STOCK, named);
        if (setter != want_setter || getter != want_getter) {
            r_gaps.push_back(
                named + String(" is accessed through ") + want_setter
                + String("/") + want_getter + String(" on ") + String(STOCK)
                + String(" and through ") + setter + String("/") + getter
                + String(" on ") + String(SUBSTITUTE)
            );
        }
    }
}

void collect_signals(PackedStringArray &r_gaps) {
    ClassDBSingleton *classes = ClassDBSingleton::get_singleton();
    const TypedArray<Dictionary> stock
        = classes->class_get_signal_list(STOCK, true);
    for (int row = 0; row < stock.size(); ++row) {
        const Dictionary declared = stock[row];
        const String named = declared.get("name", "");
        if (!classes->class_has_signal(SUBSTITUTE, named)) {
            r_gaps.push_back(
                String(SUBSTITUTE) + String(" emits no ") + named
                + String(" signal")
            );
        }
    }
}

PackedStringArray gaps() {
    PackedStringArray out;
    collect_methods(out);
    collect_properties(out);
    collect_signals(out);
    return out;
}

TEST_CASE(
    "[Networked][Session][Substitution] S1 the stock class this one "
    "substitutes for is registered, so an empty reading means the surfaces "
    "matched rather than that nothing was compared"
) {
    ClassDBSingleton *classes = ClassDBSingleton::get_singleton();
    CHECK(classes->class_exists(STOCK));
    CHECK(classes->class_exists(SUBSTITUTE));
    CHECK(classes->class_get_method_list(STOCK, true).size() > 0);
    CHECK(classes->class_get_property_list(STOCK, true).size() > 0);
    CHECK(classes->class_get_signal_list(STOCK, true).size() > 0);
}

TEST_CASE(
    "[Networked][Session][Substitution] S2 every method, property accessor "
    "and signal SceneMultiplayer declares is published here under the same "
    "name and the same signature, so a game swapping one for the other "
    "keeps every call site it already wrote"
) {
    const PackedStringArray found = gaps();
    for (int at = 0; at < found.size(); ++at) {
        NETW_FORMAT_TEXT(gap, String(found[at]).utf8().get_data());
        MESSAGE(gap);
    }
    NETW_CHECK_EQ(int(found.size()), 0);
}

} // namespace TestNetwSessionSubstitution

#endif
