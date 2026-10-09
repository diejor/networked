#pragma once

#include "netw_test.h"

#include <cstdio>
#include <string>
#include <vector>

#include "godot/performance.hpp"
#include "godot/project_settings.hpp"
#include "godot/utility.hpp"
#include "netw/api/netw_multiplayer.hpp"

namespace netw_test {

inline constexpr int64_t NETW_TEST_SEED = 0x60d07;

using ResetHook = void (*)();

inline std::vector<ResetHook> &reset_hooks() {
    static std::vector<ResetHook> hooks;
    return hooks;
}

inline bool register_reset_hook(ResetHook p_hook) {
    reset_hooks().push_back(p_hook);
    return true;
}

inline void smooth_when_headless(bool p_smooth) {
    godot::ProjectSettings *settings = godot::ProjectSettings::get_singleton();
    if (settings != nullptr) {
        settings->set_setting(
            netw::NetwMultiplayer::SMOOTH_WHEN_HEADLESS_SETTING,
            p_smooth
        );
    }
}

inline void reset_for_case() {
    netw::gd::seed(NETW_TEST_SEED);
    smooth_when_headless(true);
    for (ResetHook hook : reset_hooks()) {
        hook();
    }
}

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

struct CensusRow {
    int64_t objects = 0;
    int64_t resources = 0;
    int64_t freed_before_reset = 0;
    int64_t freed_by_reset = 0;
    std::string name;
};

struct CensusLedger {
    std::vector<CensusRow> rows;
    bool read_by_runner = false;
};

inline CensusLedger &census_ledger() {
    static CensusLedger ledger;
    return ledger;
}

class ResetListener final : public doctest::IReporter {
    Census opened;
    Census closed;
    std::string current;
    bool row_awaits_reset = false;

    void reset_after_case() {
        if (!row_awaits_reset) {
            reset_for_case();
            return;
        }
        row_awaits_reset = false;
        const Census before = Census::take();
        reset_for_case();
        const Census after = Census::take();
        CensusRow &row = census_ledger().rows.back();
        row.freed_before_reset = closed.objects - before.objects;
        row.freed_by_reset = before.objects - after.objects;
    }

public:
    explicit ResetListener(const doctest::ContextOptions &) {
    }

    void test_case_start(const doctest::TestCaseData &data) override {
        reset_after_case();
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
        closed = Census::take();
        if (!opened.measurable() || !closed.measurable()) {
            return;
        }
        const int64_t objects = closed.objects - opened.objects;
        const int64_t resources = closed.resources - opened.resources;
        if (objects == 0 && resources == 0) {
            return;
        }
        CensusRow row;
        row.objects = objects;
        row.resources = resources;
        row.name = current;
        census_ledger().rows.push_back(row);
        row_awaits_reset = true;
    }

    void test_run_end(const doctest::TestRunStats &) override {
        reset_after_case();
        const CensusLedger &ledger = census_ledger();
        if (ledger.read_by_runner || ledger.rows.empty()) {
            return;
        }
        std::printf(
            "NETW_CENSUS %d case(s) ended holding more than they opened "
            "with\n",
            int(ledger.rows.size())
        );
        for (const CensusRow &row : ledger.rows) {
            std::printf(
                "  objects %+lld resources %+lld freed by reset %lld  %s\n",
                (long long)row.objects,
                (long long)row.resources,
                (long long)row.freed_by_reset,
                row.name.c_str()
            );
        }
    }

    void report_query(const doctest::QueryData &) override {
    }
    void test_run_start() override {
        census_ledger().rows.clear();
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

#define NETW_INSTALL_RESET_LISTENER() \
    DOCTEST_REGISTER_LISTENER("netw-reset", 2, netw_test::ResetListener)
