#include "netw/entity/ids.hpp"

#include <vector>

namespace netw::entity {

namespace {

struct Slot {
    int holders = 1;
};

godot::RID_Owner<Slot> &process_lifetime_owner() {
    static godot::RID_Owner<Slot> instance;
    static bool described = false;
    if (!described) {
        instance.set_description("netw::entity");
        described = true;
    }
    return instance;
}

} // namespace

godot::RID mint() {
    return process_lifetime_owner().make_rid(Slot());
}

bool minted(const godot::RID &p_entity) {
    return process_lifetime_owner().owns(p_entity);
}

bool retain(const godot::RID &p_entity) {
    Slot *slot = process_lifetime_owner().get_or_null(p_entity);
    if (slot == nullptr) {
        return false;
    }
    slot->holders += 1;
    return true;
}

void release(const godot::RID &p_entity) {
    Slot *slot = process_lifetime_owner().get_or_null(p_entity);
    if (slot == nullptr) {
        return;
    }
    slot->holders -= 1;
    if (slot->holders <= 0) {
        process_lifetime_owner().free(p_entity);
    }
}

int holders(const godot::RID &p_entity) {
    const Slot *slot = process_lifetime_owner().get_or_null(p_entity);
    return slot ? slot->holders : 0;
}

int outstanding() {
    return int(process_lifetime_owner().get_rid_count());
}

void shutdown() {
    const uint32_t count = process_lifetime_owner().get_rid_count();
    if (count == 0) {
        return;
    }
    std::vector<godot::RID> held(count);
    process_lifetime_owner().fill_owned_buffer(held.data());
    for (const godot::RID &entity : held) {
        process_lifetime_owner().free(entity);
    }
}

} // namespace netw::entity
