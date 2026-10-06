#pragma once

#include "netw_test.h"

#include "godot/class_db.hpp"
#include "godot/variant.hpp"

#if defined(NETW_TIER_HOSTED)
#include <godot_cpp/classes/logger.hpp>
#include <godot_cpp/classes/os.hpp>
#include <godot_cpp/classes/script_backtrace.hpp>

#include <atomic>
#include <mutex>
#endif

namespace netw_test {

#if defined(NETW_TIER_HOSTED)

class FaultLog : public godot::Logger {
    GDCLASS(FaultLog, godot::Logger)

    std::atomic<int64_t> errors{0};
    std::mutex warned_lock;
    godot::PackedStringArray warned;
    godot::PackedStringArray errored;

    static int64_t count_containing(
        const godot::PackedStringArray &p_lines,
        const godot::String &p_text
    ) {
        int64_t count = 0;
        for (const godot::String &each : p_lines) {
            if (each.contains(p_text)) {
                count += 1;
            }
        }
        return count;
    }

protected:
    static void _bind_methods() {
    }

public:
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
        const godot::String line = p_code + godot::String(" ") + p_rationale;
        if (p_error_type == ERROR_TYPE_ERROR) {
            errors.fetch_add(1);
            const std::lock_guard<std::mutex> held(warned_lock);
            errored.push_back(line);
        } else if (p_error_type == ERROR_TYPE_WARNING) {
            const std::lock_guard<std::mutex> held(warned_lock);
            warned.push_back(line);
        }
    }

    void _log_message(const godot::String &p_message, bool p_error) override {
    }

    int64_t error_count() const {
        return errors.load();
    }

    int64_t warning_count_containing(const godot::String &p_text) {
        const std::lock_guard<std::mutex> held(warned_lock);
        return count_containing(warned, p_text);
    }

    int64_t error_count_containing(const godot::String &p_text) {
        const std::lock_guard<std::mutex> held(warned_lock);
        return count_containing(errored, p_text);
    }

    godot::PackedStringArray faults() {
        const std::lock_guard<std::mutex> held(warned_lock);
        godot::PackedStringArray both = warned;
        both.append_array(errored);
        return both;
    }
};

struct FaultWatch {
    godot::Ref<FaultLog> log;

    FaultWatch() {
        log.instantiate();
        godot::OS::get_singleton()->add_logger(log);
    }

    ~FaultWatch() {
        godot::OS::get_singleton()->remove_logger(log);
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
};

#endif

} // namespace netw_test
