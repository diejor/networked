#pragma once

#include <cstdint>

#include "netw/profile.hpp"

#if defined(DEBUG_ENABLED)
#include "godot/variant.hpp"
#endif

namespace netw::stage {

enum Stage : uint8_t {
    TRANSPORT,
    RECEIVE,
    SPAWN,
    APPLY,
    INTEREST,
    LAG_COMPENSATION,
    ROLLBACK,
    SIMULATION,
    PULL,
    SEND,
    FLUSH,
    DISPLAY,
    OTHER,
    COUNT,
};

#if defined(DEBUG_ENABLED)

extern const char *const NAMES[COUNT];
extern const char *const CATEGORY;

struct Book {
    uint64_t nanos[COUNT] = {};
    uint64_t mark = 0;
    uint64_t frame = UINT64_MAX;
    Stage open = COUNT;
    bool timing = false;
};

extern Book book;

uint64_t now_nanos();
Stage enter(Stage p_stage, uint64_t p_now);
void leave(Stage p_stage, Stage p_outer, uint64_t p_now);

bool turn(uint64_t p_frame);
void set_timing(bool p_timing);
godot::Array take();
void flush_frame();

class Scope {
    Stage stage;
    Stage outer = COUNT;
    bool on;

public:
    explicit Scope(Stage p_stage) : stage(p_stage), on(book.timing) {
        if (on) {
            outer = enter(stage, now_nanos());
        }
    }
    ~Scope() {
        if (on) {
            leave(stage, outer, now_nanos());
        }
    }
    Scope(const Scope &) = delete;
    Scope &operator=(const Scope &) = delete;
};

#endif

} // namespace netw::stage

#if defined(DEBUG_ENABLED)
#define NETW_STAGE_JOIN(m_a, m_b) m_a##m_b
#define NETW_STAGE_SCOPE(m_line) NETW_STAGE_JOIN(netw_stage_scope_, m_line)
#define NETW_STAGE(m_stage, m_name, m_color) \
    NETW_ZONE_NC(m_name, m_color); \
    netw::stage::Scope NETW_STAGE_SCOPE(__LINE__)(netw::stage::m_stage)
#define NETW_STAGE_FLUSH() netw::stage::flush_frame()
#else
#define NETW_STAGE(m_stage, m_name, m_color) \
    NETW_ZONE_NC(m_name, m_color); \
    static_assert(netw::stage::m_stage < netw::stage::COUNT)
#define NETW_STAGE_FLUSH() ((void)0)
#endif
