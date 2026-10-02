#include "netw/display/tracks.hpp"

using namespace godot;

namespace netw::display {

int Tracks::first_named(const StringName &name) const {
    for (uint32_t at = 0; at < names.size(); ++at) {
        if (names[at] == name) {
            return int(at);
        }
    }
    return -1;
}

int Tracks::named_count(const StringName &name) const {
    int count = 0;
    for (const StringName &held : names) {
        count += held == name ? 1 : 0;
    }
    return count;
}

int Tracks::declare(const StringName &key, const StringName &name) {
    const int found = by_key(key);
    if (found >= 0) {
        return found;
    }
    keys.push_back(key);
    names.push_back(name);
    return int(keys.size()) - 1;
}

int Tracks::by_key(const StringName &key) const {
    for (uint32_t at = 0; at < keys.size(); ++at) {
        if (keys[at] == key) {
            return int(at);
        }
    }
    return -1;
}

int Tracks::by_name(const StringName &name) const {
    return first_named(name);
}

bool Tracks::is_ambiguous(const StringName &name) const {
    return named_count(name) > 1;
}

int Tracks::ambiguous_count() const {
    int count = 0;
    for (uint32_t at = 0; at < names.size(); ++at) {
        if (first_named(names[at]) == int(at) && named_count(names[at]) > 1) {
            count += 1;
        }
    }
    return count;
}

int Tracks::size() const {
    return int(keys.size());
}

void Tracks::clear() {
    keys.reset();
    names.reset();
}

} // namespace netw::display
