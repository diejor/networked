#include "netw/entity_ids.hpp"

#include <vector>

namespace netw::entity_ids {

namespace {

struct Slot {
    int holders = 1;
};

// A function-local static, so the mint is constructed on first use rather than
// at a static-initialization order nobody controls. It outlives every session
// deliberately: a handle is not allowed to change meaning because a session
// ended.
godot::RID_Owner<Slot> &owner() {
    static godot::RID_Owner<Slot> instance;
    static bool described = false;
    if (!described) {
        instance.set_description("netw::entity_ids");
        described = true;
    }
    return instance;
}

} // namespace

godot::RID mint() {
    return owner().make_rid(Slot());
}

bool minted(const godot::RID &p_entity) {
    return owner().owns(p_entity);
}

bool retain(const godot::RID &p_entity) {
    Slot *slot = owner().get_or_null(p_entity);
    if (slot == nullptr) {
        return false;
    }
    slot->holders += 1;
    return true;
}

void release(const godot::RID &p_entity) {
    Slot *slot = owner().get_or_null(p_entity);
    if (slot == nullptr) {
        return;
    }
    slot->holders -= 1;
    if (slot->holders <= 0) {
        owner().free(p_entity);
    }
}

int holders(const godot::RID &p_entity) {
    const Slot *slot = owner().get_or_null(p_entity);
    return slot ? slot->holders : 0;
}

int outstanding() {
    return int(owner().get_rid_count());
}

void shutdown() {
    const uint32_t count = owner().get_rid_count();
    if (count == 0) {
        return;
    }
    std::vector<godot::RID> held(count);
    owner().fill_owned_buffer(held.data());
    for (const godot::RID &entity : held) {
        owner().free(entity);
    }
}

} // namespace netw::entity_ids
