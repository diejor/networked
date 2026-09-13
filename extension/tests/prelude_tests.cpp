// The cases that keep `support/netw_test.h` honest.
//
// The prelude's job is that one case file compiles and means the same thing in
// both tiers, so these cases assert the spellings rather than any behaviour of
// the addon: the tier gate resolves to exactly one tier, the module-only
// spellings are callable from the hosted tier, and the numeric macros compare
// and pass. A macro with no user is a macro nobody has compiled.

#include "support/netw_test.h"

#include "netw/log.hpp"

namespace TestNetwPrelude {

// Exactly one tier is defined, and the case can tell which. A file that
// compiled under neither would have failed at the `#error` instead.
TEST_CASE("[Networked][Prelude][Hosted] the tier gate resolves to one tier") {
#if defined(NETW_TIER_HOSTED)
    const bool hosted = true;
#else
    const bool hosted = false;
#endif
#if defined(NETW_TIER_MODULE)
    const bool module_tier = true;
#else
    const bool module_tier = false;
#endif
    CHECK(hosted != module_tier);
}

// A namespace definition in the module tier and a no-op namespace here, so the
// one spelling compiles at the one scope the engine allows it at.
TEST_FORCE_LINK(netw_prelude);

// The module tier's error-silencing spellings are no-ops in the hosted tier
// rather than absent, so a `[Hosted]` case may bracket a deliberate error
// without a tier guard.
TEST_CASE(
    "[Networked][Prelude][Hosted] the module-only spellings are callable"
) {
    ERR_PRINT_OFF;
    ERR_PRINT_ON;
    CHECK(true);
}

TEST_CASE("[Networked][Prelude][Hosted] the integer macro compares exactly") {
    NETW_CHECK_EQ(2 + 2, 4);
    NETW_CHECK_EQ(-1, -1);
}

TEST_CASE(
    "[Networked][Prelude][Hosted] the float macro compares to tolerance"
) {
    NETW_CHECK_CLOSE(0.1 + 0.2, 0.3, 1e-9);
    NETW_CHECK_CLOSE(-2.5, -2.5, 0.0);
}

int guarded_value(bool condition, int &evaluations) {
    NETW_ERR_COND_V(
        (++evaluations, condition),
        -1,
        netw::sys::TEST,
        "the planted condition fired"
    );
    return 1;
}

int failed_value() {
    if (false) {
        NETW_ERR_V(-1, netw::sys::TEST, "the compile-only failure fired");
    }
    return 1;
}

TEST_CASE(
    "[Networked][Prelude][Hosted] condition guards evaluate their input once"
) {
    CHECK(netw::log::format("peer=%d", 7) == "peer=7");
    int evaluations = 0;
    CHECK(guarded_value(false, evaluations) == 1);
    CHECK(evaluations == 1);

    int message_builds = 0;
    NETW_WARN_COND(
        false,
        netw::sys::TEST,
        godot::String::num_int64(++message_builds)
    );
    CHECK(message_builds == 0);
    CHECK(failed_value() == 1);

    if (false) {
        NETW_ASSERT(false, netw::sys::TEST, "the compile-only assertion fired");
    }
}

} // namespace TestNetwPrelude
