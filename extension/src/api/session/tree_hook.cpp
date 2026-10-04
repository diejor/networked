#include "netw/tree_hook.hpp"

#include "godot/callable.hpp"
#include "godot/hash_map.hpp"
#include "godot/local_vector.hpp"
#include "godot/multiplayer.hpp"
#include "godot/multiplayer_synchronizer.hpp"
#include "godot/object.hpp"
#include "godot/scene_tree.hpp"
#include "netw/api/entity.hpp"
#include "netw/api/entity_record.hpp"
#include "netw/api/netw_multiplayer.hpp"
#include "netw/colors.hpp"
#include "netw/profile.hpp"

using namespace godot;

namespace netw {

namespace {

constexpr uint32_t SWEEP_FLOOR = 64;
constexpr uint32_t SIEVE_FLOOR_BITS = 12;
constexpr uint32_t SIEVE_BITS_PER_MARK = 16;
constexpr uint64_t SIEVE_SPREAD = 0x9E3779B97F4A7C15ull;

struct TreeHook {
    ObjectID tree;
    bool authority_joins = false;
    HashMap<uint64_t, uint16_t> marks;
    HashMap<uint64_t, uint64_t> watched;
    LocalVector<NetwMultiplayer *> removals;
    LocalVector<NetwMultiplayer *> booked;
    LocalVector<uint64_t> sieve;
    uint32_t sieve_bits = 0;
    uint32_t sweep_at = SWEEP_FLOOR;
};

TreeHook &tree_hook() {
    static TreeHook hook;
    return hook;
}

uint32_t sieve_slot(const TreeHook &p_hook, const Object *p_object) {
    const uint64_t spread = uint64_t(uintptr_t(p_object)) * SIEVE_SPREAD;
    return uint32_t(spread >> (64 - p_hook.sieve_bits));
}

bool sieve_admits(const TreeHook &p_hook, const Object *p_object) {
    if (p_hook.sieve_bits == 0) {
        return false;
    }
    const uint32_t slot = sieve_slot(p_hook, p_object);
    return (p_hook.sieve[slot >> 6] & (uint64_t(1) << (slot & 63))) != 0;
}

void sieve_add(TreeHook &r_hook, const Object *p_object) {
    if (p_object == nullptr || r_hook.sieve_bits == 0) {
        return;
    }
    const uint32_t slot = sieve_slot(r_hook, p_object);
    r_hook.sieve[slot >> 6] |= uint64_t(1) << (slot & 63);
}

void sieve_rebuild(TreeHook &r_hook) {
    uint32_t bits = SIEVE_FLOOR_BITS;
    while ((uint64_t(1) << bits)
           < uint64_t(r_hook.marks.size()) * SIEVE_BITS_PER_MARK) {
        bits += 1;
    }
    r_hook.sieve_bits = bits;
    r_hook.sieve.resize(uint32_t((uint64_t(1) << bits) >> 6));
    for (uint32_t at = 0; at < r_hook.sieve.size(); ++at) {
        r_hook.sieve[at] = 0;
    }
    for (const KeyValue<uint64_t, uint16_t> &held : r_hook.marks) {
        sieve_add(r_hook, gd::object_of(ObjectID(held.key)));
    }
}

void sweep_marks(TreeHook &r_hook) {
    LocalVector<uint64_t> gone;
    for (const KeyValue<uint64_t, uint16_t> &held : r_hook.marks) {
        if (!gd::object_registered(ObjectID(held.key))) {
            gone.push_back(held.key);
        }
    }
    for (uint32_t at = 0; at < gone.size(); ++at) {
        r_hook.marks.erase(gone[at]);
    }
    const uint32_t kept = r_hook.marks.size();
    r_hook.sweep_at = kept * 2 > SWEEP_FLOOR ? kept * 2 : SWEEP_FLOOR;
    sieve_rebuild(r_hook);
}

uint16_t marks_of(uint64_t p_node) {
    const uint16_t *held = tree_hook().marks.getptr(p_node);
    return held != nullptr ? *held : uint16_t(0);
}

void set_marks(
    uint64_t p_node,
    uint16_t p_marks,
    const Object *p_object = nullptr
) {
    TreeHook &hook = tree_hook();
    if (p_marks == 0) {
        hook.marks.erase(p_node);
        return;
    }
    const bool fresh = !hook.marks.has(p_node);
    if (fresh && hook.marks.size() >= hook.sweep_at) {
        sweep_marks(hook);
    }
    hook.marks[p_node] = p_marks;
    if (!fresh) {
        return;
    }
    if (uint64_t(hook.marks.size()) * SIEVE_BITS_PER_MARK
        > (uint64_t(hook.sieve_bits == 0 ? 0 : 1) << hook.sieve_bits)) {
        sieve_rebuild(hook);
        return;
    }
    sieve_add(
        hook,
        p_object != nullptr ? p_object : gd::object_of(ObjectID(p_node))
    );
}

bool may_be_marked(const Object *p_object) {
    const TreeHook &hook = tree_hook();
    return !hook.marks.is_empty() && sieve_admits(hook, p_object);
}

Ref<NetwEntity> meta_entity(Node *p_node) {
    const StringName mark = NetwMultiplayer::wrapper_meta();
    if (!p_node->has_meta(mark)) {
        return Ref<NetwEntity>();
    }
    return Ref<NetwEntity>(
        Object::cast_to<NetwEntity>(p_node->get_meta(mark).operator Object *())
    );
}

} // namespace

void NetwMultiplayer::tree_hook_arm() {
    SceneTree *tree = gd::scene_tree();
    TreeHook &hook = tree_hook();
    if (tree == nullptr || hook.tree == gd::instance_id(tree)) {
        return;
    }
    hook.tree = gd::instance_id(tree);
    tree->connect(
        StringName("node_added"),
        callable_mp_static(&NetwMultiplayer::tree_node_added)
    );
    tree->connect(
        StringName("node_removed"),
        callable_mp_static(&NetwMultiplayer::tree_node_removed)
    );
}

void NetwMultiplayer::tree_watch_authority_joins() {
    tree_hook().authority_joins = true;
    tree_hook_arm();
}

void NetwMultiplayer::tree_watch_removals(NetwMultiplayer *p_session) {
    TreeHook &hook = tree_hook();
    if (!hook.removals.has(p_session)) {
        hook.removals.push_back(p_session);
    }
    tree_hook_arm();
}

void NetwMultiplayer::tree_forget_session(NetwMultiplayer *p_session) {
    TreeHook &hook = tree_hook();
    hook.removals.erase(p_session);
    hook.booked.erase(p_session);
    for (const KeyValue<uint64_t, Ref<NetwEntity>> &held :
         p_session->entity_book) {
        Node *node = Object::cast_to<Node>(gd::object_of(ObjectID(held.key)));
        if (node == nullptr) {
            continue;
        }
        node->set_meta(wrapper_meta(), held.value);
        set_marks(
            held.key,
            (marks_of(held.key) & uint16_t(~tree::MARK_BOOKED))
                | tree::MARK_ENTITY
        );
    }
    p_session->entity_book.clear();
}

Ref<NetwEntity> NetwMultiplayer::tree_booked(uint64_t p_node) {
    const TreeHook &hook = tree_hook();
    for (uint32_t at = 0; at < hook.booked.size(); ++at) {
        const Ref<NetwEntity> *held
            = hook.booked[at]->entity_book.getptr(p_node);
        if (held != nullptr) {
            return *held;
        }
    }
    return Ref<NetwEntity>();
}

NetwMultiplayer *NetwMultiplayer::tree_adopt(
    Node *p_node,
    const Ref<NetwEntity> &p_entity,
    NetwMultiplayer *p_session
) {
    NetwMultiplayer *session = p_session;
    if (session == nullptr) {
        const Ref<MultiplayerAPI> api = p_node->get_multiplayer();
        session = Object::cast_to<NetwMultiplayer>(api.ptr());
    }
    if (session == nullptr) {
        return nullptr;
    }
    const uint64_t id = uint64_t(p_node->get_instance_id());
    TreeHook &hook = tree_hook();
    if (!hook.booked.has(session)) {
        hook.booked.push_back(session);
    }
    session->entity_book.insert(id, p_entity);
    p_node->remove_meta(wrapper_meta());
    set_marks(
        id,
        (marks_of(id) & uint16_t(~tree::MARK_ENTITY)) | tree::MARK_BOOKED
    );
    return session;
}

void NetwMultiplayer::tree_release(
    Node *p_node,
    const Ref<NetwEntity> &p_entity
) {
    const uint64_t id = uint64_t(p_node->get_instance_id());
    p_node->set_meta(wrapper_meta(), p_entity);
    set_marks(
        id,
        (marks_of(id) & uint16_t(~tree::MARK_BOOKED)) | tree::MARK_ENTITY
    );
    TreeHook &hook = tree_hook();
    for (uint32_t at = 0; at < hook.booked.size(); ++at) {
        hook.booked[at]->entity_book.erase(id);
    }
}

void NetwMultiplayer::tree_node_added(Node *p_node) {
    NETW_ZONE_NC("Tree node added", colors::SCENE);
    TreeHook &hook = tree_hook();
    if (may_be_marked(p_node)) {
        const uint64_t id = uint64_t(p_node->get_instance_id());
        const uint16_t *held = hook.marks.getptr(id);
        if (held != nullptr && (*held & tree::MARKS_ENTERING) != 0) {
            tree_marked_entered(p_node, id, *held);
        }
    }
    if (!hook.watched.is_empty()) {
        Node *parent = p_node->get_parent();
        const uint64_t *entity = parent != nullptr
            ? hook.watched.getptr(uint64_t(parent->get_instance_id()))
            : nullptr;
        if (entity != nullptr) {
            NetwEntity *watcher
                = Object::cast_to<NetwEntity>(gd::object_of(ObjectID(*entity)));
            if (watcher != nullptr) {
                watcher->descendant_entered(p_node);
            }
        }
    }
    if (hook.authority_joins && p_node->get_multiplayer_authority() != 1) {
        entity_note_node_added(p_node);
    }
    if (Object::cast_to<MultiplayerSynchronizer>(p_node) != nullptr) {
        const Ref<NetwEntity> entity = wrapper_at(p_node->get_parent());
        if (entity.is_valid()) {
            entity->invalidate_synchronizers_cache();
        }
    }
}

void NetwMultiplayer::tree_marked_entered(
    Node *p_node,
    uint64_t p_id,
    uint16_t p_marks
) {
    NETW_ZONE_NC("Tree marked node entered", colors::SCENE);
    if ((p_marks & tree::MARK_ENTITY) != 0) {
        const Ref<NetwEntity> entity = meta_entity(p_node);
        const Ref<MultiplayerAPI> api = p_node->get_multiplayer();
        NetwMultiplayer *branch = Object::cast_to<NetwMultiplayer>(api.ptr());
        if (entity.is_valid()) {
            if (entity->belongs_to(branch)) {
                tree_adopt(p_node, entity, branch);
            }
            entity->tree_entered_edge();
        }
    }
    const uint16_t due
        = p_marks & uint16_t(tree::MARKS_ONCE | tree::MARK_DERIVED_ENTER);
    if (due == 0) {
        return;
    }
    set_marks(p_id, marks_of(p_id) & uint16_t(~(due & tree::MARKS_ONCE)));
    tree::declarations_entered(p_node, due);
}

void NetwMultiplayer::tree_node_removed(Node *p_node) {
    NETW_ZONE_NC("Tree node removed", colors::SCENE);
    TreeHook &hook = tree_hook();
    if (!hook.watched.is_empty()) {
        hook.watched.erase(uint64_t(p_node->get_instance_id()));
    }
    if (!may_be_marked(p_node)) {
        return;
    }
    const uint64_t id = uint64_t(p_node->get_instance_id());
    const uint16_t *held = hook.marks.getptr(id);
    if (held == nullptr) {
        return;
    }
    const uint16_t marked = *held;
    if ((marked & tree::MARK_HEARD) != 0) {
        for (uint32_t at = 0; at < hook.removals.size(); ++at) {
            hook.removals[at]->sync_note_node_removed(p_node);
        }
    }
    if ((marked & tree::MARK_BOOKED) != 0) {
        const Ref<NetwEntity> booked = tree_booked(id);
        if (booked.is_valid()) {
            booked->tree_exiting_edge();
            tree_release(p_node, booked);
        }
    } else if ((marked & tree::MARK_ENTITY) != 0) {
        const Ref<NetwEntity> carried = meta_entity(p_node);
        if (carried.is_valid()) {
            carried->tree_exiting_edge();
        }
    }
    if ((marked & tree::MARK_DERIVED_EXIT) != 0) {
        tree::declarations_exiting(p_node);
    }
}

namespace tree {

void mark(Node *p_node, uint16_t p_marks) {
    if (p_node == nullptr) {
        return;
    }
    const uint64_t id = uint64_t(p_node->get_instance_id());
    const uint16_t now = marks_of(id);
    if ((now & p_marks) != p_marks) {
        set_marks(id, now | p_marks, p_node);
    }
    NetwMultiplayer::tree_hook_arm();
}

void mark_id(uint64_t p_node, uint16_t p_marks) {
    const uint16_t now = marks_of(p_node);
    if ((now & p_marks) != p_marks) {
        set_marks(p_node, now | p_marks);
    }
    NetwMultiplayer::tree_hook_arm();
}

bool marked(Node *p_node, uint16_t p_marks) {
    return (marks_of(uint64_t(p_node->get_instance_id())) & p_marks) != 0;
}

void unmark(uint64_t p_node, uint16_t p_marks) {
    if (tree_hook().marks.is_empty()) {
        return;
    }
    set_marks(p_node, marks_of(p_node) & uint16_t(~p_marks));
}

void watch(Node *p_node, uint64_t p_entity) {
    tree_hook().watched[uint64_t(p_node->get_instance_id())] = p_entity;
    NetwMultiplayer::tree_hook_arm();
}

void unwatch(Node *p_node) {
    if (p_node != nullptr) {
        tree_hook().watched.erase(uint64_t(p_node->get_instance_id()));
    }
}

Object *entity_object_at(Node *p_node) {
    if (may_be_marked(p_node)
        && (marks_of(uint64_t(p_node->get_instance_id())) & MARK_BOOKED) != 0) {
        const Ref<NetwEntity> booked
            = NetwMultiplayer::tree_booked(uint64_t(p_node->get_instance_id()));
        if (booked.is_valid()) {
            return booked.ptr();
        }
    }
    const StringName mark = NetwMultiplayer::wrapper_meta();
    if (!p_node->has_meta(mark)) {
        return nullptr;
    }
    const Variant found = p_node->get_meta(mark);
    return found.get_type() == Variant::OBJECT ? found.operator Object *()
                                               : nullptr;
}

bool is_entity_root(Node *p_node) {
    if (may_be_marked(p_node)
        && (marks_of(uint64_t(p_node->get_instance_id())) & MARK_BOOKED) != 0) {
        return true;
    }
    return p_node->has_meta(NetwMultiplayer::wrapper_meta());
}

void despawning(Object *p_wrapper, const StringName &p_reason) {
    NetwEntity *entity = Object::cast_to<NetwEntity>(p_wrapper);
    if (entity == nullptr || !entity->watches_despawning()) {
        return;
    }
    const Ref<MultiplayerAPI> api = entity->get_multiplayer();
    NetwMultiplayer *core = Object::cast_to<NetwMultiplayer>(api.ptr());
    if (core != nullptr) {
        core->liveness_owner_despawning(p_reason, p_wrapper);
    }
}

#if defined(NETW_TESTS)
uint64_t watched_by_under_test(Node *p_node) {
    const uint64_t *held
        = tree_hook().watched.getptr(uint64_t(p_node->get_instance_id()));
    return held != nullptr ? *held : 0;
}
#endif

} // namespace tree

} // namespace netw
