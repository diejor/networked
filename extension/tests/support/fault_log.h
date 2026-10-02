#pragma once

#include "netw_test.h"

#include "godot/class_db.hpp"
#include "godot/variant.hpp"

#if defined(NETW_TIER_HOSTED)
#include <godot_cpp/classes/logger.hpp>
#include <godot_cpp/classes/os.hpp>
#include <godot_cpp/classes/script_backtrace.hpp>

#include <atomic>
#endif

namespace netw_test {

#if defined(NETW_TIER_HOSTED)

class FaultLog : public godot::Logger {
    GDCLASS(FaultLog, godot::Logger)

    std::atomic<int64_t> errors{0};

protected:
    static void _bind_methods() {}

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
        if (p_error_type == ERROR_TYPE_ERROR) {
            errors.fetch_add(1);
        }
    }

    void _log_message(const godot::String &p_message, bool p_error) override {}

    int64_t error_count() const {
        return errors.load();
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
};

#endif

} // namespace netw_test
