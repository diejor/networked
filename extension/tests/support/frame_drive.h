#pragma once

#include "netw_test.h"

#include <vector>

namespace netw_test {

struct FrameScenario {
    virtual ~FrameScenario() = default;
    virtual bool advance() = 0;
};

inline std::vector<FrameScenario *> &frame_scenarios() {
    static std::vector<FrameScenario *> scenarios;
    return scenarios;
}

inline bool register_frame_scenario(FrameScenario *p_scenario) {
    frame_scenarios().push_back(p_scenario);
    return true;
}

inline bool frame_drive_advance() {
    static size_t index = 0;
    std::vector<FrameScenario *> &scenarios = frame_scenarios();
    while (index < scenarios.size()) {
        if (scenarios[index]->advance()) {
            return true;
        }
        ++index;
    }
    return false;
}

} // namespace netw_test

#define NETW_FRAME_SCENARIO(m_type, m_name, ...) \
    static m_type m_name{__VA_ARGS__}; \
    static const bool netw_frame_registered_##m_name \
        = netw_test::register_frame_scenario(&m_name)
