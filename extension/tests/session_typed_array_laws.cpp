#include "support/netw_test.h"

#include "godot/variant.hpp"
#include "netw/api/netw_multiplayer.hpp"

namespace TestNetwSessionTypedArrayLaws {

using namespace godot;
using netw::NetwMultiplayer;

struct TypingRow {
    const char *verb;
    Variant::Type builtin;
    const char *element;
    Array answered;
};

TEST_CASE(
    "[Networked][Session][Hosted] TA1 every listing verb answers an "
    "element-typed array, so a caller assigns it without copying it row "
    "by row"
) {
    Ref<NetwMultiplayer> core;
    core.instantiate();

    const TypingRow rows[] = {
        {"event_watches", Variant::DICTIONARY, "", core->event_watches()},
        {"event_ring", Variant::DICTIONARY, "", core->event_ring(0)},
        {"scene_list", Variant::RID, "", core->scene_list()},
        {"scene_find_all",
         Variant::RID,
         "",
         core->scene_find_all(StringName("nowhere"))},
        {"scene_get_entities",
         Variant::RID,
         "",
         core->scene_get_entities(RID())},
        {"interest_get_membership",
         Variant::RID,
         "",
         core->interest_get_membership(RID())},
    };

    for (const TypingRow &row : rows) {
        CAPTURE(row.verb);
        CHECK(row.answered.is_typed());
        NETW_CHECK_EQ(
            int64_t(row.answered.get_typed_builtin()),
            int64_t(row.builtin)
        );
        NETW_FORMAT_TEXT(
            answered_element,
            String(row.answered.get_typed_class_name()).utf8().get_data()
        );
        CAPTURE(answered_element);
        CHECK(
            String(row.answered.get_typed_class_name()) == String(row.element)
        );
    }
}

} // namespace TestNetwSessionTypedArrayLaws
