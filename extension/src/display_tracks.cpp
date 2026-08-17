#include "netw/display_tracks.hpp"

#include "godot/class_db.hpp"

using namespace godot;

namespace netw {

int NetwDisplayTracks::declare(
    const StringName &key,
    const StringName &name
) {
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

int NetwDisplayTracks::by_key(const StringName &key) const {
    const HashMap<StringName, int32_t>::ConstIterator found
        = key_index.find(key);
    return found != key_index.end() ? found->value : -1;
}

int NetwDisplayTracks::by_name(const StringName &name) const {
    const HashMap<StringName, int32_t>::ConstIterator found
        = name_index.find(name);
    return found != name_index.end() ? found->value : -1;
}

bool NetwDisplayTracks::is_ambiguous(const StringName &name) const {
    return ambiguous.has(name);
}

int NetwDisplayTracks::ambiguous_count() const {
    return int(ambiguous.size());
}

int NetwDisplayTracks::size() const {
    return int(channels);
}

void NetwDisplayTracks::clear() {
    key_index.clear();
    name_index.clear();
    ambiguous.clear();
    channels = 0;
}

void NetwDisplayTracks::_bind_methods() {
    ClassDB::bind_method(
        D_METHOD("declare", "key", "name"),
        &NetwDisplayTracks::declare
    );
    ClassDB::bind_method(
        D_METHOD("by_key", "key"),
        &NetwDisplayTracks::by_key
    );
    ClassDB::bind_method(
        D_METHOD("by_name", "name"),
        &NetwDisplayTracks::by_name
    );
    ClassDB::bind_method(
        D_METHOD("is_ambiguous", "name"),
        &NetwDisplayTracks::is_ambiguous
    );
    ClassDB::bind_method(
        D_METHOD("ambiguous_count"),
        &NetwDisplayTracks::ambiguous_count
    );
    ClassDB::bind_method(D_METHOD("size"), &NetwDisplayTracks::size);
    ClassDB::bind_method(D_METHOD("clear"), &NetwDisplayTracks::clear);
}

} // namespace netw
