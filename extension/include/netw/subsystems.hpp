#pragma once

#include <cstdint>

#define NETW_SUBSYSTEM_TABLE(X) \
    X(CLOCK, "clock", 0) \
    X(CODEC, "codec", 1) \
    X(ENTITY, "entity", 2) \
    X(EVENT, "event", 3) \
    X(INTEREST, "interest", 4) \
    X(INTERPOLATION, "interp", 5) \
    X(LAGCOMP, "lagcomp", 6) \
    X(LIVENESS, "liveness", 7) \
    X(PREDICTION, "prediction", 8) \
    X(SCENE, "scene", 9) \
    X(SCRIPT, "script", 17) \
    X(SESSION, "session", 10) \
    X(SPAWN, "spawn", 11) \
    X(TABLE, "table", 12) \
    X(TEST, "test", 13) \
    X(TICK, "tick", 14) \
    X(TRANSPORT, "transport", 15) \
    X(WIRE, "wire", 16)

namespace netw {

enum Subsystem : uint32_t {
    SUBSYSTEM_NONE = 0U,
#define NETW_SUBSYSTEM_BIT(m_name, m_text, m_index) \
    SUBSYSTEM_##m_name = 1U << (m_index),
    NETW_SUBSYSTEM_TABLE(NETW_SUBSYSTEM_BIT)
#undef NETW_SUBSYSTEM_BIT
        SUBSYSTEM_ALL = 0xffffffffU,
};

struct SubsystemName {
    const char *text;
};

namespace sys {

#define NETW_SUBSYSTEM_NAME(m_name, m_text, m_index) \
    inline constexpr SubsystemName m_name{m_text};
NETW_SUBSYSTEM_TABLE(NETW_SUBSYSTEM_NAME)
#undef NETW_SUBSYSTEM_NAME

} // namespace sys

inline constexpr int subsystem_count() {
    int total = 0;
#define NETW_SUBSYSTEM_COUNT(m_name, m_text, m_index) total += 1;
    NETW_SUBSYSTEM_TABLE(NETW_SUBSYSTEM_COUNT)
#undef NETW_SUBSYSTEM_COUNT
    return total;
}

const char *subsystem_name(Subsystem p_subsystem);

Subsystem subsystem_of(const char *p_name);

const char *subsystem_at(int p_index);

} // namespace netw
