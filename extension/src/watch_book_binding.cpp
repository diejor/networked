#include "netw/watch_book_binding.hpp"

#include "godot/class_db.hpp"

namespace netw {

using namespace godot;

void NetwWatchBook::poll(
    int64_t p_key,
    const Array &p_values,
    const Array &p_readable
) {
    impl.poll(p_key, p_values, p_readable);
}

Array NetwWatchBook::mask_for(int64_t p_key, int64_t p_peer) const {
    uint64_t mask = 0;
    Array values;
    impl.mask_for(p_key, p_peer, mask, values);
    Array out;
    out.push_back(int64_t(mask));
    out.push_back(values);
    return out;
}

void NetwWatchBook::commit(int64_t p_key, int64_t p_peer) {
    impl.commit(p_key, p_peer);
}

void NetwWatchBook::reset(int64_t p_key) {
    impl.reset(p_key);
}

void NetwWatchBook::clear_baselines(int64_t p_key) {
    impl.clear_baselines(p_key);
}

void NetwWatchBook::clear_peer(int64_t p_peer) {
    impl.clear_peer(p_peer);
}

void NetwWatchBook::retain_baselines(
    int64_t p_key,
    const PackedInt32Array &p_recipients
) {
    impl.retain_baselines(p_key, p_recipients);
}

bool NetwWatchBook::is_inited(int64_t p_key) const {
    return impl.is_inited(p_key);
}

void NetwWatchBook::clear() {
    impl.clear();
}

void NetwWatchBook::_bind_methods() {
    ClassDB::bind_method(
        D_METHOD("poll", "key", "values", "readable"),
        &NetwWatchBook::poll,
        DEFVAL(Array())
    );
    ClassDB::bind_method(
        D_METHOD("mask_for", "key", "peer"),
        &NetwWatchBook::mask_for
    );
    ClassDB::bind_method(
        D_METHOD("commit", "key", "peer"),
        &NetwWatchBook::commit
    );
    ClassDB::bind_method(D_METHOD("reset", "key"), &NetwWatchBook::reset);
    ClassDB::bind_method(
        D_METHOD("clear_baselines", "key"),
        &NetwWatchBook::clear_baselines
    );
    ClassDB::bind_method(
        D_METHOD("clear_peer", "peer"),
        &NetwWatchBook::clear_peer
    );
    ClassDB::bind_method(
        D_METHOD("retain_baselines", "key", "recipients"),
        &NetwWatchBook::retain_baselines
    );
    ClassDB::bind_method(
        D_METHOD("is_inited", "key"),
        &NetwWatchBook::is_inited
    );
    ClassDB::bind_method(D_METHOD("clear"), &NetwWatchBook::clear);
    ClassDB::bind_integer_constant(
        "NetwWatchBook",
        StringName(),
        "FIELD_LIMIT",
        repl::WatchBook::FIELD_LIMIT
    );
}

} // namespace netw
