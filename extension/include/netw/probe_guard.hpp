#pragma once

#include <cstdint>

#include "godot/local_vector.hpp"
#include "godot/templates.hpp"

namespace netw {

class ProbeGuard {
public:
    static constexpr int RATE_PER_SECOND = 10;
    static constexpr int MAX_ACTIVE = 32;
    static constexpr int64_t WINDOW_MS = 1000;

private:
    godot::LocalVector<int64_t> window;
    godot::HashSet<int64_t> active;

public:
    bool admit(int64_t p_peer, int64_t p_now_ms);
    bool forget(int64_t p_peer);
    int active_count() const;
    int window_count() const;
    void clear();
};

} // namespace netw
