#include "netw/display/tracks.hpp"

using namespace godot;

namespace netw::display {

int Tracks::declare(const StringName &key, const StringName &name) {
    const HashMap<StringName, int32_t>::ConstIterator found
        = key_index.find(key);
    if (found != key_index.end()) {
        return found->value;
    }
    const int32_t channel = channels;
    channels += 1;
    key_index[key] = channel;
    if (name_index.has(name)) {
        ambiguous.insert(name);
    } else {
        name_index[name] = channel;
    }
    return channel;
}

int Tracks::by_key(const StringName &key) const {
    const HashMap<StringName, int32_t>::ConstIterator found
        = key_index.find(key);
    return found != key_index.end() ? found->value : -1;
}

int Tracks::by_name(const StringName &name) const {
    const HashMap<StringName, int32_t>::ConstIterator found
        = name_index.find(name);
    return found != name_index.end() ? found->value : -1;
}

bool Tracks::is_ambiguous(const StringName &name) const {
    return ambiguous.has(name);
}

int Tracks::ambiguous_count() const {
    return int(ambiguous.size());
}

int Tracks::size() const {
    return int(channels);
}

void Tracks::clear() {
    key_index.clear();
    name_index.clear();
    ambiguous.clear();
    channels = 0;
}

} // namespace netw::display
