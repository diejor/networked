#pragma once

/* Laws, and the manifest of the cells a run executed.
 *
 * A law is a free function in a static table. There is no registration
 * framework and no fixture: one scenario is driven once, and every law reads
 * the same run. Adding a law costs one row, adding a scenario costs one row,
 * and the corpus is their product.
 *
 * A law RETURNS its verdict rather than asserting it, which is what makes the
 * red-proof obligation mechanical: the same law function is checked to hold
 * against a clean run and to BREAK against a planted one, and a law nothing can
 * break is not reading what its claim says it reads.
 *
 * [codeblock]
 * static LawVerdict law_reconverges(const ScenarioRun &p_run) {
 *     const Lane p = p_run.lane("P");
 *     if (p.tail_divergence(5) >= p.epsilon()) {
 *         return law_broken("tail %g is not under epsilon", ...);
 *     }
 *     return law_held();
 * }
 *
 * for (const Scenario &s : CORPUS) {
 *     const ScenarioRun run = ScenarioRun::kernel(s);
 *     REQUIRE(run.decisions() > 0);
 *     for (const LawRow &law : LAWS) {
 *         NETW_CELL(law, s);
 *         NETW_LAW_HOLDS(law, run);
 *     }
 * }
 * [/codeblock]
 *
 * `Cells` is the accounting instrument the crossing gate resolves a `MATRIX`
 * ledger row against, exactly as it resolves a named successor against
 * `--list-test-cases`. It is a runtime enumeration of coverage rather than a
 * source scan, so a cell that did not execute cannot license a deletion.
 * The one committed spelling of a cell is `"<law>"x"<scenario>"`, ASCII `x`
 * and both sides quoted, because a manifest a gate has to guess at is a
 * manifest that licenses whatever it fails to parse.
 */

#include "netw_test.h"

#include <cstdarg>
#include <cstdio>
#include <cstdlib>
#include <set>
#include <string>

namespace netw_test {

class ScenarioRun;

// The detail is a fixed array rather than a pointer because doctest
// stringifies a `const char *` as a pointer, through the same numeric facet
// that crashes the hosted tier.
struct LawVerdict {
    bool held = true;
    char detail[192] = { 0 };
};

// A law reads whatever its family's driver hands back, so the row is
// parameterized by that run rather than by the one driver that came first.
template <typename Run>
struct LawRowFor {
    // The identifier a ledger row names, and half of every cell coordinate.
    const char *name;
    // The failure prose, stated once here rather than per assertion.
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

/* Every cell the run executed, deduplicated and sorted by the container.
 *
 * The strings outlive the run because a `std::set` never moves an element it
 * holds, which is what lets `record` hand back a pointer a `CAPTURE` can read
 * at failure time.
 */
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

    // The module tier has no bound `run()` to carry a path, so it names one
    // through the environment. Both tiers reach the same writer, which is what
    // makes the two manifests comparable rather than merely similar.
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

// Writes the manifest at the end of a run that asked for one. A listener is
// how the module tier reaches the end of its own run, which the engine's test
// runner owns.
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

// Marks the cell and puts its coordinates plus the law's claim into the
// context every assertion under it reports with. Opens no scope of its own, so
// a law loop body is the scope.
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

// The red-proof half: the law must SEE the planted defect. A law that stays
// green against its own plant is the green nobody has seen red.
#define NETW_LAW_BREAKS(m_law, m_run) NETW_LAW_VERDICT(m_law, m_run, false)
