#pragma once

#include "netw_test.h"

#include "godot/class_db.hpp"
#include "godot/json.hpp"
#include "godot/ref_counted.hpp"
#include "godot/variant.hpp"

#if defined(NETW_MODULE)
#include "core/core_bind.h"
#elif defined(NETW_GDEXTENSION)
#include <godot_cpp/classes/logger.hpp>
#include <godot_cpp/classes/os.hpp>
#include <godot_cpp/classes/script_backtrace.hpp>
#endif

#include <algorithm>
#include <cstdio>
#include <filesystem>
#include <mutex>
#include <set>
#include <vector>

namespace netw_test {

#if defined(NETW_MODULE)
using TierLogger = CoreBind::Logger;
using TierOS = CoreBind::OS;
#else
using TierLogger = godot::Logger;
using TierOS = godot::OS;
#endif

struct FaultLine {
    int64_t serial = 0;
    godot::String text;
    bool error = false;
    bool stderr_only = false;
    godot::String message;
    godot::String file;
    godot::String function;
    int line = 0;
    int64_t case_id = 0;
};

struct FaultCase {
    godot::String name;
    bool passed = false;
};

class FaultLog;

struct FaultLedger {
    std::mutex lock;
    int64_t stamped = 0;
    std::set<int64_t> asserted;
    std::vector<const FaultLog *> attached;
    std::vector<FaultCase> cases{FaultCase()};
    int64_t active_case = 0;
};

inline FaultLedger &fault_ledger() {
    static FaultLedger ledger;
    return ledger;
}

class FaultLog : public TierLogger {
#if defined(NETW_GDEXTENSION)
    GDCLASS(FaultLog, godot::Logger)
#endif

    std::mutex lines_lock;
    std::vector<FaultLine> lines;

    void record(
        const godot::String &p_text,
        bool p_error,
        const godot::String &p_message,
        const godot::String &p_file,
        const godot::String &p_function,
        int p_line,
        bool p_stderr_only = false
    ) {
        FaultLine line;
        line.text = p_text;
        line.error = p_error;
        line.stderr_only = p_stderr_only;
        line.message = p_message;
        line.file = p_file;
        line.function = p_function;
        line.line = p_line;
        {
            FaultLedger &ledger = fault_ledger();
            const std::lock_guard<std::mutex> held(ledger.lock);
            if (!ledger.attached.empty() && ledger.attached.front() == this) {
                ledger.stamped += 1;
            }
            line.serial = ledger.stamped;
            line.case_id = ledger.active_case;
        }
        const std::lock_guard<std::mutex> held(lines_lock);
        lines.push_back(line);
    }

    template <typename Matches>
    godot::PackedStringArray assert_lines(Matches p_matches) {
        godot::PackedStringArray matched;
        std::vector<int64_t> serials;
        {
            const std::lock_guard<std::mutex> held(lines_lock);
            int64_t error_at = 0;
            int64_t warning_at = 0;
            for (const FaultLine &line : lines) {
                const int64_t at = line.error ? error_at++ : warning_at++;
                if (p_matches(line, at)) {
                    matched.push_back(line.text);
                    serials.push_back(line.serial);
                }
            }
        }
        FaultLedger &ledger = fault_ledger();
        const std::lock_guard<std::mutex> held(ledger.lock);
        ledger.asserted.insert(serials.begin(), serials.end());
        return matched;
    }

    int64_t logged(bool p_error) {
        const std::lock_guard<std::mutex> held(lines_lock);
        int64_t count = 0;
        for (const FaultLine &line : lines) {
            count += line.error == p_error ? 1 : 0;
        }
        return count;
    }

protected:
    static void _bind_methods() {
    }

public:
#if defined(NETW_MODULE)
    void log_error(
        const char *p_function,
        const char *p_file,
        int p_line,
        const char *p_code,
        const char *p_rationale,
        bool p_editor_notify,
        ErrorType p_type,
        const godot::TypedArray<ScriptBacktrace> &p_script_backtraces
    ) override {
        if (p_type == ERROR_TYPE_ERROR || p_type == ERROR_TYPE_WARNING) {
            record(
                godot::String(p_code) + godot::String(" ")
                    + godot::String(p_rationale),
                p_type == ERROR_TYPE_ERROR,
                p_rationale != nullptr && *p_rationale
                    ? godot::String(p_rationale)
                    : godot::String(p_code),
                p_file,
                p_function,
                p_line
            );
        }
    }

    void log_message(const godot::String &p_text, bool p_error) override {
        if (p_error && !p_text.strip_edges().is_empty()) {
            record(
                p_text,
                true,
                p_text.strip_edges(),
                godot::String(),
                godot::String(),
                0,
                true
            );
        }
    }
#else
    void _log_error(
        const godot::String &p_function,
        const godot::String &p_file,
        int32_t p_line,
        const godot::String &p_code,
        const godot::String &p_rationale,
        bool p_editor_notify,
        int32_t p_error_type,
        const godot::TypedArray<godot::Ref<godot::ScriptBacktrace>>
            &p_script_backtraces
    ) override {
        if (p_error_type == ERROR_TYPE_ERROR
            || p_error_type == ERROR_TYPE_WARNING) {
            record(
                p_code + godot::String(" ") + p_rationale,
                p_error_type == ERROR_TYPE_ERROR,
                p_rationale.is_empty() ? p_code : p_rationale,
                p_file,
                p_function,
                p_line
            );
        }
    }

    void _log_message(const godot::String &p_message, bool p_error) override {
        if (p_error && !p_message.strip_edges().is_empty()) {
            record(
                p_message,
                true,
                p_message.strip_edges(),
                godot::String(),
                godot::String(),
                0,
                true
            );
        }
    }
#endif

    static void attach(const godot::Ref<FaultLog> &p_log) {
        {
            FaultLedger &ledger = fault_ledger();
            const std::lock_guard<std::mutex> held(ledger.lock);
            ledger.attached.push_back(p_log.ptr());
        }
        TierOS::get_singleton()->add_logger(godot::Ref<TierLogger>(p_log));
    }

    static void detach(const godot::Ref<FaultLog> &p_log) {
        TierOS::get_singleton()->remove_logger(godot::Ref<TierLogger>(p_log));
        FaultLedger &ledger = fault_ledger();
        const std::lock_guard<std::mutex> held(ledger.lock);
        ledger.attached.erase(
            std::remove(
                ledger.attached.begin(),
                ledger.attached.end(),
                p_log.ptr()
            ),
            ledger.attached.end()
        );
    }

    std::vector<FaultLine> recorded() {
        const std::lock_guard<std::mutex> held(lines_lock);
        return lines;
    }

    int64_t count_matching(
        bool p_error,
        const godot::String &p_pattern,
        bool p_stderr_only = false
    ) {
        return assert_lines(
                   [&](const FaultLine &p_line, int64_t) {
                       return p_line.error == p_error
                           && p_line.stderr_only == p_stderr_only
                           && p_line.text.strip_edges().match(p_pattern);
                   }
        ).size();
    }

    std::vector<FaultLine> unmatched() {
        std::vector<FaultLine> pending = recorded();
        FaultLedger &ledger = fault_ledger();
        const std::lock_guard<std::mutex> held(ledger.lock);
        pending.erase(
            std::remove_if(
                pending.begin(),
                pending.end(),
                [&](const FaultLine &p_line) {
                    return ledger.asserted.count(p_line.serial) > 0;
                }
            ),
            pending.end()
        );
        return pending;
    }

    int64_t error_count() {
        return assert_lines(
                   [](const FaultLine &p_line, int64_t) { return p_line.error; }
        ).size();
    }

    int64_t warning_count_containing(const godot::String &p_text) {
        return assert_lines(
                   [&](const FaultLine &p_line, int64_t) {
                       return !p_line.error && p_line.text.contains(p_text);
                   }
        ).size();
    }

    int64_t error_count_containing(const godot::String &p_text) {
        return assert_lines(
                   [&](const FaultLine &p_line, int64_t) {
                       return p_line.error && p_line.text.contains(p_text);
                   }
        ).size();
    }

    godot::PackedStringArray error_lines(int64_t p_from) {
        return assert_lines([&](const FaultLine &p_line, int64_t p_at) {
            return p_line.error && p_at >= p_from;
        });
    }

    godot::PackedStringArray warning_lines(int64_t p_from) {
        return assert_lines([&](const FaultLine &p_line, int64_t p_at) {
            return !p_line.error && p_at >= p_from;
        });
    }

    int64_t errors_logged() {
        return logged(true);
    }

    int64_t warnings_logged() {
        return logged(false);
    }
};

inline godot::Ref<FaultLog> &run_fault_log() {
    static godot::Ref<FaultLog> log;
    return log;
}

inline void begin_fault_capture() {
    if (run_fault_log().is_valid()) {
        return;
    }
    run_fault_log().instantiate();
    FaultLog::attach(run_fault_log());
}

inline void begin_fault_case(const char *p_name) {
    FaultLedger &ledger = fault_ledger();
    const std::lock_guard<std::mutex> held(ledger.lock);
    ledger.cases.push_back({godot::String::utf8(p_name), false});
    ledger.active_case = int64_t(ledger.cases.size()) - 1;
}

inline void end_fault_case(bool p_passed) {
    FaultLedger &ledger = fault_ledger();
    const std::lock_guard<std::mutex> held(ledger.lock);
    ledger.cases[ledger.active_case].passed = p_passed;
    ledger.active_case = 0;
}

inline void write_fault_report(bool p_passed) {
    const godot::String destination
        = TierOS::get_singleton()->get_environment("NETW_TEST_DIAGNOSTICS");
    if (destination.is_empty() || run_fault_log().is_null()) {
        return;
    }
    const std::vector<FaultLine> lines = run_fault_log()->recorded();
    godot::Array events;
    FaultLedger &ledger = fault_ledger();
    {
        const std::lock_guard<std::mutex> held(ledger.lock);
        for (const FaultLine &line : lines) {
            const FaultCase &scope = ledger.cases[line.case_id];
            godot::Dictionary event;
            event["serial"] = line.serial;
            event["case"] = scope.name;
            event["passed"] = line.case_id == 0 ? p_passed : scope.passed;
            event["expected"] = ledger.asserted.count(line.serial) > 0;
            event["severity"] = line.stderr_only ? "STDERR"
                : line.error                     ? "ERROR"
                                                 : "WARNING";
            event["message"] = line.message;
            event["file"] = line.file;
            event["function"] = line.function;
            event["line"] = line.line;
            events.push_back(event);
        }
    }
    const auto path = destination.utf8();
    const std::filesystem::path file_path(path.get_data());
    std::filesystem::create_directories(file_path.parent_path());
    FILE *file = std::fopen(path.get_data(), "wb");
    if (file != nullptr) {
        const auto json = godot::JSON::stringify(events).utf8();
        std::fwrite(json.get_data(), 1, size_t(json.length()), file);
        std::fclose(file);
    }
}

inline void end_fault_capture() {
    if (run_fault_log().is_valid()) {
        FaultLog::detach(run_fault_log());
        run_fault_log().unref();
    }
}

struct FaultWatch {
    struct Expectation {
        godot::String pattern;
        int64_t count = 0;
        bool error = false;
        bool stderr_only = false;
    };

    std::vector<Expectation> expectations;
    godot::Ref<FaultLog> log;

    FaultWatch() {
        log.instantiate();
        FaultLog::attach(log);
    }

    ~FaultWatch() {
        for (const Expectation &expectation : expectations) {
            const auto pattern = expectation.pattern.utf8();
            NETW_FORMAT_TEXT(expected_message, pattern.get_data());
            CAPTURE(expected_message);
            NETW_CHECK_EQ(
                log->count_matching(
                    expectation.error,
                    expectation.pattern,
                    expectation.stderr_only
                ),
                expectation.count
            );
        }
        if (!expectations.empty()) {
            const std::vector<FaultLine> unexpected = log->unmatched();
            if (!unexpected.empty()) {
                const auto text = unexpected.front().text.utf8();
                NETW_FORMAT_TEXT(unexpected_message, text.get_data());
                CAPTURE(unexpected_message);
                NETW_CHECK_EQ(unexpected.size(), 0);
            }
        }
        FaultLog::detach(log);
    }

    FaultWatch(const FaultWatch &) = delete;
    FaultWatch &operator=(const FaultWatch &) = delete;

    void expect_error(const godot::String &p_pattern, int64_t p_count) {
        expectations.push_back({p_pattern, p_count, true});
    }

    void expect_warning(const godot::String &p_pattern, int64_t p_count) {
        expectations.push_back({p_pattern, p_count, false});
    }

    void expect_stderr(const godot::String &p_pattern, int64_t p_count) {
        expectations.push_back({p_pattern, p_count, true, true});
    }

    int64_t errors() const {
        return log->error_count();
    }

    int64_t warnings_containing(const godot::String &p_text) const {
        return log->warning_count_containing(p_text);
    }

    int64_t errors_containing(const godot::String &p_text) const {
        return log->error_count_containing(p_text);
    }

    godot::PackedStringArray error_lines(int64_t p_from = 0) const {
        return log->error_lines(p_from);
    }

    godot::PackedStringArray warning_lines(int64_t p_from = 0) const {
        return log->warning_lines(p_from);
    }

    int64_t errors_logged() const {
        return log->errors_logged();
    }

    int64_t warnings_logged() const {
        return log->warnings_logged();
    }
};

} // namespace netw_test
