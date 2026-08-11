// The cases that pin what NetwJunitReporter writes.
//
// The reporter is the only thing that reads a library run, so a defect in it
// reports as a green suite rather than as a broken tool. These cases are green
// on purpose and prove themselves from OUTSIDE, in the XML: `native-report`
// reads the row count and the per-row detail, and a regression here shows up
// as a count that no longer matches the case count.
//
// The library build only. The module build is read by doctest's own junit
// reporter, which has none of these defects and none of this file's reason to
// exist.

#include "support/netw_test.h"

namespace TestNetwJunitReporter {

// A `MESSAGE()` is informational. A reporter that scores every message as a
// failure turns a note into a red run, which is how a suite gets its notes
// deleted instead of its bug fixed.
TEST_CASE("[Networked][Report] a message does not fail its case") {
    MESSAGE("this note is not a failure");
    CHECK(true);
}

// One row, four passes. doctest runs the body once per leaf subcase and
// announces the repeats, so a reporter that opens a row per repeat inflates
// every count read off the XML.
TEST_CASE("[Networked][Report] a case with subcases emits one row") {
    int value = 0;
    SUBCASE("first") {
        value = 1;
    }
    SUBCASE("second") {
        value = 2;
        SUBCASE("nested once") {
            value = 3;
        }
        SUBCASE("nested twice") {
            value = 4;
        }
    }
    CHECK(value > 0);
}

} // namespace TestNetwJunitReporter
