#include "netw/log.hpp"

#include "godot/os.hpp"
#include "godot/utility.hpp"
#include "netw/colors.hpp"
#include "netw/profile.hpp"
#include "netw/subsystems.hpp"

using namespace godot;

namespace netw::log {

namespace {

std::atomic<Level> threshold = Level::INFO;

Level parse_level(const String &text) {
    const String value = text.strip_edges().to_lower();
    if (value == "trace") {
        return Level::TRACE;
    }
    if (value == "debug") {
        return Level::DEBUG;
    }
    if (value == "warn") {
        return Level::WARN;
    }
    if (value == "error") {
        return Level::ERROR;
    }
    if (value == "none") {
        return Level::NONE;
    }
    return Level::INFO;
}

const char *name_for(Level level) {
    switch (level) {
        case Level::TRACE:
            return "trace";
        case Level::DEBUG:
            return "debug";
        case Level::INFO:
            return "info";
        case Level::WARN:
            return "warn";
        case Level::ERROR:
            return "error";
        default:
            return "none";
    }
}

#if defined(NETW_PROFILING)
uint32_t color_for(Level level) {
    switch (level) {
        case Level::TRACE:
            return colors::TRACE;
        case Level::DEBUG:
            return colors::DEBUG;
        case Level::INFO:
            return colors::INFO;
        case Level::WARN:
            return colors::WARN;
        case Level::ERROR:
            return colors::ERROR;
        default:
            return colors::INFO;
    }
}
#endif

String line_for(Level level, const char *system, const String &message) {
    return String("[") + String(name_for(level)) + String("][")
        + String(system == nullptr ? "netw" : system) + String("] ") + message;
}

void profile_line(Level level, const String &line) {
#if defined(NETW_PROFILING)
    const CharString utf8 = line.utf8();
    NETW_PROFILE_MESSAGE(utf8.get_data(), utf8.length(), color_for(level));
#else
    (void)level;
    (void)line;
#endif
}

} // namespace

static String requested_setting() {
    OS *os = OS::get_singleton();
    for (const char *name : {"NETW_LOG", "NETW_TEST_LOG"}) {
        const String held = os->get_environment(name);
        if (!held.is_empty()) {
            return held;
        }
    }
    const String flag = "--netw-log=";
    const PackedStringArray args = gd::cmdline_args();
    for (int at = 0; at < args.size(); ++at) {
        const String arg = args[at];
        if (arg.begins_with(flag)) {
            return arg.substr(flag.length());
        }
    }
    return String();
}

void configure() {
    const String setting = requested_setting();
    const Level configured = setting.is_empty()
        ? (OS::get_singleton()->is_stdout_verbose() ? Level::INFO : Level::WARN)
        : parse_level(setting);
    threshold.store(configured, std::memory_order_relaxed);
}

bool enabled(Level p_level) {
    const Level configured = threshold.load(std::memory_order_relaxed);
    return int(p_level) >= int(configured) && configured != Level::NONE;
}

bool is_fault(Level p_level) {
    return p_level == Level::WARN || p_level == Level::ERROR;
}

void set_level(Level p_level) {
    threshold.store(p_level, std::memory_order_relaxed);
}

Level level() {
    return threshold.load(std::memory_order_relaxed);
}

Level level_named(const String &p_name) {
    return parse_level(p_name);
}

String format_args(const String &p_message, const Array &p_args) {
    if (p_args.is_empty()) {
        return p_message;
    }
    Variant formatted;
    bool valid = false;
    Variant::evaluate(
        Variant::OP_MODULE,
        Variant(p_message),
        Variant(p_args),
        formatted,
        valid
    );
    return valid ? String(formatted) : p_message;
}

void write(Level p_level, SubsystemName p_system, const String &p_message) {
    const String line = line_for(p_level, p_system.text, p_message);
    profile_line(p_level, line);
    if (is_fault(p_level)) {
        if (p_level == Level::ERROR) {
            gd::push_error(line);
        } else {
            gd::push_warning(line);
        }
    } else if (enabled(p_level)) {
        gd::print(line);
    }
}

void write_at(
    Level level,
    SubsystemName system,
    const String &message,
    const char *function,
    const char *file,
    int line
) {
    const String text = line_for(level, system.text, message);
    profile_line(level, text);
    if (is_fault(level)) {
        if (level == Level::ERROR) {
            gd::push_error_at(function, file, line, text);
        } else {
            gd::push_warning_at(function, file, line, text);
        }
    } else if (enabled(level)) {
        gd::print(text);
    }
}

String condition_message(
    const char *condition,
    const String &message,
    const char *return_expression
) {
    String text
        = String("Condition \"") + String(condition) + String("\" is true.");
    if (return_expression != nullptr) {
        text
            += String(" Returning: ") + String(return_expression) + String(".");
    }
    if (!message.is_empty()) {
        text += String(" ") + message;
    }
    return text;
}

String assertion_message(const char *condition, const String &message) {
    String text
        = String("Assertion \"") + String(condition) + String("\" failed.");
    if (!message.is_empty()) {
        text += String(" ") + message;
    }
    return text;
}

} // namespace netw::log
