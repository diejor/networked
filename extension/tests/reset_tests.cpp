// The canary pair: one case pollutes, the next proves the baseline restored.
//
// A reset that runs but does nothing is indistinguishable from no reset at all
// until the day an ordering bug is blamed on the code under test. These two
// cases are the only thing that tells them apart, so they are written as a
// pair and the second one refuses to pass alone.
//
// `[Hosted]` for a reason beyond portability: the module tier resets through
// the engine's own listener and the hosted tier through this project's, and
// the pair passing in both is what says the two tiers reset to the SAME
// baseline rather than merely each to one of their own.

#include "support/netw_test.h"

#include "godot/utility.hpp"
#include "support/netw_reset.h"

namespace TestNetwReset {

// What the first case drew from the seeded generator, so the second can insist
// on seeing it again. A file-static is the point: it is the one thing that
// deliberately survives the reset.
int64_t first_draw = 0;
bool the_polluter_ran = false;

TEST_CASE("[Networked][Reset][Hosted] a case may leave the generator moved") {
    first_draw = netw::gd::randi();
    the_polluter_ran = true;

    // The pollution. Nothing restores this: the next case's reset has to.
    netw::gd::seed(12345);
    netw::gd::randi();
    netw::gd::randi();

    const int64_t after_polluting = netw::gd::randi();
    // The polluted stream really is a different one, or the pair proves
    // nothing. A generator that ignored the reseed would make the second case
    // pass for the wrong reason.
    CHECK(after_polluting != first_draw);
}

TEST_CASE("[Networked][Reset][Hosted] the next case starts from the baseline") {
    // The pair only means anything together, and a filter that ran only this
    // half would otherwise report a green reset it never observed.
    REQUIRE_MESSAGE(
        the_polluter_ran,
        "run the polluting case in the same run, or this proves nothing"
    );
    NETW_CHECK_EQ(netw::gd::randi(), first_draw);
}

// The seed a reset restores is the engine suite's, so a value drawn in one
// tier is the value drawn in the other.
TEST_CASE("[Networked][Reset][Hosted] the baseline seed is the engine's") {
    NETW_CHECK_EQ(netw_test::NETW_TEST_SEED, 0x60d07);
}

} // namespace TestNetwReset
