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

// The prefix's first bracket. Lowercase so it matches the NETW_LOG vocabulary
// a reader already types.
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

void configure() {
    const String setting = OS::get_singleton()->get_environment("NETW_LOG");
    const Level configured = setting.is_empty()
        ? (OS::get_singleton()->is_stdout_verbose() ? Level::INFO : Level::WARN)
        : parse_level(setting);
    threshold.store(configured, std::memory_order_relaxed);
}

namespace {

void note_unlisted(const char *system) {
    if (subsystem_of(system) != SUBSYSTEM_NONE) {
        return;
    }
    static std::atomic_bool told = false;
    if (told.exchange(true, std::memory_order_relaxed)) {
        return;
    }
    gd::push_warning(
        String("[warn][log] subsystem \"") + String(system)
        + String("\" is not in netw/subsystems.hpp, so nothing can select ")
        + String("it: add a row there or use one that exists")
    );
}

} // namespace

bool enabled(Level level) {
    const Level configured = threshold.load(std::memory_order_relaxed);
    return int(level) >= int(configured) && configured != Level::NONE;
}

void write(Level level, const char *system, const String &message) {
    note_unlisted(system);
    const String line = line_for(level, system, message);
    profile_line(level, line);
    if (level == Level::ERROR) {
        gd::push_error(line);
    } else if (level == Level::WARN) {
        gd::push_warning(line);
    } else if (enabled(level)) {
        gd::print(line);
    }
}

void write_at(
    Level level,
    const char *system,
    const String &message,
    const char *function,
    const char *file,
    int line
) {
    note_unlisted(system);
    const String text = line_for(level, system, message);
    profile_line(level, text);
    if (level == Level::ERROR) {
        gd::push_error_at(function, file, line, text);
    } else if (level == Level::WARN) {
        gd::push_warning_at(function, file, line, text);
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
