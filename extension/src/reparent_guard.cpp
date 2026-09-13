#include "netw/reparent_guard.hpp"

#include <vector>

using namespace godot;

namespace netw {

namespace {

const char *PROP_LAYER = "collision_layer";
const char *PROP_MASK = "collision_mask";

} // namespace

RID ReparentGuard::open(Node *p_body) {
    if (p_body == nullptr) {
        return RID();
    }
    const ObjectID body = gd::instance_id(p_body);
    if (const RID *standing = by_body.getptr(uint64_t(body))) {
        Hold *held = holds.get_or_null(*standing);
        if (held != nullptr) {
            held->holders += 1;
            return *standing;
        }
        by_body.erase(uint64_t(body));
    }
    Hold hold;
    hold.body = body;
    hold.holders = 1;
    hold.prior_mode = int(p_body->get_process_mode());
    hold.has_layer = gd::has_property(p_body, StringName(PROP_LAYER));
    hold.has_mask = gd::has_property(p_body, StringName(PROP_MASK));
    if (hold.has_layer) {
        hold.prior_layer = int64_t(p_body->get(StringName(PROP_LAYER)));
        p_body->set(StringName(PROP_LAYER), int64_t(0));
    }
    if (hold.has_mask) {
        hold.prior_mask = int64_t(p_body->get(StringName(PROP_MASK)));
        p_body->set(StringName(PROP_MASK), int64_t(0));
    }
    p_body->set_process_mode(Node::PROCESS_MODE_DISABLED);
    const RID opened = holds.make_rid(hold);
    by_body.insert(uint64_t(body), opened);
    return opened;
}

bool ReparentGuard::is_open(const RID &p_guard) const {
    return holds.get_or_null(p_guard) != nullptr;
}

bool ReparentGuard::tracks_physics(const RID &p_guard) const {
    const Hold *hold = holds.get_or_null(p_guard);
    return hold != nullptr && (hold->has_layer || hold->has_mask);
}

Node *ReparentGuard::body_of(const RID &p_guard) const {
    const Hold *hold = holds.get_or_null(p_guard);
    if (hold == nullptr) {
        return nullptr;
    }
    return Object::cast_to<Node>(gd::object_of(hold->body));
}

void ReparentGuard::arm(
    const RID &p_guard,
    Object *p_tree,
    int p_frames,
    const Callable &p_answered,
    const Callable &p_frame_callback
) {
    Hold *hold = holds.get_or_null(p_guard);
    if (hold == nullptr) {
        return;
    }
    hold->tree = gd::instance_id(p_tree);
    hold->frames = p_frames;
    hold->answered = p_answered;
    hold->frame_callback = p_frame_callback;
}

bool ReparentGuard::spend(const RID &p_guard) {
    Hold *hold = holds.get_or_null(p_guard);
    if (hold == nullptr) {
        return false;
    }
    hold->frames -= 1;
    return hold->frames <= 0;
}

Callable ReparentGuard::take_answer(const RID &p_guard) {
    Hold *hold = holds.get_or_null(p_guard);
    if (hold == nullptr) {
        return Callable();
    }
    const Callable answered = hold->answered;
    hold->answered = Callable();
    return answered;
}

Callable ReparentGuard::close_window(const RID &p_guard) {
    Hold *hold = holds.get_or_null(p_guard);
    if (hold == nullptr) {
        return Callable();
    }
    const Callable spent = hold->frame_callback;
    hold->tree = ObjectID();
    hold->frames = 0;
    hold->answered = Callable();
    hold->frame_callback = Callable();
    return spent;
}

Object *ReparentGuard::window_tree(const RID &p_guard) const {
    const Hold *hold = holds.get_or_null(p_guard);
    if (hold == nullptr) {
        return nullptr;
    }
    return gd::object_of(hold->tree);
}

void ReparentGuard::resume_processing(const RID &p_guard) {
    const Hold *hold = holds.get_or_null(p_guard);
    if (hold == nullptr) {
        return;
    }
    Node *body = Object::cast_to<Node>(gd::object_of(hold->body));
    if (body == nullptr) {
        return;
    }
    body->set_process_mode(Node::ProcessMode(hold->prior_mode));
}

void ReparentGuard::release(const RID &p_guard) {
    Hold *hold = holds.get_or_null(p_guard);
    if (hold == nullptr) {
        return;
    }
    hold->holders -= 1;
    if (hold->holders > 0) {
        return;
    }
    by_body.erase(uint64_t(hold->body));
    Node *body = Object::cast_to<Node>(gd::object_of(hold->body));
    if (body != nullptr) {
        if (hold->has_layer) {
            body->set(StringName(PROP_LAYER), hold->prior_layer);
        }
        if (hold->has_mask) {
            body->set(StringName(PROP_MASK), hold->prior_mask);
        }
        body->set_process_mode(Node::ProcessMode(hold->prior_mode));
    }
    holds.free(p_guard);
}

void ReparentGuard::clear() {
    const uint32_t count = holds.get_rid_count();
    if (count == 0) {
        return;
    }
    std::vector<RID> owned(count);
    holds.fill_owned_buffer(owned.data());
    for (const RID &guard : owned) {
        Hold *hold = holds.get_or_null(guard);
        if (hold != nullptr) {
            hold->holders = 1;
        }
        release(guard);
    }
}

ReparentGuard::ReparentGuard() {
    holds.set_description("netw::ReparentGuard::Hold");
}

ReparentGuard::~ReparentGuard() {
    clear();
}

} // namespace netw
