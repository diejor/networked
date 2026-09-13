#pragma once

#include <atomic>
#include <utility>

#include "godot/utility.hpp"
#include "godot/variant.hpp"
#include "netw/profile.hpp"
#include "netw/subsystems.hpp"

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
bool is_fault(Level level);
void set_level(Level level);
Level level();
Level level_named(const godot::String &name);
godot::String format_args(
    const godot::String &message,
    const godot::Array &args
);

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

void write(Level level, SubsystemName system, const godot::String &message);
void write_at(
    Level level,
    SubsystemName system,
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
