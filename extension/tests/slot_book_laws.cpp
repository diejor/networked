#include "support/netw_test.h"

#include <cstdint>

#include "netw/api/netw_multiplayer.hpp"
#include "netw/repl/property_set.hpp"

namespace TestNetwSlotBookLaws {

using namespace godot;
using netw::repl::PropertySet;
using netw::repl::SetBook;

TEST_CASE(
    "[Networked][Store][Hosted] a set id read after its set leaves answers "
    "gone, and the slot it frees comes back under a new id"
) {
    SetBook sets;
    const uint64_t first = sets.create();
    REQUIRE(sets.get(first) != nullptr);
    sets.get(first)->record = PropertySet::RECORD_STATE;

    const bool freed = sets.free(first);
    CHECK(freed);
    const bool gone_after_free = sets.get(first) == nullptr;
    CHECK(gone_after_free);
    const bool freed_twice = sets.free(first);
    CHECK_FALSE(freed_twice);

    const uint64_t second = sets.create();
    const bool same_slot
        = (second & 0xffffffffu) == (first & 0xffffffffu);
    CHECK(same_slot);
    const bool new_id = second != first;
    CHECK(new_id);
    const bool still_gone = sets.get(first) == nullptr;
    CHECK(still_gone);
    REQUIRE(sets.get(second) != nullptr);
    NETW_CHECK_EQ(sets.get(second)->record, int64_t(PropertySet::RECORD_NONE));
    const bool none_is_gone = sets.get(SetBook::NONE) == nullptr;
    CHECK(none_is_gone);
}

TEST_CASE(
    "[Networked][Store][Hosted] a session's set ids answer gone once the "
    "session clears its property sets"
) {
    Ref<netw::NetwMultiplayer> session;
    session.instantiate();
    SetBook &sets = session->property_set_book();
    const uint64_t held = sets.create();
    REQUIRE(sets.get(held) != nullptr);

    session->property_set_clear();
    const bool gone = sets.get(held) == nullptr;
    CHECK(gone);
    NETW_CHECK_EQ(int64_t(sets.count()), int64_t(0));
    const uint64_t again = sets.create();
    const bool new_id = again != held;
    CHECK(new_id);
    const bool still_gone = sets.get(held) == nullptr;
    CHECK(still_gone);
}

} // namespace TestNetwSlotBookLaws
