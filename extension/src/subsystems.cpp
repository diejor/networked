#include "netw/subsystems.hpp"

namespace netw {

namespace {

struct Row {
    Subsystem bit;
    const char *name;
};

constexpr Row ROWS[] = {
#define NETW_SUBSYSTEM_ROW(m_name, m_text, m_index) \
    {SUBSYSTEM_##m_name, m_text},
    NETW_SUBSYSTEM_TABLE(NETW_SUBSYSTEM_ROW)
#undef NETW_SUBSYSTEM_ROW
};

bool same(const char *a, const char *b) {
    if (a == nullptr || b == nullptr) {
        return false;
    }
    while (*a != '\0' && *a == *b) {
        a += 1;
        b += 1;
    }
    return *a == *b;
}

} // namespace

const char *subsystem_name(Subsystem p_subsystem) {
    for (const Row &row : ROWS) {
        if (row.bit == p_subsystem) {
            return row.name;
        }
    }
    return "";
}

Subsystem subsystem_of(const char *p_name) {
    for (const Row &row : ROWS) {
        if (same(row.name, p_name)) {
            return row.bit;
        }
    }
    return SUBSYSTEM_NONE;
}

const char *subsystem_at(int p_index) {
    if (p_index < 0 || p_index >= subsystem_count()) {
        return "";
    }
    return ROWS[p_index].name;
}

} // namespace netw
