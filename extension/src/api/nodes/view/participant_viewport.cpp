#include "netw/api/nodes/view/participant_viewport.hpp"

#include "godot/callable.hpp"
#include "godot/class_db.hpp"
#include "godot/math.hpp"
#include "godot/viewport.hpp"
#include "netw/log.hpp"

using namespace godot;

namespace netw {

namespace {

const char *SIG_SIZE_CHANGED = "size_changed";

} // namespace

ParticipantWindow *ParticipantViewport::slot_at(uint32_t p_index) const {
    return Object::cast_to<ParticipantWindow>(gd::object_of(slots[p_index]));
}

ParticipantWindow *ParticipantViewport::add_slot(ParticipantWindow *p_slot) {
    if (p_slot == nullptr) {
        return nullptr;
    }
    if (!has_slot(p_slot)) {
        slots.push_back(gd::instance_id(p_slot));
    }
    p_slot->set_visible(true);
    relayout();
    return p_slot;
}

void ParticipantViewport::forget(const ObjectID &p_slot) {
    for (uint32_t at = 0; at < slots.size(); ++at) {
        if (slots[at] == p_slot) {
            slots.remove_at(at);
            break;
        }
    }
    LocalVector<int64_t> orphaned;
    for (const KeyValue<int64_t, ObjectID> &row : device_slots) {
        if (row.value == p_slot) {
            orphaned.push_back(row.key);
        }
    }
    for (const int64_t device : orphaned) {
        device_slots.erase(device);
    }
}

void ParticipantViewport::remove_slot(ParticipantWindow *p_slot) {
    if (!has_slot(p_slot)) {
        return;
    }
    forget(gd::instance_id(p_slot));
    p_slot->set_visible(false);
    relayout();
}

bool ParticipantViewport::has_slot(ParticipantWindow *p_slot) const {
    if (p_slot == nullptr) {
        return false;
    }
    const ObjectID wanted = gd::instance_id(p_slot);
    for (uint32_t at = 0; at < slots.size(); ++at) {
        if (slots[at] == wanted) {
            return true;
        }
    }
    return false;
}

TypedArray<ParticipantWindow> ParticipantViewport::get_slots() const {
    TypedArray<ParticipantWindow> out;
    for (uint32_t at = 0; at < slots.size(); ++at) {
        ParticipantWindow *slot = slot_at(at);
        if (slot != nullptr) {
            out.push_back(slot);
        }
    }
    return out;
}

void ParticipantViewport::assign_device(
    int64_t p_device_id,
    ParticipantWindow *p_slot
) {
    NETW_ERR_COND(
        !has_slot(p_slot),
        sys::SCENE,
        "device %d cannot be routed to a window this viewport does not tile",
        p_device_id
    );
    device_slots[p_device_id] = gd::instance_id(p_slot);
}

ParticipantWindow *ParticipantViewport::device_slot(int64_t p_device_id) const {
    const HashMap<int64_t, ObjectID>::ConstIterator found
        = device_slots.find(p_device_id);
    if (found == device_slots.end()) {
        return nullptr;
    }
    return Object::cast_to<ParticipantWindow>(gd::object_of(found->value));
}

void ParticipantViewport::set_embeds_subwindows(bool p_embeds) {
    embeds_subwindows = p_embeds;
    Viewport *enclosing = get_viewport();
    if (enclosing != nullptr) {
        enclosing->set_embedding_subwindows(embeds_subwindows);
    }
}

void ParticipantViewport::NETW_NODE_INPUT(const Ref<InputEvent> &p_event) {
    const bool joypad
        = Object::cast_to<InputEventJoypadButton>(p_event.ptr()) != nullptr
        || Object::cast_to<InputEventJoypadMotion>(p_event.ptr()) != nullptr;
    if (!joypad) {
        return;
    }
    ParticipantWindow *slot = device_slot(p_event->get_device());
    if (slot == nullptr) {
        return;
    }
    slot->send_input(p_event);
    Viewport *enclosing = get_viewport();
    if (enclosing != nullptr) {
        enclosing->set_input_as_handled();
    }
}

void ParticipantViewport::relayout() {
    if (slots.is_empty()) {
        return;
    }
    Viewport *enclosing = get_viewport();
    if (enclosing == nullptr) {
        return;
    }
    const Rect2 rect = enclosing->get_visible_rect();
    const int count = int(slots.size());
    const int columns = int(Math::ceil(Math::sqrt(double(count))));
    const int rows = int(Math::ceil(double(count) / double(columns)));
    const Vector2 cell(
        rect.size.x / real_t(columns),
        rect.size.y / real_t(rows)
    );

    for (int at = 0; at < count; ++at) {
        ParticipantWindow *slot = slot_at(uint32_t(at));
        if (slot == nullptr) {
            continue;
        }
        const int column = at % columns;
        const int row = at / columns;
        slot->set_tiled_rect(Rect2i(
            Vector2i(
                int(rect.position.x + real_t(column) * cell.x),
                int(rect.position.y + real_t(row) * cell.y)
            ),
            Vector2i(int(cell.x), int(cell.y))
        ));
    }
}

void ParticipantViewport::watch_enclosing_viewport(bool p_watch) {
    Viewport *enclosing = get_viewport();
    if (enclosing == nullptr) {
        return;
    }
    const Callable settle = callable_mp(this, &ParticipantViewport::relayout);
    const bool watching = enclosing->is_connected(SIG_SIZE_CHANGED, settle);
    if (p_watch && !watching) {
        enclosing->connect(SIG_SIZE_CHANGED, settle);
    } else if (!p_watch && watching) {
        enclosing->disconnect(SIG_SIZE_CHANGED, settle);
    }
}

void ParticipantViewport::_notification(int p_what) {
    if (p_what == NOTIFICATION_READY) {
        set_process_input(true);
        set_embeds_subwindows(embeds_subwindows);
        watch_enclosing_viewport(true);
        relayout();
    } else if (p_what == NOTIFICATION_EXIT_TREE) {
        watch_enclosing_viewport(false);
        for (uint32_t at = slots.size(); at > 0; --at) {
            ParticipantWindow *slot = slot_at(at - 1);
            if (slot != nullptr) {
                slot->set_visible(false);
            }
        }
        slots.clear();
        device_slots.clear();
    }
}

void ParticipantViewport::_bind_methods() {
    ClassDB::bind_method(
        D_METHOD("add_slot", "slot"),
        &ParticipantViewport::add_slot
    );
    ClassDB::bind_method(
        D_METHOD("remove_slot", "slot"),
        &ParticipantViewport::remove_slot
    );
    ClassDB::bind_method(
        D_METHOD("has_slot", "slot"),
        &ParticipantViewport::has_slot
    );
    ClassDB::bind_method(
        D_METHOD("get_slots"),
        &ParticipantViewport::get_slots
    );
    ClassDB::bind_method(
        D_METHOD("assign_device", "device_id", "slot"),
        &ParticipantViewport::assign_device
    );
    ClassDB::bind_method(
        D_METHOD("device_slot", "device_id"),
        &ParticipantViewport::device_slot
    );
    ClassDB::bind_method(
        D_METHOD("set_embeds_subwindows", "embeds"),
        &ParticipantViewport::set_embeds_subwindows
    );
    ClassDB::bind_method(
        D_METHOD("get_embeds_subwindows"),
        &ParticipantViewport::get_embeds_subwindows
    );
    ADD_PROPERTY(
        PropertyInfo(Variant::BOOL, "embeds_subwindows"),
        "set_embeds_subwindows",
        "get_embeds_subwindows"
    );
}

} // namespace netw
