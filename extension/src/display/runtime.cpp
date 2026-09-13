#include "netw/display/runtime.hpp"

#include <limits>

#include "netw/display/history.hpp"
#include "netw/log.hpp"
#include "netw/subsystems.hpp"

using namespace godot;

namespace netw::display {

Runtime::Runtime() {
    display_offset_limit = std::numeric_limits<double>::infinity();
}

Runtime::~Runtime() {
    clear_channels();
}

Channel *Runtime::add_channel() {
    Channel *channel = memnew(Channel);
    states.push_back(channel);
    return channel;
}

void Runtime::clear_channels() {
    for (Channel *channel : states) {
        memdelete(channel);
    }
    states.clear();
}

void Runtime::bind(NetwEntity *p_entity, Node *p_owner) {
    entity_port.bind(p_entity);
    owner_port.bind(p_owner);
}

Ref<NetwEntity> Runtime::entity() {
    return Ref<NetwEntity>(
        Object::cast_to<NetwEntity>(entity_port.resolve(sys::INTERPOLATION))
    );
}

RID Runtime::entity_rid() {
    const Ref<NetwEntity> held = entity();
    return held.is_valid() ? held->get_rid_handle() : RID();
}

Node *Runtime::owner() {
    return Object::cast_to<Node>(owner_port.resolve(sys::INTERPOLATION));
}

Channel *Runtime::channel_named(const StringName &p_name) const {
    NETW_WARN_COND(
        tracks.is_ambiguous(p_name),
        sys::INTERPOLATION,
        "'%s' names more than one channel on this entity, so a lookup by "
        "name answers whichever declared first",
        String(p_name)
    );
    const int at = tracks.by_name(p_name);
    if (at < 0 || at >= int(states.size())) {
        return nullptr;
    }
    return states[at];
}

void Runtime::snap_named(const StringName &p_name, const Variant &p_value) {
    Channel *channel = channel_named(p_name);
    if (channel != nullptr) {
        channel->snap(p_value);
    }
}

void Runtime::reset(int p_display_offset, int p_recommended_display_offset) {
    playhead.settle(config, p_display_offset, p_recommended_display_offset);
    for (Channel *channel : states) {
        channel->display_history().clear();
        channel->render_offset().clear();
        channel->set_last_written(channel->current_source_value());
        channel->write(channel->get_last_written());
    }
}

int64_t Runtime::authoring_tick() const {
    if (authoring_binding.is_null() || playhead.get_display_tick() < 0) {
        return -1;
    }
    for (Channel *channel : states) {
        if (!channel->get_authoring_ticks()) {
            continue;
        }
        const History &history = channel->display_history();
        const int64_t previous
            = history.bracketing_ticks(playhead.get_display_tick()).x;
        if (previous >= 0) {
            return previous;
        }
    }
    return -1;
}

Ref<NetwRingBuffer> Runtime::buffer_of(const StringName &p_track) const {
    if (pump_mode == netw::display::PUMP_CHASE) {
        return Ref<NetwRingBuffer>();
    }
    const Channel *channel = channel_named(p_track);
    if (channel == nullptr) {
        return Ref<NetwRingBuffer>();
    }
    return channel->display_history().get_buffer();
}

Variant Runtime::track_stat(
    const StringName &p_track,
    const StringName &p_stat
) const {
    const Channel *channel
        = p_track.is_empty() ? nullptr : channel_named(p_track);
    if (p_stat == StringName("sleeping")) {
        return channel != nullptr && channel->display_history().is_sleeping();
    }
    if (p_stat == StringName("offset_armed")) {
        return channel != nullptr && channel->render_offset().armed;
    }
    if (p_stat == StringName("offset_held")) {
        return channel != nullptr && channel->render_offset().is_held();
    }
    if (p_stat == StringName("buffer")) {
        return buffer_of(p_track);
    }
    if (p_stat == StringName("buffer_size")) {
        const Ref<NetwRingBuffer> buffer = buffer_of(p_track);
        return buffer.is_valid() ? buffer->size() : 0;
    }
    if (p_stat == StringName("channels")) {
        return int64_t(states.size());
    }
    if (p_stat == StringName("ambiguous")) {
        return tracks.ambiguous_count();
    }
    if (p_stat == StringName("pumped_frames")) {
        return pumped;
    }
    if (p_stat == StringName("pump_mode")) {
        return pump_mode;
    }
    if (p_stat == StringName("starvation_ticks")) {
        return playhead.get_starvation_ticks();
    }
    if (p_stat == StringName("display_lag")) {
        return playhead.get_display_lag();
    }
    if (p_stat == StringName("role")) {
        return role;
    }
    return Variant();
}

} // namespace netw::display
