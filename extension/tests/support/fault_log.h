#pragma once

#include "netw_test.h"

#include "godot/class_db.hpp"
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
};

class FaultLog;

struct FaultLedger {
    std::mutex lock;
    int64_t stamped = 0;
    std::set<int64_t> asserted;
    std::vector<const FaultLog *> attached;
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

    void record(const godot::String &p_text, bool p_error) {
        FaultLine line;
        line.text = p_text;
        line.error = p_error;
        {
            FaultLedger &ledger = fault_ledger();
            const std::lock_guard<std::mutex> held(ledger.lock);
            if (!ledger.attached.empty() && ledger.attached.front() == this) {
                ledger.stamped += 1;
            }
            line.serial = ledger.stamped;
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
                p_type == ERROR_TYPE_ERROR
            );
        }
    }

    void log_message(const godot::String &p_text, bool p_error) override {
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
                p_error_type == ERROR_TYPE_ERROR
            );
        }
    }

    void _log_message(const godot::String &p_message, bool p_error) override {
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
        TierOS::get_singleton()->remove_logger(
            godot::Ref<TierLogger>(p_log)
        );
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

    int64_t error_count() {
        return assert_lines([](const FaultLine &p_line, int64_t) {
                   return p_line.error;
               })
            .size();
    }

    int64_t warning_count_containing(const godot::String &p_text) {
        return assert_lines([&](const FaultLine &p_line, int64_t) {
                   return !p_line.error && p_line.text.contains(p_text);
               })
            .size();
    }

    int64_t error_count_containing(const godot::String &p_text) {
        return assert_lines([&](const FaultLine &p_line, int64_t) {
                   return p_line.error && p_line.text.contains(p_text);
               })
            .size();
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

struct FaultWatch {
    godot::Ref<FaultLog> log;

    FaultWatch() {
        log.instantiate();
        FaultLog::attach(log);
    }

    ~FaultWatch() {
        FaultLog::detach(log);
    }

    FaultWatch(const FaultWatch &) = delete;
    FaultWatch &operator=(const FaultWatch &) = delete;

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
