#pragma once

/* Per-case reset, so no case can be made to pass by the case before it.
 *
 * The engine's own suite resets between cases, and this is the same discipline
 * spelled for both tiers: one seed, one set of hooks, and a census of what a
 * case left behind. Without it a suite's greenness depends on its order, which
 * is the failure mode that survives every review because nothing about it is
 * visible in a case.
 *
 * [codeblock]
 * before every case, every re-entry, and every subcase
 *   1  seed the one global generator with NETW_TEST_SEED
 *   2  run every registered reset hook, in registration order
 *   3  sample the object and resource counts
 * after the case
 *   4  sample again and record the delta — REPORTED, never failed
 * [/codeblock]
 *
 * NEITHER HALF CAN BE PROMOTED TO A FAILURE, and both reasons were measured
 * rather than assumed (2026-08-05, the clock crossing).
 *
 * The OBJECT half carries real noise. The library build cannot flush the delete
 * queue, so a node a case handed to `queue_free` is still counted at the case's
 * end and is not a leak. Five transport rig cases report between +144 and +304
 * objects for exactly that reason.
 *
 * The RESOURCE half is worse than noisy, it is VACUOUS. `resource_count` reads
 * `OBJECT_RESOURCE_COUNT`, which counts the resource cache rather than live
 * `Resource` instances, and nothing in the native corpus loads a resource by
 * path. A deliberately leaked `Resource` moves the OBJECT count by one and
 * leaves this half at zero, so a gate built on it would be green against every
 * implementation, including a leaking one.
 *
 * Promoting the census therefore waits on a trustworthy signal rather than on a
 * decision. Two would do: a delete-queue flush the library build can drive, or
 * a live-instance count that does not go through the resource cache.
 *
 * Installed once per tier with [code]NETW_INSTALL_RESET_LISTENER[/code], from
 * the one translation unit that tier compiles: the runner in the library
 * build, the hosted bridge header in the module build. Writing it twice
 * registers the listener twice and resets twice, which is harmless and still
 * wrong.
 */

// A sibling under `support/`, so the prelude is reached by its bare name here.
#include "netw_test.h"

#include <cstdio>
#include <string>
#include <vector>

#include "godot/performance.hpp"
#include "godot/utility.hpp"

namespace netw_test {

// The engine's suite seeds with this, so both tiers draw the same sequence in
// the same case and a `[Hosted]` case may assert on a drawn number.
inline constexpr int64_t NETW_TEST_SEED = 0x60d07;

using ResetHook = void (*)();

// Function-local storage, so a header included by many translation units still
// has exactly one registry.
inline std::vector<ResetHook> &reset_hooks() {
    static std::vector<ResetHook> hooks;
    return hooks;
}

// Registers a hook to run before every case. Returns a value only so a
// namespace-scope constant can call it at load time.
inline bool register_reset_hook(ResetHook p_hook) {
    reset_hooks().push_back(p_hook);
    return true;
}

// What the runner does between cases. Public because a case that deliberately
// pollutes shared state can restore it on the spot rather than leaving the
// next case to discover it.
inline void reset_for_case() {
    netw::gd::seed(NETW_TEST_SEED);
    for (ResetHook hook : reset_hooks()) {
        hook();
    }
}

// The counts a case is measured against. A monitor that is not up answers -1,
// and a delta involving -1 goes unreported rather than reported as nonsense.
struct Census {
    int64_t objects = -1;
    int64_t resources = -1;

    static Census take() {
        Census taken;
        taken.objects = netw::gd::object_count();
        taken.resources = netw::gd::resource_count();
        return taken;
    }

    bool measurable() const {
        return objects >= 0 && resources >= 0;
    }
};

// A doctest listener is a reporter that is never selected to write output, so
// it observes every run whichever reporter was chosen.
class ResetListener final : public doctest::IReporter {
    Census opened;
    std::string current;
    std::vector<std::string> drifted;

public:
    explicit ResetListener(const doctest::ContextOptions &) {
    }

    void test_case_start(const doctest::TestCaseData &data) override {
        reset_for_case();
        current = data.m_name;
        opened = Census::take();
    }

    void test_case_reenter(const doctest::TestCaseData &) override {
        reset_for_case();
    }

    void subcase_start(const doctest::SubcaseSignature &) override {
        reset_for_case();
    }

    void test_case_end(const doctest::CurrentTestCaseStats &) override {
        const Census closed = Census::take();
        if (!opened.measurable() || !closed.measurable()) {
            return;
        }
        const int64_t objects = closed.objects - opened.objects;
        const int64_t resources = closed.resources - opened.resources;
        if (objects == 0 && resources == 0) {
            return;
        }
        char line[240];
        std::snprintf(
            line,
            sizeof(line),
            "  objects %+lld resources %+lld  %s",
            (long long)objects,
            (long long)resources,
            current.c_str()
        );
        drifted.push_back(std::string(line));
    }

    void test_run_end(const doctest::TestRunStats &) override {
        if (drifted.empty()) {
            return;
        }
        std::printf(
            "NETW_CENSUS %d case(s) ended holding more than they opened "
            "with\n",
            int(drifted.size())
        );
        for (const std::string &line : drifted) {
            std::printf("%s\n", line.c_str());
        }
    }

    void report_query(const doctest::QueryData &) override {
    }
    void test_run_start() override {
    }
    void test_case_exception(const doctest::TestCaseException &) override {
    }
    void subcase_end() override {
    }
    void log_assert(const doctest::AssertData &) override {
    }
    void log_message(const doctest::MessageData &) override {
    }
    void test_case_skipped(const doctest::TestCaseData &) override {
    }
};

} // namespace netw_test

// Priority 2 puts this after the engine's own listener in the module tier,
// where both run and both seed the same value.
#define NETW_INSTALL_RESET_LISTENER() \
    DOCTEST_REGISTER_LISTENER("netw-reset", 2, netw_test::ResetListener)
