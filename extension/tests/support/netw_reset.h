#pragma once

#include "netw_test.h"

#include <cstdio>
#include <string>
#include <vector>

#include "godot/performance.hpp"
#include "godot/utility.hpp"

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

inline void reset_for_case() {
    netw::gd::seed(NETW_TEST_SEED);
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

#define NETW_INSTALL_RESET_LISTENER() \
    DOCTEST_REGISTER_LISTENER("netw-reset", 2, netw_test::ResetListener)
