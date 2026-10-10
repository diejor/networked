#include "support/netw_test.h"

#include "godot/ref_counted.hpp"
#include "support/netw_reset.h"

#include <algorithm>

#if defined(NETW_TIER_HOSTED)

namespace TestCensusReset {

using netw_test::CensusRow;

const char *const HOLDER_NAME = "CR1 a case may end holding an object";

godot::Ref<godot::RefCounted> held_until_reset;
bool holder_ran = false;

void release_held() {
    held_until_reset.unref();
}

const bool release_hooked = netw_test::register_reset_hook(&release_held);

TEST_CASE(
    "[Networked][Census] CR1 a case may end holding an object that only a "
    "reset hook releases"
) {
    held_until_reset.instantiate();
    holder_ran = true;
}

TEST_CASE(
    "[Networked][Census] CR2 the census counts what CR1 held as freed by the "
    "reset hook and not before it, then retires that row so the run's count "
    "reads only the suite"
) {
    REQUIRE_MESSAGE(
        holder_ran,
        "run CR1 in the same run, or this proves nothing"
    );
    std::vector<CensusRow> &rows = netw_test::census_ledger().rows;
    auto holder
        = std::find_if(rows.begin(), rows.end(), [](const CensusRow &p_row) {
              return p_row.name.find(HOLDER_NAME) != std::string::npos;
          });
    REQUIRE(holder != rows.end());
    NETW_CHECK_EQ(holder->objects, int64_t(1));
    NETW_CHECK_EQ(holder->freed_before_reset, int64_t(0));
    NETW_CHECK_EQ(holder->freed_by_reset, int64_t(1));
    rows.erase(holder);
}

} // namespace TestCensusReset

#endif
