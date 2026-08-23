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
#include "netw/predict/frames.hpp"
#include "netw/profile.hpp"
#include "netw/api/tests.hpp"
#include "netw/wire/registry.hpp"
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
    std::vector<std::string> subcases;

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
    ClassDB::bind_method(D_METHOD("wire_spec"), &NetwNativeTests::wire_spec);
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

namespace {

const char *kind_name(wire::ChannelKind value) {
    switch (value) {
        case wire::ChannelKind::KEYED: return "KEYED";
        case wire::ChannelKind::SESSION: return "SESSION";
        case wire::ChannelKind::ROUTED: return "ROUTED";
    }
    return "?";
}

const char *reliability_name(wire::Reliability value) {
    switch (value) {
        case wire::Reliability::UNRELIABLE: return "UNRELIABLE";
        case wire::Reliability::UNRELIABLE_ACKED: return "UNRELIABLE_ACKED";
        case wire::Reliability::RELIABLE: return "RELIABLE";
    }
    return "?";
}

const char *freshness_name(wire::Freshness value) {
    switch (value) {
        case wire::Freshness::NONE: return "NONE";
        case wire::Freshness::FRESHEST_WINS: return "FRESHEST_WINS";
    }
    return "?";
}

const char *delivery_name(wire::Delivery value) {
    switch (value) {
        case wire::Delivery::IMMEDIATE: return "IMMEDIATE";
        case wire::Delivery::FITTED: return "FITTED";
    }
    return "?";
}

const char *direction_name(wire::Direction value) {
    switch (value) {
        case wire::Direction::EITHER: return "EITHER";
        case wire::Direction::SERVER_TO_CLIENT: return "SERVER_TO_CLIENT";
        case wire::Direction::CLIENT_TO_SERVER: return "CLIENT_TO_SERVER";
        case wire::Direction::OWNER_TO_SERVER: return "OWNER_TO_SERVER";
        case wire::Direction::SERVER_TO_OWNER: return "SERVER_TO_OWNER";
    }
    return "?";
}

const char *payload_name(wire::PayloadContract value) {
    switch (value) {
        case wire::PayloadContract::RAW: return "RAW";
        case wire::PayloadContract::PLANNED: return "PLANNED";
        case wire::PayloadContract::DELTA: return "DELTA";
    }
    return "?";
}

Array spec_channels() {
    const wire::WireRegistry reg = wire::WireRegistry::create_default();
    Array out;
    for (int id = 0; id < wire::WireRegistry::MAX_CHANNELS; ++id) {
        const wire::ChannelDecl *decl = reg.find_channel(uint8_t(id));
        if (decl == nullptr || decl->is_reserved) {
            continue;
        }
        Dictionary row;
        row[StringName("id")] = int64_t(id);
        row[StringName("name")] = String(decl->name);
        row[StringName("kind")] = String(kind_name(decl->kind));
        row[StringName("reliability")]
            = String(reliability_name(decl->reliability));
        row[StringName("freshness")] = String(freshness_name(decl->freshness));
        row[StringName("delivery")] = String(delivery_name(decl->delivery));
        row[StringName("direction")] = String(direction_name(decl->direction));
        row[StringName("payload")] = String(payload_name(decl->payload));
        out.push_back(row);
    }
    return out;
}

Array spec_reserved() {
    const wire::WireRegistry reg = wire::WireRegistry::create_default();
    Array out;
    for (int id = 0; id < wire::WireRegistry::MAX_CHANNELS; ++id) {
        const wire::ChannelDecl *decl = reg.find_channel(uint8_t(id));
        if (decl != nullptr && decl->is_reserved) {
            out.push_back(int64_t(id));
        }
    }
    return out;
}

} // namespace

Dictionary NetwNativeTests::wire_spec() const {
    Dictionary out;
    out[StringName("records")] = predict::spec_records();
    out[StringName("channels")] = spec_channels();
    out[StringName("reserved")] = spec_reserved();
    out[StringName("identity")]
        = int64_t(wire::WireRegistry::create_default().identity_hash()
                  & 0x7FFFFFFFFFFFFFFFULL);
    return out;
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
