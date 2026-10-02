#pragma once

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
extern const char *const PHYSICS_FRAME;
extern const char *const POLL_FRAME;
extern const char *const INTEREST_EDGES;
extern const char *const INTEREST_TRANSITIONS;
extern const char *const PREDICT_CORRECTIONS;
extern const char *const PREDICT_TAP_DRAINED;
extern const char *const RPC_CALLS_DEFERRED;
extern const char *const ROWS_PULLED;
extern const char *const AWAKE_ROWS;
extern const char *const SWEEP_POSITION;
extern const char *const ROWS_OWED;
extern const char *const ROWS_SENT;
extern const char *const BYTES_SENT;
extern const char *const BYTE_BUDGET;
extern const char *const MEMORY_STORE_ROWS;
extern const char *const MEMORY_PEER_ROWS;
extern const char *const MEMORY_PEER_FLIGHTS;
extern const char *const MEMORY_PREDICT_TRANSITIONS;
extern const char *const MEMORY_PREDICT_INPUTS;
extern const char *const MEMORY_PREDICT_JOURNAL;
extern const char *const MEMORY_PREDICT_SLOTS;
extern const char *const MEMORY_DISPLAY;
extern const char *const MEMORY_BINDINGS;
extern const char *const MEMORY_SETS;
extern const char *const MEMORY_ENTITIES;
extern const char *const MEMORY_COMPAT;

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
const char *plot_name(
    const char *p_prefix,
    int64_t p_id,
    PlotFormat p_format,
    uint32_t p_color
);
void tally(const char *p_name, int64_t p_count);
void flush_tallies();

void startup();
void shutdown();
void install_logger();
void remove_logger();

template <typename... Args> void discard(Args &&...) {
}

} // namespace netw::profile

#ifndef NETW_SUBSYSTEMS
#define NETW_SUBSYSTEMS netw::profile::SUBSYSTEM_ALL
#endif

#if defined(NETW_PROFILING)
#include "godot/profiling.hpp"

namespace netw::profile {

class FrameScope {
    const char *name;

public:
    explicit FrameScope(const char *p_name) : name(p_name) {
        FrameMarkStart(name);
    }
    ~FrameScope() {
        FrameMarkEnd(name);
    }
    FrameScope(const FrameScope &) = delete;
    FrameScope &operator=(const FrameScope &) = delete;
};

} // namespace netw::profile

#define NETW_ZONE() ZoneScoped
#define NETW_ZONE_N(m_name) ZoneScopedN(m_name)
#define NETW_ZONE_C(m_color) ZoneScopedC(m_color)
#define NETW_ZONE_NC(m_name, m_color) ZoneScopedNC(m_name, m_color)
#define NETW_ZONE_TEXT(m_text, m_size) ZoneText(m_text, m_size)
#define NETW_ZONE_TEXT_F(m_format, ...) ZoneTextF(m_format, ##__VA_ARGS__)
#define NETW_ZONE_NAME_F(m_format, ...) ZoneNameF(m_format, ##__VA_ARGS__)
#define NETW_ZONE_COLOR(m_color) ZoneColor(m_color)
#define NETW_ZONE_VALUE(m_value) ZoneValue(m_value)
#define NETW_ZONE_LEAF(m_name, m_color, m_mask) \
    ZoneNamedNC( \
        ___tracy_scoped_zone, \
        m_name, \
        m_color, \
        (NETW_SUBSYSTEMS & (m_mask)) != 0 \
    )
#define NETW_ZONE_LEAF_F(m_name, m_color, m_mask, m_format, ...) \
    NETW_ZONE_LEAF(m_name, m_color, m_mask); \
    if constexpr ((NETW_SUBSYSTEMS & (m_mask)) != 0) { \
        ZoneTextF(m_format, ##__VA_ARGS__); \
    }
#define NETW_FRAME(m_name) netw::profile::FrameScope netw_frame_scope(m_name)
#define NETW_PLOT(m_name, m_value) TracyPlot(m_name, m_value)
#define NETW_PROFILE_CONNECTED() TracyIsConnected
#define NETW_TALLY(m_name, m_count) netw::profile::tally(m_name, m_count)
#define NETW_TALLY_FLUSH() netw::profile::flush_tallies()
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
#define NETW_ZONE_LEAF(m_name, m_color, m_mask) \
    NETW_PROFILE_DISCARD(m_name, m_color, m_mask)
#define NETW_ZONE_LEAF_F(m_name, m_color, m_mask, m_format, ...) \
    NETW_PROFILE_DISCARD(m_name, m_color, m_mask, m_format, ##__VA_ARGS__)
#define NETW_FRAME(m_name) NETW_PROFILE_DISCARD(m_name)
#define NETW_PLOT(m_name, m_value) NETW_PROFILE_DISCARD(m_name, m_value)
#define NETW_PROFILE_CONNECTED() false
#define NETW_TALLY(m_name, m_count) NETW_PROFILE_DISCARD(m_name, m_count)
#define NETW_TALLY_FLUSH() ((void)0)
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
