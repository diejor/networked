#pragma once

/* Native instrumentation with one spelling in both build targets.
 *
 * A profiling build maps this family directly onto Tracy. A normal build
 * type-checks instrument arguments in discarded branches and never evaluates
 * them. Frame and plot names are pooled pointers declared below. A scoped zone
 * owns Tracy's active-zone variable, so dynamic text, name, color, and value
 * macros apply to the zone in their lexical scope.
 */

#include <cstdint>

#include "netw/colors.hpp"
#include "netw/subsystems.hpp"

namespace netw::profile {

enum class PlotFormat {
    NUMBER,
    MEMORY,
    PERCENTAGE,
};

using netw::Subsystem;
using netw::SUBSYSTEM_ALL;
using netw::SUBSYSTEM_NONE;
#define NETW_SUBSYSTEM_ALIAS(m_name, m_text, m_index) \
    using netw::SUBSYSTEM_##m_name;
NETW_SUBSYSTEM_TABLE(NETW_SUBSYSTEM_ALIAS)
#undef NETW_SUBSYSTEM_ALIAS

namespace names {

extern const char *const TICK_FRAME;
extern const char *const CODEC_BYTES;
extern const char *const INTEREST_EDGES;
extern const char *const INTEREST_TRANSITIONS;
extern const char *const PREDICT_CORRECTIONS;
extern const char *const PREDICT_REPLAY_DEPTH;
extern const char *const PREDICT_ACK_AGE;
extern const char *const PREDICT_FALLBACKS;
extern const char *const PREDICT_PROBATION;
extern const char *const PREDICT_DIVERGENCE;
extern const char *const PREDICT_TAP_DRAINED;
extern const char *const RPC_CALLS_DEFERRED;

} // namespace names

bool connected();
void configure_plot(
    const char *name,
    PlotFormat format,
    bool step,
    bool fill,
    uint32_t color
);
void configure_plots();

// A shared library owns its client. A module shares the engine's client.
void startup();
void shutdown();

template <typename... Args> void discard(Args &&...) {
}

} // namespace netw::profile

#ifndef NETW_SUBSYSTEMS
#define NETW_SUBSYSTEMS netw::profile::SUBSYSTEM_ALL
#endif

#if defined(NETW_PROFILING)
#include "godot/profiling.hpp"

#define NETW_ZONE() ZoneScoped
#define NETW_ZONE_N(m_name) ZoneScopedN(m_name)
#define NETW_ZONE_C(m_color) ZoneScopedC(m_color)
#define NETW_ZONE_NC(m_name, m_color) ZoneScopedNC(m_name, m_color)
#define NETW_ZONE_TEXT(m_text, m_size) ZoneText(m_text, m_size)
#define NETW_ZONE_TEXT_F(m_format, ...) ZoneTextF(m_format, ##__VA_ARGS__)
#define NETW_ZONE_NAME_F(m_format, ...) ZoneNameF(m_format, ##__VA_ARGS__)
#define NETW_ZONE_COLOR(m_color) ZoneColor(m_color)
#define NETW_ZONE_VALUE(m_value) ZoneValue(m_value)
#define NETW_ZONE_SYS(m_mask) \
    ZoneNamed(___tracy_scoped_zone, (NETW_SUBSYSTEMS & (m_mask)) != 0)
#define NETW_TICK_MARK() FrameMarkNamed(netw::profile::names::TICK_FRAME)
#define NETW_PLOT(m_name, m_value) TracyPlot(m_name, m_value)
#define NETW_PLOT_CONFIG(m_name, m_format, m_step, m_fill, m_color) \
    netw::profile::configure_plot(m_name, m_format, m_step, m_fill, m_color)
#define NETW_THREAD(m_name, m_group) \
    tracy::SetThreadNameWithHint(m_name, m_group)
#define NETW_LOCKABLE(m_type, m_name) TracyLockable(m_type, m_name)
#define NETW_ALLOC_N(m_pointer, m_size, m_name) \
    TracyAllocN(m_pointer, m_size, m_name)
#define NETW_FREE_N(m_pointer, m_name) TracyFreeN(m_pointer, m_name)
#define NETW_APP_INFO(m_text, m_size) TracyAppInfo(m_text, m_size)
#define NETW_PROFILE_MESSAGE(m_text, m_size, m_color) \
    TracyMessageC(m_text, m_size, m_color)
#else
#define NETW_PROFILE_DISCARD(...) \
    do { \
        if constexpr (false) { \
            netw::profile::discard(__VA_ARGS__); \
        } \
    } while (false)

#define NETW_ZONE() ((void)0)
#define NETW_ZONE_N(m_name) NETW_PROFILE_DISCARD(m_name)
#define NETW_ZONE_C(m_color) NETW_PROFILE_DISCARD(m_color)
#define NETW_ZONE_NC(m_name, m_color) NETW_PROFILE_DISCARD(m_name, m_color)
#define NETW_ZONE_TEXT(m_text, m_size) NETW_PROFILE_DISCARD(m_text, m_size)
#define NETW_ZONE_TEXT_F(m_format, ...) \
    NETW_PROFILE_DISCARD(m_format, ##__VA_ARGS__)
#define NETW_ZONE_NAME_F(m_format, ...) \
    NETW_PROFILE_DISCARD(m_format, ##__VA_ARGS__)
#define NETW_ZONE_COLOR(m_color) NETW_PROFILE_DISCARD(m_color)
#define NETW_ZONE_VALUE(m_value) NETW_PROFILE_DISCARD(m_value)
#define NETW_ZONE_SYS(m_mask) NETW_PROFILE_DISCARD(m_mask)
#define NETW_TICK_MARK() ((void)0)
#define NETW_PLOT(m_name, m_value) NETW_PROFILE_DISCARD(m_name, m_value)
#define NETW_PLOT_CONFIG(m_name, m_format, m_step, m_fill, m_color) \
    do { \
        if constexpr (false) { \
            netw::profile::configure_plot( \
                m_name, \
                m_format, \
                m_step, \
                m_fill, \
                m_color \
            ); \
        } \
    } while (false)
#define NETW_THREAD(m_name, m_group) NETW_PROFILE_DISCARD(m_name, m_group)
#define NETW_LOCKABLE(m_type, m_name) m_type m_name
#define NETW_ALLOC_N(m_pointer, m_size, m_name) \
    NETW_PROFILE_DISCARD(m_pointer, m_size, m_name)
#define NETW_FREE_N(m_pointer, m_name) NETW_PROFILE_DISCARD(m_pointer, m_name)
#define NETW_APP_INFO(m_text, m_size) NETW_PROFILE_DISCARD(m_text, m_size)
#define NETW_PROFILE_MESSAGE(m_text, m_size, m_color) \
    NETW_PROFILE_DISCARD(m_text, m_size, m_color)
#endif
