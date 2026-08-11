#pragma once

/* Logging, written once and read two ways.
 *
 * Every line carries a level and a subsystem and prints as
 * `[level][sys] text`, so a profiler's message filter and a `grep` over stdout
 * take the same query. Learning to select one is learning to select the other,
 * which is the whole reason the prefix is machine-shaped rather than pretty.
 *
 * The subsystem is the first argument of every macro. The vocabulary is short
 * and shared: `clock`, `codec`, `interest`, `interp`, `lagcomp`, `liveness`,
 * `prediction`, `scene`, `session`, `spawn`, `table`, `tick`, `transport`,
 * `wire`. A new one is a deliberate addition, not a spelling.
 *
 * What each level costs, which is why there are five of them:
 *
 *   TRACE DEBUG INFO   Compiled out entirely in a release build with no
 *                      profiler. Where compiled in, the message is built only
 *                      when a profiler is connected or the level is enabled,
 *                      so an unheard string is never formatted and an argument
 *                      that costs something to compute is never computed.
 *   WARN               Always compiled, always written.
 *   ERROR              Always compiled, always written, and always reaches
 *                      `push_error`. This is the error floor: no setting and
 *                      no build configures an error into silence.
 *
 * The threshold comes from `NETW_LOG`, falling back to the engine's verbose
 * flag and then to WARN, so a default run is quiet without being deaf.
 *
 * `NETW_WARN_ONCE` and `NETW_ERROR_ONCE` write at most once per call site.
 * `NETW_WARN_COND` reports and continues. `NETW_ERR` and `NETW_ERR_COND`
 * report and return, while their `_V` forms return the supplied value. All
 * condition macros evaluate their condition once and preserve the caller's
 * source location. `NETW_ASSERT` reports and traps in every build. It is only
 * for internal states that cannot safely continue. Untrusted input belongs in
 * an error guard.
 *
 * [codeblock]
 * NETW_TRACE("codec", "encoded snapshot bytes=%d", bytes.size());
 * NETW_WARN_ONCE("transport", "peer=%d sent an unknown channel", peer_id);
 * NETW_ERR_COND(peer_id <= 0, "session", "peer id must be positive");
 * [/codeblock]
 */

#include <atomic>
#include <utility>

#include "godot/utility.hpp"
#include "godot/variant.hpp"
#include "netw/profile.hpp"

namespace netw::log {

enum class Level {
    TRACE = 0,
    DEBUG = 1,
    INFO = 2,
    WARN = 3,
    ERROR = 4,
    NONE = 5,
};

void configure();
bool enabled(Level level);

inline godot::String format(const godot::String &message) {
    return message;
}

inline godot::String format(const char *message) {
    return godot::String(message);
}

template <typename... Args>
godot::String format(const godot::String &pattern, Args &&...args) {
    return godot::vformat(pattern, std::forward<Args>(args)...);
}

template <typename... Args>
godot::String format(const char *pattern, Args &&...args) {
    return godot::vformat(godot::String(pattern), std::forward<Args>(args)...);
}

void write(Level level, const char *system, const godot::String &message);
void write_at(
    Level level,
    const char *system,
    const godot::String &message,
    const char *function,
    const char *file,
    int line
);
godot::String condition_message(
    const char *condition,
    const godot::String &message,
    const char *return_expression = nullptr
);
godot::String assertion_message(
    const char *condition,
    const godot::String &message
);

} // namespace netw::log

// The three readable-only levels exist where someone can read them: a build
// that profiles, or a debug build. Elsewhere they are not gated at runtime,
// they are not compiled, so their arguments cost nothing to have written.
#if defined(NETW_PROFILING) || defined(DEBUG_ENABLED)
#define NETW_LOG_VERBOSE 1
#else
#define NETW_LOG_VERBOSE 0
#endif

#if NETW_LOG_VERBOSE
#define NETW_LOG_LAZY(m_level, m_system, ...) \
    do { \
        if (netw::profile::connected() || netw::log::enabled(m_level)) { \
            netw::log::write( \
                m_level, \
                m_system, \
                netw::log::format(__VA_ARGS__) \
            ); \
        } \
    } while (false)
#else
#define NETW_LOG_LAZY(m_level, m_system, ...) ((void)0)
#endif

#define NETW_TRACE(m_system, ...) \
    NETW_LOG_LAZY(netw::log::Level::TRACE, m_system, __VA_ARGS__)
#define NETW_DEBUG(m_system, ...) \
    NETW_LOG_LAZY(netw::log::Level::DEBUG, m_system, __VA_ARGS__)
#define NETW_INFO(m_system, ...) \
    NETW_LOG_LAZY(netw::log::Level::INFO, m_system, __VA_ARGS__)

#define NETW_WARN(m_system, ...) \
    netw::log::write_at( \
        netw::log::Level::WARN, \
        m_system, \
        netw::log::format(__VA_ARGS__), \
        __FUNCTION__, \
        __FILE__, \
        __LINE__ \
    )
#define NETW_ERROR(m_system, ...) \
    netw::log::write_at( \
        netw::log::Level::ERROR, \
        m_system, \
        netw::log::format(__VA_ARGS__), \
        __FUNCTION__, \
        __FILE__, \
        __LINE__ \
    )

// One flag per expansion, because a macro's `static` is local to where it was
// written. Two call sites saying the same thing still each get one line.
#define NETW_LOG_ONCE(m_level, m_system, ...) \
    do { \
        static std::atomic_bool netw_logged_once = false; \
        if (!netw_logged_once.exchange(true, std::memory_order_relaxed)) { \
            netw::log::write_at( \
                m_level, \
                m_system, \
                netw::log::format(__VA_ARGS__), \
                __FUNCTION__, \
                __FILE__, \
                __LINE__ \
            ); \
        } \
    } while (false)

#define NETW_WARN_ONCE(m_system, ...) \
    NETW_LOG_ONCE(netw::log::Level::WARN, m_system, __VA_ARGS__)
#define NETW_ERROR_ONCE(m_system, ...) \
    NETW_LOG_ONCE(netw::log::Level::ERROR, m_system, __VA_ARGS__)

#define NETW_ERR(m_system, ...) \
    do { \
        NETW_ERROR(m_system, __VA_ARGS__); \
        return; \
    } while (false)

#define NETW_ERR_V(m_value, m_system, ...) \
    do { \
        NETW_ERROR(m_system, __VA_ARGS__); \
        return m_value; \
    } while (false)

#define NETW_WARN_COND(m_condition, m_system, ...) \
    do { \
        if (m_condition) { \
            NETW_WARN( \
                m_system, \
                netw::log::condition_message( \
                    #m_condition, \
                    netw::log::format(__VA_ARGS__) \
                ) \
            ); \
        } \
    } while (false)

#define NETW_WARN_COND_ONCE(m_condition, m_system, ...) \
    do { \
        if (m_condition) { \
            NETW_WARN_ONCE( \
                m_system, \
                netw::log::condition_message( \
                    #m_condition, \
                    netw::log::format(__VA_ARGS__) \
                ) \
            ); \
        } \
    } while (false)

#define NETW_ERR_COND(m_condition, m_system, ...) \
    do { \
        if (m_condition) { \
            NETW_ERROR( \
                m_system, \
                netw::log::condition_message( \
                    #m_condition, \
                    netw::log::format(__VA_ARGS__) \
                ) \
            ); \
            return; \
        } \
    } while (false)

#define NETW_ERR_COND_V(m_condition, m_value, m_system, ...) \
    do { \
        if (m_condition) { \
            NETW_ERROR( \
                m_system, \
                netw::log::condition_message( \
                    #m_condition, \
                    netw::log::format(__VA_ARGS__), \
                    #m_value \
                ) \
            ); \
            return m_value; \
        } \
    } while (false)

#define NETW_ASSERT(m_condition, m_system, ...) \
    do { \
        if (!(m_condition)) { \
            NETW_ERROR( \
                m_system, \
                netw::log::assertion_message( \
                    #m_condition, \
                    netw::log::format(__VA_ARGS__) \
                ) \
            ); \
            netw::gd::crash(); \
        } \
    } while (false)
