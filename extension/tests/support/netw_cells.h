#pragma once

#include "netw_test.h"

#include <cstdarg>
#include <cstdio>
#include <cstdlib>
#include <set>
#include <string>

namespace netw_test {

class ScenarioRun;

struct LawVerdict {
    bool held = true;
    char detail[192] = { 0 };
};

template <typename Run>
struct LawRowFor {
    const char *name;
    const char *claim;
    LawVerdict (*check)(const Run &);
};

typedef LawVerdict (*LawCheck)(const ScenarioRun &);

typedef LawRowFor<ScenarioRun> LawRow;

inline LawVerdict law_held() {
    return LawVerdict();
}

inline LawVerdict law_broken(const char *p_format, ...) {
    LawVerdict verdict;
    verdict.held = false;
    va_list arguments;
    va_start(arguments, p_format);
    std::vsnprintf(verdict.detail, sizeof(verdict.detail), p_format, arguments);
    va_end(arguments);
    return verdict;
}

class Cells {
    std::set<std::string> executed;

    static Cells &instance() {
        static Cells cells;
        return cells;
    }

public:
    static const char *record(const char *p_law, const char *p_scenario) {
        char line[256];
        std::snprintf(line, sizeof(line), "\"%s\"x\"%s\"", p_law, p_scenario);
        return instance().executed.insert(std::string(line)).first->c_str();
    }

    static int count() {
        return int(instance().executed.size());
    }

    static const char *requested_path() {
        return std::getenv("NETW_CELLS_PATH");
    }

    static bool write(const char *p_path) {
        std::FILE *out = std::fopen(p_path, "wb");
        if (out == nullptr) {
            return false;
        }
        for (const std::string &cell : instance().executed) {
            std::fprintf(out, "%s\n", cell.c_str());
        }
        std::fclose(out);
        return true;
    }
};

class CellsListener final : public doctest::IReporter {
public:
    explicit CellsListener(const doctest::ContextOptions &) {
    }

    void test_run_end(const doctest::TestRunStats &) override {
        const char *path = Cells::requested_path();
        if (path != nullptr && path[0] != '\0') {
            Cells::write(path);
        }
    }

    void report_query(const doctest::QueryData &) override {
    }
    void test_run_start() override {
    }
    void test_case_start(const doctest::TestCaseData &) override {
    }
    void test_case_reenter(const doctest::TestCaseData &) override {
    }
    void test_case_end(const doctest::CurrentTestCaseStats &) override {
    }
    void test_case_exception(const doctest::TestCaseException &) override {
    }
    void subcase_start(const doctest::SubcaseSignature &) override {
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

#define NETW_INSTALL_CELLS_LISTENER() \
    DOCTEST_REGISTER_LISTENER("netw-cells", 3, netw_test::CellsListener)

#define NETW_CELL(m_law, m_scenario) \
    NETW_FORMAT_TEXT( \
        netw_cell_text, \
        netw_test::Cells::record( \
            (m_law).name, (m_scenario).label.utf8().get_data() \
        ) \
    ); \
    CAPTURE(netw_cell_text); \
    NETW_FORMAT_TEXT(netw_claim_text, (m_law).claim); \
    CAPTURE(netw_claim_text)

#define NETW_LAW_VERDICT(m_law, m_run, m_expected) \
    do { \
        const netw_test::LawVerdict netw_verdict = (m_law).check(m_run); \
        NETW_FORMAT_TEXT(netw_detail_text, netw_verdict.detail); \
        CAPTURE(netw_detail_text); \
        CHECK(netw_verdict.held == (m_expected)); \
    } while (0)

#define NETW_LAW_HOLDS(m_law, m_run) NETW_LAW_VERDICT(m_law, m_run, true)

#define NETW_LAW_BREAKS(m_law, m_run) NETW_LAW_VERDICT(m_law, m_run, false)
