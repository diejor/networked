// The runner is the one translation unit that carries doctest's implementation.
// It reaches doctest through the prelude like every case file does, so the two
// agree on doctest's configuration: a config macro set in one place and not the
// other is a silent ODR violation.
#define DOCTEST_CONFIG_IMPLEMENT
#include "support/netw_test.h"

#include <cstdio>
#include <filesystem>
#include <mutex>
#include <sstream>
#include <string>
#include <vector>

#include "godot/class_db.hpp"
#include "godot/project_settings.hpp"
#include "netw/colors.hpp"
#include "netw/log.hpp"
#include "netw/profile.hpp"
#include "netw/tests.hpp"
#include "support/netw_cells.h"
#include "support/netw_reset.h"

using namespace godot;

NETW_INSTALL_RESET_LISTENER();

namespace netw {

namespace {

void probe_default_zone(int64_t value) {
    NETW_ZONE();
    NETW_ZONE_COLOR(colors::TICK);
    NETW_ZONE_VALUE(value);
    NETW_ZONE_TEXT_F("value=%lld", static_cast<long long>(value));
    NETW_ZONE_NAME_F("probe %lld", static_cast<long long>(value));
}

void probe_named_zone() {
    NETW_ZONE_N("Networked named instrumentation probe");
}

void probe_colored_zone() {
    NETW_ZONE_C(colors::TICK);
}

void compile_ambient_profile_macros() {
    if (false) {
        NETW_THREAD("Networked test", 1);
        NETW_LOCKABLE(std::mutex, lock);
        void *pointer = &lock;
        NETW_ALLOC_N(pointer, sizeof(lock), "Networked test lock");
        NETW_FREE_N(pointer, "Networked test lock");
        NETW_APP_INFO("Networked native tests", 22);
    }
}

std::string escape_xml(const char *text) {
    std::string escaped;
    if (text == nullptr) {
        return escaped;
    }
    for (const char *cursor = text; *cursor != '\0'; ++cursor) {
        switch (*cursor) {
            case '&':
                escaped += "&amp;";
                break;
            case '<':
                escaped += "&lt;";
                break;
            case '>':
                escaped += "&gt;";
                break;
            case '\"':
                escaped += "&quot;";
                break;
            case '\'':
                escaped += "&apos;";
                break;
            default:
                escaped += *cursor;
                break;
        }
    }
    return escaped;
}

/* Writes one JUnit row per test case, whatever happened inside it.
 *
 * Three properties this reporter has to hold, each of which it once got wrong
 * and each of which reads as a green run rather than as a broken report:
 *
 *   one row per case      A case with subcases is executed once per subcase,
 *                         and doctest announces the repeats through
 *                         `test_case_reenter`. Opening a row there duplicates
 *                         the case in every count that reads the XML.
 *   every failure kept    A row carries the text of EVERY failed assertion,
 *                         appended in order. Overwriting keeps only the last,
 *                         so the assertion a reader needs is the one that is
 *                         gone.
 *   severity respected    `MESSAGE()` is informational. Only a message that
 *                         doctest itself scores as a failure — `FAIL` and
 *                         `FAIL_CHECK` — fails the case.
 *
 * The detail of a failure carries the active `INFO` and `CAPTURE` context, so
 * a `NETW_CHECK_CLOSE` that missed names the two values it compared rather
 * than reporting that a bool was false.
 */
class NetwJunitReporter final : public doctest::IReporter {
    struct Case {
        std::string file;
        std::string name;
        std::string detail;
        double seconds = 0.0;
        bool failure = false;
        bool error = false;
    };

    const doctest::ContextOptions &options;
    std::vector<Case> cases;
    // The subcase path currently being executed, so one row can still say
    // which of its subcases failed.
    std::vector<std::string> subcases;

    // A reporter callback can arrive before any case opened only if doctest
    // changes its call order, and dropping the text beats indexing off the end.
    Case *current() {
        return cases.empty() ? nullptr : &cases.back();
    }

    void append_detail(const std::string &text) {
        Case *test_case = current();
        if (test_case == nullptr || text.empty()) {
            return;
        }
        if (!test_case->detail.empty()) {
            test_case->detail += "\n";
        }
        test_case->detail += text;
    }

    std::string subcase_prefix() const {
        std::string prefix;
        for (const std::string &name : subcases) {
            prefix += name;
            prefix += " / ";
        }
        return prefix;
    }

    // The INFO and CAPTURE scopes live on doctest's own stack and are gone by
    // the time the run ends, so they are read here, at the failure.
    std::string active_context() const {
        const int count = get_num_active_contexts();
        if (count == 0) {
            return std::string();
        }
        const doctest::IContextScope *const *scopes = get_active_contexts();
        std::ostringstream stream;
        for (int index = 0; index < count; ++index) {
            stream << "\n  logged: ";
            scopes[index]->stringify(&stream);
        }
        return stream.str();
    }

public:
    explicit NetwJunitReporter(const doctest::ContextOptions &context)
        : options(context) {
    }

    void report_query(const doctest::QueryData &) override {
    }
    void test_run_start() override {
    }

    void test_run_end(const doctest::TestRunStats &stats) override {
        int failures = 0;
        int errors = 0;
        for (const Case &test_case : cases) {
            failures += test_case.failure ? 1 : 0;
            errors += test_case.error ? 1 : 0;
        }
        FILE *output = std::fopen(options.out.c_str(), "wb");
        if (output == nullptr) {
            return;
        }
        std::fprintf(output, "<?xml version=\"1.0\" encoding=\"UTF-8\"?>\n");
        std::fprintf(
            output,
            "<testsuites tests=\"%u\" failures=\"%d\">\n",
            stats.numTestCasesPassingFilters,
            failures
        );
        std::fprintf(
            output,
            "  <testsuite name=\"networked-native\" tests=\"%u\" "
            "failures=\"%d\" errors=\"%d\">\n",
            stats.numTestCasesPassingFilters,
            failures,
            errors
        );
        for (const Case &test_case : cases) {
            const std::string file = escape_xml(test_case.file.c_str());
            const std::string name = escape_xml(test_case.name.c_str());
            std::fprintf(
                output,
                "    <testcase classname=\"%s\" name=\"%s\" "
                "time=\"%.9f\">",
                file.c_str(),
                name.c_str(),
                test_case.seconds
            );
            if (test_case.failure || test_case.error) {
                const char *tag = test_case.error ? "error" : "failure";
                const std::string detail = escape_xml(test_case.detail.c_str());
                std::fprintf(output, "<%s>%s</%s>", tag, detail.c_str(), tag);
            }
            std::fprintf(output, "</testcase>\n");
        }
        std::fprintf(output, "  </testsuite>\n</testsuites>\n");
        std::fclose(output);
    }

    void test_case_start(const doctest::TestCaseData &data) override {
        cases.push_back({data.m_file.c_str(), data.m_name});
        subcases.clear();
    }

    // A re-entry is the same case running another of its subcases, not a new
    // case. The row stays open and accumulates.
    void test_case_reenter(const doctest::TestCaseData &) override {
        subcases.clear();
    }

    void test_case_end(const doctest::CurrentTestCaseStats &stats) override {
        Case *test_case = current();
        if (test_case == nullptr) {
            return;
        }
        test_case->seconds = stats.seconds;
        if (!stats.testCaseSuccess && !test_case->error) {
            test_case->failure = true;
        }
    }

    void test_case_exception(
        const doctest::TestCaseException &exception
    ) override {
        Case *test_case = current();
        if (test_case == nullptr) {
            return;
        }
        test_case->error = true;
        append_detail(
            subcase_prefix() + exception.error_string.c_str() + active_context()
        );
    }

    void subcase_start(const doctest::SubcaseSignature &signature) override {
        subcases.push_back(signature.m_name.c_str());
    }

    void subcase_end() override {
        if (!subcases.empty()) {
            subcases.pop_back();
        }
    }

    void log_assert(const doctest::AssertData &assertion) override {
        if (!assertion.m_failed) {
            return;
        }
        Case *test_case = current();
        if (test_case == nullptr) {
            return;
        }
        test_case->failure = true;
        std::string text = subcase_prefix();
        text += assertion.m_expr;
        text += ": ";
        text += assertion.m_decomp.c_str();
        append_detail(text + active_context());
    }

    // A warn-severity message is `MESSAGE()`, which reports without failing.
    // Only `FAIL` and `FAIL_CHECK` reach the row.
    void log_message(const doctest::MessageData &message) override {
        if ((message.m_severity & doctest::assertType::is_warn) != 0) {
            return;
        }
        Case *test_case = current();
        if (test_case == nullptr) {
            return;
        }
        test_case->failure = true;
        append_detail(
            subcase_prefix() + message.m_string.c_str() + active_context()
        );
    }

    void test_case_skipped(const doctest::TestCaseData &) override {
    }
};

DOCTEST_REGISTER_REPORTER("netw-junit", 0, NetwJunitReporter);

} // namespace

int run_native_tests(
    const String &filter,
    const String &report_path,
    const String &cells_path
) {
    const String global_path
        = ProjectSettings::get_singleton()->globalize_path(report_path);
    const CharString path = global_path.utf8();
    std::filesystem::path file_path(path.get_data());
    std::filesystem::create_directories(file_path.parent_path());

    doctest::Context context;
    context.setOption("reporters", "netw-junit");
    context.setOption("out", path.get_data());
    context.setOption("no-version", true);
    if (!filter.is_empty()) {
        const CharString filter_text = filter.utf8();
        context.setOption("test-case", filter_text.get_data());
    }
    const int code = context.run();
    if (!cells_path.is_empty()) {
        const CharString cells = ProjectSettings::get_singleton()
                                     ->globalize_path(cells_path)
                                     .utf8();
        std::filesystem::create_directories(
            std::filesystem::path(cells.get_data()).parent_path()
        );
        netw_test::Cells::write(cells.get_data());
    }
    return code;
}

void NetwNativeTests::_bind_methods() {
    ClassDB::bind_method(
        D_METHOD("run", "filter", "report_path", "cells_path"),
        &NetwNativeTests::run,
        DEFVAL(String()),
        DEFVAL(String("res://reports/native/results.xml")),
        DEFVAL(String())
    );
    ClassDB::bind_method(
        D_METHOD("instrumentation_probe", "value"),
        &NetwNativeTests::instrumentation_probe
    );
}

Dictionary NetwNativeTests::run(
    const String &filter,
    const String &report_path,
    const String &cells_path
) {
    Dictionary result;
    result[StringName("filter")] = filter;
    result[StringName("report_path")] = report_path;
    result[StringName("cells_path")] = cells_path;
    result[StringName("exit_code")]
        = run_native_tests(filter, report_path, cells_path);
    result[StringName("cells")] = netw_test::Cells::count();
    return result;
}

void NetwNativeTests::instrumentation_probe(int64_t value) const {
    NETW_ZONE_NC("Networked instrumentation probe", colors::TICK);
    NETW_ZONE_VALUE(value);
#if defined(NETW_PROFILING)
    const CharString text = String::num_int64(value).utf8();
    NETW_ZONE_TEXT(text.get_data(), text.length());
#endif
    NETW_TICK_MARK();
    NETW_TRACE("test", "Networked native trace probe");
    probe_default_zone(value);
    probe_named_zone();
    probe_colored_zone();
    compile_ambient_profile_macros();
}

} // namespace netw
