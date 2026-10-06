#include "netw/api/sync_pipeline.hpp"

#include <cstdlib>

#include "godot/class_db.hpp"
#include "godot/scene_tree.hpp"
#include "godot/utility.hpp"
#include "netw/api/event_plane.hpp"
#include "netw/api/interpolate.hpp"
#include "netw/api/member_config.hpp"
#include "netw/api/netw_multiplayer.hpp"
#include "netw/api/prediction.hpp"
#include "netw/api/property_config.hpp"
#include "netw/colors.hpp"
#include "netw/entity/control.hpp"
#include "netw/log.hpp"
#include "netw/object_port.hpp"
#include "netw/predict/tick_work.hpp"
#include "netw/profile.hpp"
#include "netw/property_set_builder.hpp"
#include "netw/repl/snapshot_frame.hpp"
#include "netw/script/model.hpp"
#include "netw/subsystems.hpp"
#include "netw/sync_authoring.hpp"

using namespace godot;

namespace netw {

using repl::PropertyBinding;
using repl::PropertyBindingPtr;
using repl::PropertyColumn;
using repl::PropertySet;

namespace {

const char *SIG_ENTITY_LIVE = "entity_live";
const char *CONTRACT_KEY_PREFIX = "sync-prediction-contract?";
const char *UNROUTABLE_META = "_netw_unroutable_warned";

const int LANE_ROW = 0;
const int LANE_WINDOW = 1;
const int LANE_RETAINED = 2;

const StringName &decode_seam() {
    static const StringName name("_sync_decode");
    return name;
}

constexpr int INTERPOLATED_LIMIT = 64;

uint64_t interpolated_mask(
    PropertyBinding *p_binding,
    Node *p_node,
    const store::RowAddress &p_at,
    const store::Table &p_table,
    uint32_t p_lane
) {
    PropertyBinding::InterpolatedColumns &held
        = p_binding->interpolated[p_lane];
    const uint64_t epoch = script::model::interpolation_epoch();
    const uint64_t script = PropertyBinding::script_generation_of(p_node);
    if (held.epoch == epoch && held.script == script
        && held.table == p_at.table) {
        return held.mask;
    }
    uint64_t mask = 0;
    const int columns = p_table.schema.column_count();
    for (int column = 0; column < columns && column < INTERPOLATED_LIMIT;
         ++column) {
        const StringName &key = p_table.schema.at(column)->key;
        if (script::model::get_node_property_interpolator(p_node, key)
                .is_valid()) {
            mask |= uint64_t(1) << column;
        }
    }
    held.mask = mask;
    held.epoch = epoch;
    held.script = script;
    held.table = p_at.table;
    return mask;
}

#if defined(NETW_TESTS)
struct ReceiveAllocations {
    NetwMultiplayer *plane;
    const int64_t opened;

    explicit ReceiveAllocations(NetwMultiplayer *p_plane)
        : plane(p_plane), opened(store::allocations_seen()) {
    }

    ~ReceiveAllocations() {
        if (plane != nullptr) {
            plane->idle_work.receive_allocations
                += store::allocations_seen() - opened;
        }
    }
};
#endif

} // namespace

NetwMultiplayer *SyncPipeline::core() const {
    return Object::cast_to<NetwMultiplayer>(gd::instance_from_id(core_id));
}

Object *SyncPipeline::api() const {
    return gd::instance_from_id(api_id);
}

void SyncPipeline::set_core(Object *p_core) {
    NetwMultiplayer *plane = Object::cast_to<NetwMultiplayer>(p_core);
    core_id = gd::instance_id(plane);
    hears_entity_live = plane != nullptr;
}

void SyncPipeline::set_api(Object *p_api) {
    api_id = gd::instance_id(p_api);
}

void SyncPipeline::set_sync_model(NetwSyncModel *p_model) {
    sync_model = p_model;
}

void SyncPipeline::set_channels(
    int64_t p_row,
    int64_t p_row_window,
    int64_t p_row_delta,
    int64_t p_row_control,
    int64_t p_property,
    int64_t p_signal
) {
    channel_row = p_row;
    channel_row_window = p_row_window;
    channel_row_delta = p_row_delta;
    channel_row_control = p_row_control;
    channel_property = p_property;
    channel_signal = p_signal;
}

void SyncPipeline::set_stage_seams(
    const Callable &p_encode,
    const Callable &p_decode
) {
    encode_stage_seam = p_encode;
    decode_stage_seam = p_decode;
}

void SyncPipeline::set_tap_seam(const Callable &p_tap) {
    tap_seam = p_tap;
}

ReplicationSend *SyncPipeline::row_send() {
    NetwMultiplayer *plane = core();
    if (!row_sender_armed && plane != nullptr) {
        row_sender_armed = true;
        row_sender.set_encode_stage(
            callable_mp(plane, &NetwMultiplayer::sync_pipeline_stage_row_frame)
        );
        row_sender.watch_store(&plane->store());
    }
    return &row_sender;
}

PackedByteArray SyncPipeline::stage_row_frame(
    int64_t p_peer,
    int64_t p_frame_tick,
    int64_t,
    int64_t,
    int64_t,
    const PackedByteArray &p_bytes
) {
    return run_encode_stage(p_peer, p_frame_tick, p_bytes);
}

PackedByteArray SyncPipeline::run_encode_stage(
    int64_t p_peer,
    int64_t p_tick,
    const PackedByteArray &p_bytes
) {
    NETW_ZONE_NC("SyncPipeline encode stage", colors::CODEC);
    NETW_ZONE_VALUE(p_bytes.size());
    PackedByteArray bytes = p_bytes;
    if (encode_stage_seam.is_valid()) {
        bytes = encode_stage_seam.call(p_peer, p_bytes, p_tick);
    }
    NetwMultiplayer *plane = core();
    if (plane != nullptr && plane->event_wants(EventPlane::SYNC_ENCODE, 0)) {
        Dictionary detail;
        detail[StringName("bytes")] = bytes.size();
        plane->event_emit(
            EventPlane::SYNC_ENCODE,
            0,
            detail,
            StringName(),
            p_peer,
            OK,
            Dictionary()
        );
    }
    return bytes;
}

Error SyncPipeline::decode_stage_body() {
    NetwMultiplayer *plane = core();
    if (plane == nullptr || !stage_at.valid()) {
        return ERR_INVALID_DATA;
    }
    if (stage_landing != store::Landing::ACCEPTED) {
        stage_landing = plane->store().receive(
            stage_at,
            int(stage_sender),
            stage_payload,
            datagram_tick,
            stage_windowed
        );
    }
    return stage_landing == store::Landing::ACCEPTED ? OK : ERR_INVALID_DATA;
}

Error SyncPipeline::run_decode_stage(
    const RID &p_entity,
    int64_t p_comp,
    const PackedByteArray &p_payload
) {
    NetwMultiplayer *plane = core();
    NETW_ZONE_LEAF_F(
        "Row decode",
        colors::CODEC,
        profile::SUBSYSTEM_ENTITY,
        "entity %lld",
        plane != nullptr ? static_cast<long long>(
                               plane->get_liveness_core()->route_of(p_entity)
                           )
                         : 0LL
    );
    NETW_ZONE_VALUE(p_payload.size());
    Error verdict = OK;
    if (!decode_stage_seam.is_valid() || plane == nullptr
        || !plane->overrides_seam(decode_seam())) {
        verdict = decode_stage_body();
    } else {
#if defined(NETW_TESTS)
        plane->idle_work.decode_stages += 1;
#endif
        verdict = Error(
            int(decode_stage_seam.call(
                p_entity,
                p_comp,
                0,
                -1,
                p_payload,
                callable_mp(
                    plane,
                    &NetwMultiplayer::sync_pipeline_decode_stage_body
                )
            ))
        );
    }
    if (plane != nullptr) {
        const int64_t route = plane->get_liveness_core()->route_of(p_entity);
        if (plane->event_wants(EventPlane::SYNC_DECODE, route)) {
            Dictionary detail;
            detail[StringName("comp")] = p_comp;
            plane->event_emit(
                EventPlane::SYNC_DECODE,
                route,
                detail,
                StringName(),
                0,
                verdict,
                Dictionary()
            );
        }
    }
    return verdict;
}

void SyncPipeline::register_derived(Node *p_node) {
    if (p_node == nullptr) {
        return;
    }
    NETW_ZONE_NC("SyncPipeline register derived", colors::WIRE);
    NETW_ZONE_VALUE(bindings.size());
    const Ref<Script> script = p_node->get_script();
    if (script.is_null()) {
        return;
    }
    const LocalVector<uint64_t> *registered = node_bindings(p_node);
    if (registered != nullptr && !registered->is_empty()) {
        return;
    }
    Object *shell = api();
    const uint64_t state = property_set_builder::from_script(
        script,
        PropertySet::RECORD_STATE,
        shell,
        p_node
    );
    if (state != repl::SetBook::NONE) {
        register_property_set(p_node, state);
    }
    const uint64_t input = property_set_builder::from_script(
        script,
        PropertySet::RECORD_INPUT,
        shell,
        p_node
    );
    if (input != repl::SetBook::NONE) {
        register_property_set(p_node, input);
    }
    queue_prediction_contract_check(p_node, set_of(state), set_of(input));
    const uint64_t broadcast = property_set_builder::from_script(
        script,
        PropertySet::RECORD_BROADCAST,
        shell,
        p_node
    );
    if (broadcast != repl::SetBook::NONE) {
        register_property_set(p_node, broadcast);
    }
}

PropertySet *SyncPipeline::set_of(uint64_t p_set) const {
    NetwMultiplayer *plane = core();
    return plane != nullptr ? plane->property_set_book().get(p_set) : nullptr;
}

void SyncPipeline::note_schema_seal(const PropertySet *p_set) {
    const NetwMultiplayer *plane = core();
    if (plane != nullptr
        && plane->session_get_state()
            != NetwMultiplayer::SESSION_STATE_OFFLINE) {
        return;
    }
    schemas_sealed_before_open[p_set->identity_hash()] = true;
}

int64_t SyncPipeline::sealed_schema_identity() const {
    uint64_t fold = 0;
    for (const KeyValue<int64_t, bool> &entry : schemas_sealed_before_open) {
        fold ^= uint64_t(entry.key);
    }
    return int64_t(fold);
}

Error SyncPipeline::register_property_set(Node *p_node, uint64_t p_set) {
    const PropertySet *declared = set_of(p_set);
    if (p_node == nullptr || declared == nullptr || !declared->get_sealed()) {
        return ERR_INVALID_DATA;
    }
    NETW_ZONE_NC("SyncPipeline register property set", colors::WIRE);
    NETW_ZONE_VALUE(bindings.size());
    note_schema_seal(declared);
    repl::BindingBook &book = core()->property_binding_book();
    PropertyBinding *held = nullptr;
    if (const LocalVector<uint64_t> *same = node_bindings(p_node)) {
        for (uint32_t at = 0; at < same->size(); ++at) {
            PropertyBinding *candidate = live((*same)[at]);
            const PropertySet *other
                = candidate != nullptr ? candidate->get_set() : nullptr;
            if (other != nullptr
                && other->get_record() == declared->get_record()) {
                held = candidate;
                break;
            }
        }
    }
    if (held != nullptr) {
        if (held->set_id == p_set) {
            bind_declaration(held);
            return OK;
        }
        drop_declaration(held);
        const uint64_t replaced = held->id;
        PropertyBinding *replacement = book.create(p_set, p_node);
        capture_declaration_key(replacement);
        replace_binding(binding_position(held), replacement);
        bind_declaration(replacement);
        core()->predict_rebind_replaced(replaced);
        return OK;
    }
    PropertyBinding *created = book.create(p_set, p_node);
    capture_declaration_key(created);
    append_binding(created);
    bind_declaration(created);
    if (declared->get_record() == PropertySet::RECORD_STATE) {
        register_state_timeline(p_node);
    }
    return OK;
}

void SyncPipeline::queue_prediction_contract_check(
    Node *p_node,
    const PropertySet *p_state,
    const PropertySet *p_input
) {
    NetwMultiplayer *plane = core();
    if (plane == nullptr || p_state == nullptr || p_input == nullptr
        || p_state->columns.is_empty() || p_input->columns.is_empty()) {
        return;
    }
    PackedStringArray parts;
    for (const PropertyColumn &column : p_state->columns) {
        parts.push_back(String("s:") + String(column.get_key()));
    }
    for (const PropertyColumn &column : p_input->columns) {
        parts.push_back(String("i:") + String(column.get_key()));
    }
    const int64_t config_hash = int64_t(String("\n").join(parts).hash());
    const int64_t instance_id = int64_t(gd::instance_id(p_node));
    const HashMap<int64_t, int64_t>::ConstIterator seen
        = contract_hashes.find(instance_id);
    if (seen && seen->value == config_hash) {
        return;
    }
    contract_hashes[instance_id] = config_hash;
    plane->session_defer(
        callable_mp(
            plane,
            &NetwMultiplayer::sync_pipeline_report_missing_prediction_component
        )
            .bind(p_node, config_hash),
        StringName(vformat("%s%d", String(CONTRACT_KEY_PREFIX), instance_id))
    );
}

void SyncPipeline::report_missing_prediction_component(
    Node *p_node,
    int64_t p_config_hash
) {
    Node *node = p_node;
    if (node == nullptr) {
        return;
    }
    const HashMap<int64_t, int64_t>::ConstIterator seen
        = contract_hashes.find(int64_t(gd::instance_id(node)));
    if (!seen || seen->value != p_config_hash) {
        return;
    }
    const Ref<NetwEntity> entity = NetwEntity::of(node);
    if (entity.is_valid()) {
        const Ref<NetwPrediction> prediction = entity->prediction_if_minted();
        if (prediction.is_valid() && prediction->is_registered()) {
            return;
        }
    }
    if (authoring::declares_prediction(node)) {
        return;
    }
    const String entity_name = entity.is_valid() ? String(entity->get_name())
                                                 : String(node->get_name());
    NETW_WARN(
        sys::PREDICTION,
        "%s",
        missing_prediction_component_message(entity_name)
    );
}

String SyncPipeline::missing_prediction_component_message(
    const String &p_entity_name
) {
    return p_entity_name
        + String(
               " declares state and input properties but no prediction, so "
               "its controller sends input without predicting. Fill the "
               "prediction block on its MultiplayerSynchronizer, or call "
               "NetwMultiplayer.predict_declare."
        );
}

void SyncPipeline::register_state_timeline(Node *p_node) {
    NetwMultiplayer *plane = core();
    if (plane == nullptr || !plane->is_host()) {
        return;
    }
    const Ref<NetwEntity> entity = NetwEntity::of(p_node);
    if (entity.is_valid()) {
        plane->lagcomp_timeline_declare(plane->entity_of(entity->get_owner()));
    }
}

bool SyncPipeline::holds_state_binding(const Ref<NetwEntity> &p_entity) const {
    NETW_ZONE_NC("SyncPipeline holds state binding", colors::WIRE);
    NETW_ZONE_VALUE(bindings.size());
    for (uint32_t at = 0; at < bindings.size(); ++at) {
        PropertyBinding *const held = live(bindings[at]);
        if (held->get_set()->get_record() != PropertySet::RECORD_STATE) {
            continue;
        }
        if (NetwEntity::of(held->node()) == p_entity) {
            return true;
        }
    }
    return false;
}

void SyncPipeline::reconcile_state_timeline(Node *p_node) {
    NetwMultiplayer *plane = core();
    if (plane == nullptr || !plane->is_host()) {
        return;
    }
    const Ref<NetwEntity> entity = NetwEntity::of(p_node);
    if (entity.is_null() || holds_state_binding(entity)) {
        return;
    }
    plane->lagcomp_timeline_undeclare(entity->get_rid_handle());
}

void SyncPipeline::reconcile_dropped_state_timelines() {
    NetwMultiplayer *plane = core();
    if (plane == nullptr || !plane->is_host()) {
        state_binding_dropped.clear();
        return;
    }
    for (uint32_t at = 0; at < state_binding_dropped.size(); ++at) {
        const RID entity = state_binding_dropped[at];
        const Ref<NetwEntity> wrapper
            = Object::cast_to<NetwEntity>(plane->entity_get_view(entity).ptr());
        if (wrapper.is_null() || holds_state_binding(wrapper)) {
            continue;
        }
        plane->lagcomp_timeline_undeclare(entity);
    }
    state_binding_dropped.clear();
}

PropertyBindingPtr SyncPipeline::derived_binding(
    Node *p_node,
    int64_t p_record
) {
    const LocalVector<uint64_t> *same = node_bindings(p_node);
    if (same == nullptr) {
        return nullptr;
    }
    for (uint32_t at = 0; at < same->size(); ++at) {
        PropertyBinding *candidate = live((*same)[at]);
        if (candidate->get_set()->get_record() == p_record) {
            return candidate;
        }
    }
    return nullptr;
}

repl::BindingBook *SyncPipeline::binding_book() const {
    NetwMultiplayer *plane = core();
    return plane != nullptr ? &plane->property_binding_book() : nullptr;
}

PropertyBinding *SyncPipeline::live(uint64_t p_binding) const {
    repl::BindingBook *book = binding_book();
    return book != nullptr ? book->live(p_binding) : nullptr;
}

LocalVector<SyncPipeline::IndexedBinding> *SyncPipeline::held_by(
    uint64_t p_entity
) const {
    if (p_entity == 0) {
        return const_cast<LocalVector<IndexedBinding> *>(&loose_bindings);
    }
    NetwEntity *entity
        = Object::cast_to<NetwEntity>(gd::object_of(ObjectID(p_entity)));
    NetwEntityRecord *record
        = entity != nullptr ? entity->get_record() : nullptr;
    return record != nullptr ? &record->get_held_bindings() : nullptr;
}

void SyncPipeline::seat_record(PropertyBinding *p_binding) {
    Node *node = p_binding->node();
    const Ref<NetwEntity> entity = NetwEntity::of(node);
    const PropertySet *set = p_binding->get_set();
    if (entity.is_null() || set == nullptr || entity->get_owner() != node
        || entity->get_record() == nullptr) {
        return;
    }
    entity->get_record()->set_record_binding(set->get_record(), p_binding->id);
}

void SyncPipeline::unseat_record(
    uint64_t p_entity,
    const PropertyBinding *p_binding
) {
    NetwEntity *entity = p_entity != 0
        ? Object::cast_to<NetwEntity>(gd::object_of(ObjectID(p_entity)))
        : NetwEntity::of(p_binding->node()).ptr();
    NetwEntityRecord *record
        = entity != nullptr ? entity->get_record() : nullptr;
    const PropertySet *set = p_binding->get_set();
    if (record == nullptr || set == nullptr
        || record->get_record_binding(set->get_record()) != p_binding->id) {
        return;
    }
    record->set_record_binding(set->get_record(), 0);
}

void SyncPipeline::unregister_derived(Node *p_node) {
    bool dropped_state = false;
    for (uint32_t at = bindings.size(); at > 0; --at) {
        PropertyBinding *const held = live(bindings[at - 1]);
        if (held->node() != p_node) {
            continue;
        }
        drop_declaration(held);
        dropped_state = dropped_state
            || held->get_set()->get_record() == PropertySet::RECORD_STATE;
        remove_binding(at - 1);
    }
    const Ref<NetwEntity> entity = NetwEntity::of(p_node);
    if (!dropped_state || entity.is_null()) {
        return;
    }
    state_binding_dropped.push_back(entity->get_rid_handle());
}

void SyncPipeline::watch_tree() {
    if (tree_watched) {
        return;
    }
    NetwMultiplayer *plane = core();
    SceneTree *tree = gd::scene_tree();
    if (plane == nullptr || tree == nullptr) {
        return;
    }
    tree_watched = true;
    NetwMultiplayer::tree_watch_removals(plane);
    plane->store().residents.watch(true);
    for (uint32_t at = 0; at < bindings.size(); ++at) {
        Node *node = live(bindings[at])->node();
        if (node == nullptr || !node->is_inside_tree()) {
            prune_candidates.push_back(binding_nodes[at]);
        }
    }
}

void SyncPipeline::note_node_removed(Node *p_node) {
    if (p_node == nullptr) {
        return;
    }
    const uint64_t key = uint64_t(p_node->get_instance_id());
    repl::BindingBook *book = binding_book();
    if (book != nullptr && book->on_node(key) != nullptr) {
        prune_candidates.push_back(key);
    }
}

void SyncPipeline::prune_node(uint64_t p_key) {
    for (uint32_t at = bindings.size(); at > 0; --at) {
        PropertyBinding *const held = live(bindings[at - 1]);
        if (binding_nodes[at - 1] == p_key && held->node() == nullptr) {
            drop_declaration(held);
            remove_binding(at - 1);
        }
    }
}

void SyncPipeline::prune_derived() {
    watch_tree();
    for (uint32_t at = prune_candidates.size(); at > 0; --at) {
        const uint64_t key = prune_candidates[at - 1];
        prune_visits += 1;
        repl::BindingBook *book = binding_book();
        const LocalVector<uint64_t> *held
            = book != nullptr ? book->on_node(key) : nullptr;
        Node *node = held != nullptr && !held->is_empty()
            ? live((*held)[0])->node()
            : nullptr;
        if (held != nullptr && node == nullptr) {
            prune_node(key);
        } else if (node != nullptr && !node->is_inside_tree()) {
            continue;
        }
        prune_candidates.remove_at_unordered(at - 1);
    }
}

void SyncPipeline::append_binding(PropertyBinding *p_binding) {
    Node *node = p_binding->node();
    const Ref<NetwEntity> entity = NetwEntity::of(node);
    const uint64_t entity_key
        = entity.is_valid() ? uint64_t(entity->get_instance_id()) : 0;
    const uint64_t node_key
        = node != nullptr ? uint64_t(node->get_instance_id()) : 0;
    bindings.push_back(p_binding->id);
    binding_entities.push_back(entity_key);
    binding_nodes.push_back(node_key);
    if (node == nullptr || !node->is_inside_tree()) {
        prune_candidates.push_back(node_key);
    }
    IndexedBinding entry;
    entry.sequence = binding_sequence++;
    entry.binding = p_binding->id;
    if (LocalVector<IndexedBinding> *held = held_by(entity_key)) {
        held->push_back(entry);
    }
    binding_book()->index_node(node_key, p_binding->id);
    seat_record(p_binding);
    store_admit(p_binding);
}

void SyncPipeline::replace_binding(uint32_t p_at, PropertyBinding *p_binding) {
    const uint64_t previous = bindings[p_at];
    bindings[p_at] = p_binding->id;
    if (LocalVector<IndexedBinding> *held = held_by(binding_entities[p_at])) {
        for (IndexedBinding &entry : *held) {
            if (entry.binding == previous) {
                entry.binding = p_binding->id;
                break;
            }
        }
    }
    binding_book()->reindex_node(binding_nodes[p_at], previous, p_binding->id);
    if (PropertyBinding *was = live(previous)) {
        unseat_record(binding_entities[p_at], was);
    }
    seat_record(p_binding);
    store_admit(p_binding);
    retire(previous);
}

void SyncPipeline::retire(uint64_t p_binding) {
    NetwMultiplayer *plane = core();
    if (plane == nullptr) {
        return;
    }
    plane->store().remove_owner(p_binding);
    plane->property_binding_book().retire(p_binding);
}

void SyncPipeline::remove_binding(uint32_t p_at) {
    const uint64_t previous = bindings[p_at];
    const uint64_t entity_key = binding_entities[p_at];
    const uint64_t node_key = binding_nodes[p_at];
    bindings.remove_at(p_at);
    binding_entities.remove_at(p_at);
    binding_nodes.remove_at(p_at);
    if (LocalVector<IndexedBinding> *held = held_by(entity_key)) {
        for (uint32_t at = 0; at < held->size(); ++at) {
            if ((*held)[at].binding == previous) {
                held->remove_at(at);
                break;
            }
        }
    }
    binding_book()->unindex_node(node_key, previous);
    if (PropertyBinding *was = live(previous)) {
        unseat_record(entity_key, was);
    }
    retire(previous);
}

void SyncPipeline::clear_bindings() {
    NetwMultiplayer *plane = core();
    if (plane != nullptr) {
        plane->store().clear();
        for (const uint64_t held : bindings) {
            plane->property_binding_book().retire(held);
        }
        plane->property_binding_book().clear_nodes();
    }
    for (const uint64_t entity_key : binding_entities) {
        NetwEntity *entity = entity_key != 0
            ? Object::cast_to<NetwEntity>(gd::object_of(ObjectID(entity_key)))
            : nullptr;
        if (entity != nullptr && entity->get_record() != nullptr) {
            entity->get_record()->forget_bindings();
        }
    }
    bindings.clear();
    binding_entities.clear();
    loose_bindings.clear();
    binding_nodes.clear();
    prune_candidates.clear();
}

const LocalVector<uint64_t> *SyncPipeline::node_bindings(
    const Node *p_node
) const {
    repl::BindingBook *book = binding_book();
    if (p_node == nullptr || book == nullptr) {
        return nullptr;
    }
    return book->on_node(uint64_t(p_node->get_instance_id()));
}

uint32_t SyncPipeline::binding_position(
    const PropertyBinding *p_binding
) const {
    for (uint32_t at = 0; at < bindings.size(); ++at) {
        if (bindings[at] == p_binding->id) {
            return at;
        }
    }
    return bindings.size();
}

LocalVector<PropertyBindingPtr> SyncPipeline::bindings_of_entity(
    const Ref<NetwEntity> &p_entity
) const {
    static const LocalVector<IndexedBinding> none;
    const LocalVector<IndexedBinding> *own
        = held_by(uint64_t(p_entity->get_instance_id()));
    const LocalVector<IndexedBinding> &left = own != nullptr ? *own : none;
    const LocalVector<IndexedBinding> &right = loose_bindings;
    LocalVector<PropertyBindingPtr> out;
    out.reserve(left.size() + right.size());
    uint32_t at_left = 0;
    uint32_t at_right = 0;
    while (at_left < left.size() || at_right < right.size()) {
        const bool take_left = at_right >= right.size()
            || (at_left < left.size()
                && left[at_left].sequence < right[at_right].sequence);
        PropertyBinding *const held = take_left
            ? live(left[at_left++].binding)
            : live(right[at_right++].binding);
        if (held != nullptr) {
            out.push_back(held);
        }
    }
    return out;
}

LocalVector<PropertyBindingPtr> SyncPipeline::derived_group(int64_t p_route) {
    if (sync_model == nullptr) {
        return LocalVector<PropertyBindingPtr>();
    }
    return sync_model->route_bindings(p_route, NetwSyncModel::KIND_DERIVED);
}

void SyncPipeline::capture_declaration_key(
    const PropertyBindingPtr &p_binding
) {
    Node *node = p_binding->node();
    if (node == nullptr) {
        return;
    }
    const Ref<NetwEntity> entity = NetwEntity::of(node);
    if (entity.is_null() || entity->get_owner() == nullptr) {
        return;
    }
    p_binding->order_key
        = StringName(String(entity->get_owner()->get_path_to(node)));
    p_binding->comp = entity->comp_of(node);
}

void SyncPipeline::bind_declaration(const PropertyBindingPtr &p_binding) {
    NetwMultiplayer *plane = core();
    Node *node = p_binding->node();
    if (plane == nullptr || node == nullptr || sync_model == nullptr) {
        return;
    }
    const Ref<NetwEntity> entity = NetwEntity::of(node);
    if (entity.is_null()) {
        return;
    }
    if (String(p_binding->order_key).is_empty()) {
        capture_declaration_key(p_binding);
    }
    const RID rid = entity->get_rid_handle();
    int64_t route = plane->get_liveness_core()->route_of(rid);
    if (route <= 0) {
        route = entity->get_route();
    }
    if (route <= 0 || String(p_binding->order_key).is_empty()) {
        return;
    }
    p_binding->route = route;
    sync_model->declare(
        route,
        NetwSyncModel::KIND_DERIVED,
        p_binding->order_key,
        p_binding->comp,
        p_binding->get_set()->get_rid_handle(),
        p_binding->get_set()->get_record(),
        p_binding->get_set()->wire_hash(),
        p_binding->get_set()->get_policy(),
        p_binding->get_set()->get_audience()
    );
    sync_model->attach(
        route,
        NetwSyncModel::KIND_DERIVED,
        p_binding->order_key,
        p_binding->get_set()->get_record(),
        p_binding
    );
    store_place(p_binding);
}

void SyncPipeline::store_admit(const PropertyBindingPtr &p_binding) {
    NetwMultiplayer *plane = core();
    if (plane != nullptr) {
        plane->store_admit(p_binding);
    }
}

void SyncPipeline::store_place(const PropertyBindingPtr &p_binding) {
    NetwMultiplayer *plane = core();
    if (plane != nullptr) {
        plane->store_place(p_binding);
    }
}

void SyncPipeline::drop_declaration(const PropertyBindingPtr &p_binding) {
    if (sync_model != nullptr && p_binding->route > 0
        && !String(p_binding->order_key).is_empty()) {
        sync_model->detach(
            p_binding->route,
            NetwSyncModel::KIND_DERIVED,
            p_binding->order_key,
            p_binding->get_set()->get_record()
        );
        sync_model->drop(
            p_binding->route,
            NetwSyncModel::KIND_DERIVED,
            p_binding->order_key,
            p_binding->get_set()->get_record()
        );
    }
    p_binding->route = 0;
    store_place(p_binding);
}

void SyncPipeline::recapture_entity(const Ref<NetwEntity> &p_entity) {
    if (p_entity.is_null()) {
        return;
    }
    NETW_ZONE_NC("SyncPipeline recapture entity", colors::WIRE);
    const LocalVector<PropertyBindingPtr> candidates
        = entity_bindings_checked(p_entity);
    NETW_ZONE_VALUE(candidates.size());
    for (uint32_t at = 0; at < candidates.size(); ++at) {
        capture_declaration_key(candidates[at]);
        seat_record(candidates[at]);
    }
}

bool SyncPipeline::binding_belongs_to(
    const PropertyBindingPtr &p_binding,
    const Ref<NetwEntity> &p_entity
) const {
    Node *node = p_binding->node();
    if (node == nullptr) {
        return false;
    }
    const Ref<NetwEntity> found = NetwEntity::of(node);
    return found.is_valid()
        && found->get_rid_handle() == p_entity->get_rid_handle();
}

LocalVector<PropertyBindingPtr> SyncPipeline::entity_bindings_checked(
    const Ref<NetwEntity> &p_entity
) const {
    LocalVector<PropertyBindingPtr> matched;
    const LocalVector<PropertyBindingPtr> candidates
        = bindings_of_entity(p_entity);
    for (uint32_t at = 0; at < candidates.size(); ++at) {
        if (binding_belongs_to(candidates[at], p_entity)) {
            matched.push_back(candidates[at]);
        }
    }
    static const bool verify
        = std::getenv("NETW_VERIFY_BINDING_INDEX") != nullptr;
    if (!verify) {
        return matched;
    }
    LocalVector<PropertyBindingPtr> scanned;
    for (uint32_t at = 0; at < bindings.size(); ++at) {
        PropertyBinding *const held = live(bindings[at]);
        if (binding_belongs_to(held, p_entity)) {
            scanned.push_back(held);
        }
    }
    bool same = scanned.size() == matched.size();
    for (uint32_t at = 0; same && at < scanned.size(); ++at) {
        same = scanned[at] == matched[at];
    }
    if (!same) {
        NETW_ERROR(
            sys::WIRE,
            "The property index of '%s' holds %d entries and a full scan "
            "finds %d.",
            String(p_entity->get_name()),
            int(matched.size()),
            int(scanned.size())
        );
        return scanned;
    }
    return matched;
}

void SyncPipeline::on_entity_live(
    int64_t p_route,
    const Ref<NetwEntity> &p_entity
) {
    NETW_ZONE_NC("SyncPipeline on entity live", colors::WIRE);
    if (p_entity.is_valid()) {
        const LocalVector<PropertyBindingPtr> matched
            = entity_bindings_checked(p_entity);
        NETW_ZONE_VALUE(matched.size());
        for (uint32_t at = 0; at < matched.size(); ++at) {
            matched[at]->route = p_route;
            seat_record(matched[at]);
            bind_declaration(matched[at]);
        }
    }
    settle_entity_column(p_route, true);
}

void SyncPipeline::on_route_retired(int64_t p_route) {
    settle_entity_column(p_route, false);
}

void SyncPipeline::settle_entity_column(int64_t p_route, bool p_live) {
    if (!row_sender_armed) {
        return;
    }
    if (p_live) {
        row_sender.entity_bind(p_route);
    } else {
        row_sender.entity_tombstone(p_route);
    }
    const LocalVector<repl::EntityWrite> completed
        = row_sender.entity_release(p_route);
    for (uint32_t at = 0; at < completed.size(); ++at) {
        const repl::EntityWrite &write = completed[at];
        for (uint32_t which = 0; which < bindings.size(); ++which) {
            PropertyBinding *const binding = live(bindings[which]);
            if (binding->get_route() != write.slot.route) {
                continue;
            }
            binding->write_entity_column(
                int64_t(write.slot.column),
                write.route
            );
        }
    }
}

void SyncPipeline::pump(int64_t p_tick) {
    NETW_ZONE_NC("Sync pump", colors::WIRE);
    NETW_ZONE_VALUE(p_tick);
    NetwMultiplayer *plane = core();
    if (plane == nullptr || sync_model == nullptr) {
        return;
    }
    prune_derived();
    plane->sync_pump_rows(
        sync_model,
        row_send(),
        p_tick,
        callable_mp(plane, &NetwMultiplayer::sync_pipeline_bind_declaration),
        tap_seam
    );
    flush_rows();
    flush_row_control();
}

void SyncPipeline::flush_rows() {
    NetwMultiplayer *plane = core();
    if (plane == nullptr) {
        return;
    }
    plane->sync_flush_rows(
        row_send(),
        channel_row,
        channel_row_window,
        channel_row_delta
    );
}

void SyncPipeline::flush_row_control() {
    NetwMultiplayer *plane = core();
    if (plane == nullptr || !row_sender_armed) {
        return;
    }
    plane->row_control_flush(row_send(), channel_row_control);
}

void SyncPipeline::open_carrier_batch(
    int64_t p_peer_id,
    const LocalVector<CarrierRow> *p_rows,
    int64_t p_frames,
    int64_t p_bits
) {
    carrier_peer = p_peer_id;
    carrier_rows = p_rows;
    carrier_frames = p_frames;
    carrier_bits = p_bits;
}

void SyncPipeline::commit_pending_masked(int64_t p_peer_id, int64_t p_seq) {
    const LocalVector<CarrierRow> *rows = carrier_rows;
    if (rows == nullptr || carrier_peer != p_peer_id) {
        return;
    }
    carrier_rows = nullptr;
    if (row_sender_armed) {
        row_sender
            .commit(p_peer_id, p_seq, *rows, carrier_frames, carrier_bits);
    }
}

void SyncPipeline::close_carrier_batch() {
    const LocalVector<CarrierRow> *rows = carrier_rows;
    carrier_rows = nullptr;
    if (rows == nullptr || !row_sender_armed) {
        return;
    }
    row_sender.cancel(carrier_peer, *rows, datagram_tick);
}

void SyncPipeline::note_peer_ack(
    int64_t p_peer_id,
    int64_t p_acked_seq,
    uint32_t p_history
) {
    if (!row_sender_armed) {
        return;
    }
    NetwMultiplayer *plane = core();
    row_sender.acknowledge(
        p_peer_id,
        p_acked_seq,
        p_history,
        plane != nullptr ? plane->link_round_trip_ms(p_peer_id) : 0.0,
        plane != nullptr ? plane->link_jitter_ms(p_peer_id) : 0.0,
        plane != nullptr ? plane->session_elapsed_ms() : 0
    );
}

Dictionary SyncPipeline::resolve_send(Node *p_node) {
    Dictionary out;
    NetwMultiplayer *plane = core();
    const Ref<NetwEntity> entity = NetwEntity::of(p_node);
    if (entity.is_null()) {
        sends_dropped_unroutable += 1;
        return out;
    }
    if (plane == nullptr) {
        return out;
    }
    const int64_t route = plane->liveness_route_of(entity.ptr());
    if (route <= 0) {
        sends_dropped_not_live += 1;
        return out;
    }
    const int64_t comp = entity->comp_of(p_node);
    out[StringName("entity")] = entity;
    out[StringName("route")] = route;
    out[StringName("comp")] = comp;
    out[StringName("path")] = comp == 255
        ? String(entity->relative_path(entity->get_owner(), p_node))
        : String();
    return out;
}

void SyncPipeline::send_entity_event(
    const Dictionary &p_frame,
    int64_t p_channel,
    const PackedByteArray &p_payload,
    bool p_reliable
) {
    NetwMultiplayer *plane = core();
    if (plane == nullptr) {
        return;
    }
    const Ref<NetwEntity> entity = p_frame[StringName("entity")];
    const PackedInt32Array recipients = NetwSyncModel::event_recipients(
        plane->has_server_role(),
        plane->get_unique_id(),
        0,
        plane->rpc_get_recipients(entity)
    );
    for (int at = 0; at < recipients.size(); ++at) {
        plane->send_to(
            recipients[at],
            int64_t(p_frame[StringName("route")]),
            p_channel,
            p_payload,
            p_reliable,
            int64_t(p_frame[StringName("comp")]),
            String(p_frame[StringName("path")]),
            false
        );
    }
}

Array SyncPipeline::gather_stage() {
    Array values;
    if (stage_node != nullptr) {
        values.push_back(stage_node->get(stage_property));
    }
    return values;
}

void SyncPipeline::send_property(Node *p_node, const StringName &p_property) {
    NETW_ZONE_NC("SyncPipeline send property", colors::WIRE);
    NetwMultiplayer *plane = core();
    if (p_node == nullptr || plane == nullptr) {
        return;
    }
    const Dictionary frame = resolve_send(p_node);
    if (frame.is_empty()) {
        if (NetwEntity::of(p_node).is_null()
            && !p_node->has_meta(StringName(UNROUTABLE_META))) {
            p_node->set_meta(StringName(UNROUTABLE_META), true);
            NETW_WARN(
                sys::WIRE,
                "Netw.sync_property got '%s', which is not part of a "
                "NetwEntity. Call Netw.configure_entity on its root.",
                p_node->get_name()
            );
        }
        return;
    }
    if (!gd::has_property(p_node, p_property)) {
        NETW_WARN(
            sys::WIRE,
            "Netw.sync_property got '%s', which '%s' does not have. Check "
            "the property name.",
            p_property,
            p_node->get_name()
        );
        return;
    }
    const Ref<Script> script = p_node->get_script();
    const Ref<NetwPropertyConfig> opt = script.is_valid()
        ? netw::script::model::get_property_config(script, p_property)
        : Ref<NetwPropertyConfig>();
    if (opt.is_null()) {
        const int64_t local_id = plane->get_unique_id();
        if (!plane->is_host()
            && local_id != p_node->get_multiplayer_authority()) {
            NETW_WARN(
                sys::WIRE,
                "Only the authority can sync '%s', because it is not "
                "declared. Declare it with Netw.configure_property.",
                p_property
            );
            return;
        }
    }

    PropertySet declared;
    property_set_builder::from_property_config(declared, p_property, opt);
    const PropertySet *set = &declared;
    if (set->columns.is_empty()) {
        return;
    }

    wire::WriteStream stream;
    const Ref<NetwEntity> entity = frame[StringName("entity")];
    if (!netw::script::model::write_token(
            stream,
            encode_prop_val(entity, p_node, p_property)
        )) {
        return;
    }

    stage_node = p_node;
    stage_property = p_property;
    const Array values = gather_stage();
    stage_node = nullptr;
    if (values.size() != 1) {
        return;
    }
    const PropertyColumn &column = set->columns[0];
    Array quantizers;
    quantizers.push_back(column.get_quantizer());
    Array types;
    types.push_back(
        netw::script::model::get_node_property_type(p_node, p_property)
    );
    if (!call_args::values_write(stream, values, quantizers, types)
        || !stream.align_verify()) {
        return;
    }

    NETW_NETWORK_PROFILE(
        wire::ProfileSubjectScope observation(
            plane->profiler_outgoing_subject(),
            plane->profiler_member(
                int64_t(frame[StringName("route")]),
                set->get_channel(),
                p_node,
                p_property
            )
        )
    );
    send_entity_event(
        frame,
        set->get_channel(),
        stream.to_bytes(),
        set->get_reliable()
    );
}

void SyncPipeline::send_signal(
    Node *p_node,
    const StringName &p_signal,
    const Array &p_args
) {
    NETW_ZONE_NC("SyncPipeline send signal", colors::WIRE);
    NetwMultiplayer *plane = core();
    if (p_node == nullptr || plane == nullptr) {
        return;
    }
    const Dictionary frame = resolve_send(p_node);
    if (frame.is_empty()) {
        if (NetwEntity::of(p_node).is_null()
            && !p_node->has_meta(StringName(UNROUTABLE_META))) {
            p_node->set_meta(StringName(UNROUTABLE_META), true);
            NETW_WARN(
                sys::WIRE,
                "Netw.emit_entity_signal got '%s', which is not part of a "
                "NetwEntity. Call Netw.configure_entity on its root.",
                p_node->get_name()
            );
        }
        return;
    }
    if (!p_node->has_signal(p_signal)) {
        NETW_WARN(
            sys::WIRE,
            "Netw.emit_entity_signal got '%s', which '%s' does not have. "
            "Check the signal name.",
            p_signal,
            p_node->get_name()
        );
        return;
    }

    const Ref<Script> script = p_node->get_script();
    const Ref<NetwMemberConfig> opt = script.is_valid()
        ? netw::script::model::get_signal_config(script, p_signal)
        : Ref<NetwMemberConfig>();
    bool reliable = true;
    bool call_local = true;
    if (opt.is_valid()) {
        reliable
            = opt->get_transfer_mode() == NetwMemberConfig::TRANSFER_RELIABLE;
        call_local = opt->get_is_call_local();
    } else {
        const int64_t local_id = plane->get_unique_id();
        if (!plane->is_host()
            && local_id != p_node->get_multiplayer_authority()) {
            NETW_WARN(
                sys::WIRE,
                "Only the authority can emit '%s', because it is not "
                "declared. Declare it with Netw.configure_signal.",
                p_signal
            );
            return;
        }
    }

    if (call_local) {
        Array call_args;
        call_args.push_back(p_signal);
        call_args.append_array(p_args);
        Callable(p_node, StringName("emit_signal")).callv(call_args);
    }

    wire::WriteStream stream;
    const Ref<NetwEntity> entity = frame[StringName("entity")];
    const Array quantizers = opt.is_valid() ? opt->get_quantizers() : Array();
    const Array types = script.is_valid()
        ? netw::script::model::get_signal_arg_types(script, p_signal)
        : Array();
    if (!netw::script::model::write_token(
            stream,
            encode_signal_val(entity, p_node, p_signal)
        )
        || !netw::call_args::values_write(stream, p_args, quantizers, types)
        || !stream.align_verify()) {
        return;
    }

    NETW_NETWORK_PROFILE(
        wire::ProfileSubjectScope observation(
            plane->profiler_outgoing_subject(),
            plane->profiler_member(
                int64_t(frame[StringName("route")]),
                channel_signal,
                p_node,
                p_signal
            )
        )
    );
    send_entity_event(frame, channel_signal, stream.to_bytes(), reliable);
}

Variant SyncPipeline::encode_prop_val(
    const Ref<NetwEntity> &p_entity,
    Node *p_node,
    const StringName &p_property
) {
    Ref<Script> script;
    if (p_node != nullptr) {
        script = p_node->get_script();
    }
    if (script.is_valid() && p_entity.is_valid()) {
        if (!p_entity->comp_table().get_poisoned()) {
            const int64_t pid
                = netw::script::model::get_property_id(script, p_property);
            if (pid > 0) {
                return pid;
            }
        }
    }
    return p_property;
}

Variant SyncPipeline::encode_signal_val(
    const Ref<NetwEntity> &p_entity,
    Node *p_node,
    const StringName &p_signal
) {
    Ref<Script> script;
    if (p_node != nullptr) {
        script = p_node->get_script();
    }
    if (script.is_valid() && p_entity.is_valid()) {
        if (!p_entity->comp_table().get_poisoned()) {
            const int64_t sid
                = netw::script::model::get_signal_id(script, p_signal);
            if (sid > 0) {
                return sid;
            }
        }
    }
    return p_signal;
}

bool SyncPipeline::accept_unreliable(
    int64_t p_sender,
    int64_t p_route,
    int64_t p_channel,
    int64_t p_seq
) {
    return progress.accept_in_datagram(p_sender, p_route, p_channel, p_seq);
}

void SyncPipeline::reset_stream_of(int p_peer, uint64_t p_token) {
    wire::StreamReaderBook &book = row_send()->reader_book();
    wire::StreamLane named;
    if (!book.names(p_peer, p_token, named)) {
        return;
    }
    wire::ControlRecord answer;
    answer.tag = wire::ControlTag::RESET;
    answer.token = p_token;
    if (!book.invalidate(p_peer, named, answer.request, answer.token)) {
        return;
    }
    row_send()->control_scheduler().drop_token(p_peer, p_token);
    row_send()->control_scheduler().queue(p_peer, answer);
}

void SyncPipeline::reset_stray(int p_peer, uint64_t p_token) {
    NetwMultiplayer *plane = core();
    if (plane == nullptr) {
        return;
    }
    const int64_t interval = wire::repair_interval_ms(
        int64_t(plane->link_round_trip_ms(int64_t(p_peer)))
    );
    if (!row_send()->reader_book().answers_stray(
            p_peer,
            p_token,
            plane->session_elapsed_ms(),
            interval
        )) {
        return;
    }
    wire::ControlRecord answer;
    answer.tag = wire::ControlTag::RESET;
    answer.token = p_token;
    row_send()->control_scheduler().queue(p_peer, answer);
}

void SyncPipeline::open_datagram(int64_t p_base_tick, int64_t p_seq) {
    datagram_tick = p_base_tick;
    datagram_seq = p_seq;
    progress.open_datagram();
}

Dictionary SyncPipeline::gather_payload(
    Node *p_node,
    const PropertySet *p_set
) {
    Dictionary out;
    if (p_node == nullptr || p_set == nullptr) {
        return out;
    }
    for (const PropertyColumn &column : p_set->columns) {
        const StringName &key = column.get_key();
        if (gd::has_property(p_node, key)) {
            out[key] = port_get(p_node, key);
        }
    }
    return out;
}

void SyncPipeline::apply_payload(
    Node *p_node,
    const PropertySet *p_set,
    const Dictionary &p_payload
) {
    if (p_node == nullptr || p_set == nullptr) {
        return;
    }
    for (const PropertyColumn &column : p_set->columns) {
        const StringName &key = column.get_key();
        if (p_payload.has(key) && gd::has_property(p_node, key)) {
            port_set(p_node, key, p_payload[key]);
        }
    }
}

void SyncPipeline::apply_row(
    const Ref<NetwEntity> &p_entity,
    int64_t p_ordinal,
    const PackedByteArray &p_payload,
    int64_t p_sender,
    int64_t p_channel,
    int p_lane
) {
    NetwMultiplayer *plane = core();
    if (plane == nullptr || sync_model == nullptr || p_entity.is_null()) {
        return;
    }
#if defined(NETW_TESTS)
    const ReceiveAllocations counted(plane);
#endif
    const int64_t route = plane->liveness_route_of(p_entity.ptr());
    const PropertyBindingPtr binding = sync_model->admit_row(
        route,
        p_ordinal,
        p_sender,
        p_entity->steering()
    );
    if (binding == nullptr) {
        plane->attribution_note_refusal(
            p_sender,
            p_channel,
            wire::Refusal::UNBOUND
        );
        return;
    }
    wire::StreamReaderBook &book = row_send()->reader_book();
    repl::SnapshotHeader named;
    const bool windowed = p_lane == LANE_WINDOW;
    const bool readable = windowed
        ? repl::name_snapshot_window(p_payload, named)
        : repl::name_snapshot_row(p_payload, named);
    if (!readable) {
        plane->attribution_note_refusal(
            p_sender,
            p_channel,
            wire::Refusal::MALFORMED
        );
        return;
    }
    wire::StreamLane addressed;
    const bool known = book.names(int(p_sender), named.token, addressed);
    const bool names_this_lane = known && addressed.route == route
        && addressed.ordinal == uint8_t(p_ordinal);
    store::World &world = plane->store();
    const store::RowAddress at
        = world.row_of(binding->id, p_lane == LANE_RETAINED ? 1 : 0);
    if (!names_this_lane || !at.valid()) {
        if (!known) {
            reset_stray(int(p_sender), named.token);
        }
        plane->attribution_note_refusal(
            p_sender,
            p_channel,
            wire::Refusal::UNBOUND
        );
        return;
    }
    if (!book.admits_row(int(p_sender), named.token, named.revision)) {
        plane->attribution_note_refusal(
            p_sender,
            p_channel,
            wire::Refusal::STALE
        );
        return;
    }
    uint64_t accepted = 0;
    const wire::CodeRow *baseline = nullptr;
    const store::Landing admission = world.admits(
        at,
        int(p_sender),
        p_payload,
        windowed,
        accepted,
        baseline
    );
    if (admission == store::Landing::DUPLICATE
        || admission == store::Landing::STALE) {
        rows_refused_stale += 1;
        if (admission == store::Landing::DUPLICATE) {
            row_send()->control_scheduler().accept(
                int(p_sender),
                named.token,
                accepted
            );
        }
        plane->attribution_note_refusal(
            p_sender,
            p_channel,
            wire::Refusal::STALE
        );
        return;
    }
    if (admission == store::Landing::BASELINE_UNKNOWN) {
        reset_stream_of(int(p_sender), named.token);
        plane->attribution_note_refusal(
            p_sender,
            p_channel,
            wire::Refusal::BASELINE_UNKNOWN
        );
        return;
    }
    if (admission != store::Landing::ACCEPTED) {
        plane->attribution_note_refusal(
            p_sender,
            p_channel,
            wire::Refusal::MALFORMED
        );
        return;
    }
    const bool retained = p_lane == LANE_RETAINED;
    store::Table &table = world.table(at.table);
    if (!binding->admits_arrival(table.schema, p_ordinal, retained)) {
        plane->attribution_note_refusal(
            p_sender,
            p_channel,
            wire::Refusal::MALFORMED
        );
        return;
    }
    stage_payload = p_payload;
    stage_at = at;
    stage_sender = p_sender;
    stage_windowed = windowed;
    stage_landing = store::Landing::MALFORMED;
    const Error verdict = run_decode_stage(
        p_entity->get_rid_handle(),
        binding->comp,
        p_payload
    );
    const store::Landing landed = stage_landing;
    stage_payload = PackedByteArray();
    stage_at = store::RowAddress();
    if (verdict != OK || landed != store::Landing::ACCEPTED) {
        if (landed == store::Landing::BASELINE_UNKNOWN) {
            row_send()->note_baseline_drop();
        }
        plane->attribution_note_refusal(
            p_sender,
            p_channel,
            landed == store::Landing::BASELINE_UNKNOWN
                ? wire::Refusal::BASELINE_UNKNOWN
                : wire::Refusal::MALFORMED
        );
        return;
    }
    binding
        ->resolve_entity_row(row_send(), p_ordinal, table, at.row, !windowed);
    binding->settle_arrival();
    const uint32_t slot = table.slots[at.row];
    const int64_t predicted = retained ? -1 : world.predict_of(slot);
    NetwPredictionEngine *const predictor = plane->get_prediction_engine();
    const predict::Listener listener = predicted < 0
        ? predict::Listener::NONE
        : predictor->listener_for(predicted, world.ref_of(slot), binding);
    const predict::Arrival arrival = predict::Arrival::of(table, at.row);
    plane->store_received(binding, at, retained, p_lane == LANE_ROW, p_sender);
    if (!retained) {
        binding->notify_landed(table, at.row);
    }
    if (listener != predict::Listener::NONE) {
        NETW_PREDICT_TICK_SCOPE();
        predictor->deliver(predicted, listener, arrival);
    }
    row_send()->control_scheduler().accept(
        int(p_sender),
        named.token,
        named.revision
    );
    sync_model->note_row_applied();
#if defined(NETW_TESTS)
    plane->idle_work.received_rows += 1;
#endif
    feed_derived_interpolation(
        binding,
        world.row_of(binding->id, p_lane == LANE_RETAINED ? 1 : 0)
    );
}

void SyncPipeline::handle_derived_row(
    const Ref<NetwEntity> &p_entity,
    int64_t p_ordinal,
    const PackedByteArray &p_payload,
    int64_t p_sender,
    int64_t p_channel
) {
    apply_row(p_entity, p_ordinal, p_payload, p_sender, p_channel, LANE_ROW);
}

void SyncPipeline::handle_window_row(
    const Ref<NetwEntity> &p_entity,
    int64_t p_ordinal,
    const PackedByteArray &p_payload,
    int64_t p_sender,
    int64_t p_channel
) {
    apply_row(p_entity, p_ordinal, p_payload, p_sender, p_channel, LANE_WINDOW);
}

void SyncPipeline::handle_retained_row(
    const Ref<NetwEntity> &p_entity,
    int64_t p_ordinal,
    const PackedByteArray &p_payload,
    int64_t p_sender,
    int64_t p_channel
) {
    apply_row(
        p_entity,
        p_ordinal,
        p_payload,
        p_sender,
        p_channel,
        LANE_RETAINED
    );
}

void SyncPipeline::feed_derived_interpolation(
    const PropertyBindingPtr &p_binding,
    const store::RowAddress &p_at
) {
    NetwMultiplayer *plane = core();
    if (plane == nullptr) {
        return;
    }
    if (p_binding->get_set()->get_audience() != PropertySet::AUDIENCE_PUBLIC) {
        return;
    }
    Node *node = p_binding->node();
    if (node == nullptr || !p_at.valid()) {
        return;
    }
    const store::Table &table = plane->store().table(p_at.table);
    const bool authoring
        = p_binding->get_set()->get_stamp() != PropertySet::STAMP_NONE
        && table.family != store::Family::RETAINED;
    int64_t tick = table.received_ticks[p_at.row];
    if (!authoring) {
        const ClockEngine &clock = plane->clock_engine();
        tick = clock.get_configured() ? clock.get_tick() : 0;
    }
    const uint64_t owner = p_binding->id;
    const uint32_t lane = table.family == store::Family::RETAINED ? 1 : 0;
    const int columns = table.schema.column_count();
    const uint64_t mask = interpolated_mask(p_binding, node, p_at, table, lane);
    const int64_t route = p_binding->get_route();
    for (int column = 0; column < columns; ++column) {
        if (column < INTERPOLATED_LIMIT && (mask >> column & 1) == 0) {
            continue;
        }
        const StringName &key = table.schema.at(column)->key;
        if (column < INTERPOLATED_LIMIT
            && plane->display_record_followed(
                route,
                key,
                tick,
                authoring,
                owner,
                lane,
                uint32_t(column)
            )) {
            continue;
        }
        const Ref<NetwInterpolate> spec
            = netw::script::model::get_node_property_interpolator(node, key);
        if (spec.is_valid()) {
            plane->display_record(
                node,
                key,
                table.read_value(p_at.row, uint32_t(column)),
                tick,
                spec,
                authoring,
                owner,
                lane,
                uint32_t(column)
            );
        }
    }
}

PackedByteArray SyncPipeline::encode_derived_descriptors(int64_t p_route) {
    LocalVector<spawn::DescriptorRow> rows;
    const LocalVector<repl::SetRow> *found
        = sync_model != nullptr ? sync_model->route_rows(p_route) : nullptr;
    if (found != nullptr) {
        for (const repl::SetRow &row : *found) {
            if (row.kind == NetwSyncModel::KIND_DERIVED) {
                spawn::DescriptorRow entry;
                entry.ordinal = uint64_t(row.ordinal);
                entry.schema_hash = uint64_t(uint32_t(row.schema_hash));
                rows.push_back(entry);
            }
        }
    }
    return spawn::Record::write_descriptors(rows);
}

void SyncPipeline::note_derived_schema(
    int64_t p_route,
    const Dictionary &p_descriptors
) {
    if (sync_model != nullptr) {
        sync_model->note_descriptors(p_route, p_descriptors);
    }
}

void SyncPipeline::clear_session() {
    clear_bindings();
    progress.clear();
    row_sender = ReplicationSend();
    row_sender_armed = false;
    NetwMultiplayer *plane = core();
    if (plane != nullptr) {
        plane->store().clear_peers();
        plane->get_prediction_engine()->release_bindings();
    }
}

void SyncPipeline::clear_route(int64_t p_route) {
    progress.clear_route(p_route);
    NetwMultiplayer *plane = core();
    if (plane != nullptr) {
        plane->carrier_close_route(p_route);
    }
    if (row_sender_armed) {
        row_sender.close_route(p_route);
    }
    for (uint32_t at = 0; at < bindings.size(); ++at) {
        PropertyBinding *const held = live(bindings[at]);
        if (held->route == p_route) {
            held->route = 0;
            store_place(held);
        }
    }
}

void SyncPipeline::clear_peer(int64_t p_peer_id) {
    progress.clear_peer(p_peer_id);
    for (uint32_t at = 0; at < bindings.size(); ++at) {
        live(bindings[at])->clear_peer(p_peer_id);
    }
    if (row_sender_armed) {
        row_sender.forget_peer(p_peer_id);
    }
    NetwMultiplayer *plane = core();
    if (plane != nullptr) {
        plane->store().forget_sender(int(p_peer_id));
    }
}

void SyncPipeline::dispose() {
    hears_entity_live = false;
}

Dictionary SyncPipeline::counters() const {
    NetwMultiplayer *plane = core();
    const Dictionary model_stats
        = sync_model != nullptr ? sync_model->stats() : Dictionary();
    const Dictionary flush_stats
        = plane != nullptr ? plane->sync_flush_stats() : Dictionary();
    Dictionary out;
    out[StringName("sends_dropped_unroutable")] = sends_dropped_unroutable;
    out[StringName("sends_dropped_not_live")] = sends_dropped_not_live;
    out[StringName("sync_drops_stale")]
        = progress.stale_count() + rows_refused_stale;
    out[StringName("row_frames_dropped_baseline")]
        = row_sender_armed ? row_sender.baseline_drops() : int64_t(0);
    out[StringName("derived_sets_active")] = int64_t(bindings.size());
    out[StringName("derived_frames_in")]
        = int64_t(model_stats.get(StringName("rows_in"), 0));
    out[StringName("drops_derived_no_set")]
        = int64_t(model_stats.get(StringName("drops_no_set"), 0));
    out[StringName("drops_derived_bad_sender")]
        = int64_t(model_stats.get(StringName("drops_bad_sender"), 0));
    out[StringName("drops_derived_schema")]
        = int64_t(model_stats.get(StringName("drops_schema"), 0));
    out[StringName("row_frames_out")]
        = int64_t(flush_stats.get(StringName("row_frames_out"), 0));
    out[StringName("row_frames_full")]
        = int64_t(flush_stats.get(StringName("row_frames_full"), 0));
    out[StringName("row_frames_stage_refused")]
        = int64_t(flush_stats.get(StringName("row_frames_stage_refused"), 0));
    out[StringName("row_frames_ungathered")]
        = int64_t(flush_stats.get(StringName("row_frames_ungathered"), 0));
    out[StringName("row_frames_deferred")]
        = int64_t(flush_stats.get(StringName("row_frames_deferred"), 0));
    out[StringName("retained_frames_out")]
        = int64_t(flush_stats.get(StringName("retained_frames_out"), 0));
    out[StringName("window_frames_out")]
        = int64_t(flush_stats.get(StringName("window_frames_out"), 0));
    out[StringName("window_samples_out")]
        = int64_t(flush_stats.get(StringName("window_samples_out"), 0));
    out[StringName("sync_pump_skips_invalid_node")]
        = int64_t(flush_stats.get(StringName("pump_skips_invalid_node"), 0));
    out[StringName("sync_pump_skips_no_entity")]
        = int64_t(flush_stats.get(StringName("pump_skips_no_entity"), 0));
    out[StringName("sync_pump_skips_no_route")]
        = int64_t(flush_stats.get(StringName("pump_skips_no_route"), 0));
    out[StringName("sync_pump_skips_not_author")]
        = int64_t(model_stats.get(StringName("skips_not_author"), 0));
    out[StringName("sync_pump_skips_no_recipients")]
        = int64_t(model_stats.get(StringName("skips_no_recipients"), 0));
    return out;
}

void SyncPipeline::handle_property_sync(
    const Ref<NetwEntity> &p_entity,
    Node *p_comp_node,
    const PackedByteArray &p_payload,
    int64_t p_sender,
    int64_t p_comp
) {
    NetwMultiplayer *plane = core();
    if (plane == nullptr || p_comp_node == nullptr || p_entity.is_null()) {
        return;
    }
    wire::ReadStream reader(p_payload);
    const Ref<Script> script = p_comp_node->get_script();
    Variant token;
    if (!netw::script::model::read_token(reader, token)) {
        return;
    }
    StringName prop;
    if (token.get_type() == Variant::INT) {
        prop = script.is_valid() ? netw::script::model::get_property_name_by_id(
                                       script,
                                       int64_t(token)
                                   )
                                 : StringName();
    } else {
        prop = StringName(token);
    }
    if (String(prop).is_empty()) {
        return;
    }

    NETW_NETWORK_PROFILE(plane->profiler().name_arrival(plane->profiler_member(
        plane->liveness_route_of(p_entity.ptr()),
        channel_property,
        p_comp_node,
        prop
    )));
    const Ref<NetwPropertyConfig> opt = script.is_valid()
        ? netw::script::model::get_property_config(script, prop)
        : Ref<NetwPropertyConfig>();
    PropertySet declared;
    property_set_builder::from_property_config(declared, prop, opt);
    const PropertySet *set = &declared;
    if (set->columns.is_empty()) {
        return;
    }
    Array quantizers;
    quantizers.push_back(set->columns[0].get_quantizer());
    Array types;
    types.push_back(
        netw::script::model::get_node_property_type(p_comp_node, prop)
    );
    Array decoded;
    if (!netw::call_args::values_read(reader, quantizers, types, decoded)
        || !reader.align_verify() || reader.bits_remaining() != 0) {
        return;
    }
    if (decoded.is_empty()) {
        return;
    }
    const Variant value = decoded[0];

    if (p_sender != SERVER_PEER && opt.is_null()) {
        return;
    }
    if (!entity::Control::script_admits(
            p_comp_node,
            prop,
            false,
            p_sender,
            p_entity->steering()
        )) {
        NETW_WARN(
            sys::WIRE,
            "'%s' was synced by peer %d, which may not write it, so it was "
            "dropped. Set NetwMemberConfig.write_policy if that peer should "
            "write it.",
            prop,
            p_sender
        );
        return;
    }

    stage_node = p_comp_node;
    stage_property = prop;
    stage_values = decoded;
    Array staged;
    staged.push_back(value);
    const Error verdict = apply_one_value(staged);
    stage_node = nullptr;
    if (verdict != OK) {
        return;
    }

    const Array interpolators
        = opt.is_valid() ? opt->get_interpolators() : Array();
    if (!interpolators.is_empty()) {
        const Ref<NetwInterpolate> spec = interpolators[0];
        if (spec.is_valid()) {
            plane->display_record(
                p_comp_node,
                prop,
                value,
                receive_tick(),
                spec,
                false
            );
        }
    }

    if (plane->has_server_role()) {
        const int64_t route = plane->liveness_route_of(p_entity.ptr());
        const PackedInt32Array recipients = NetwSyncModel::event_recipients(
            true,
            plane->get_unique_id(),
            p_sender,
            plane->rpc_get_recipients(p_entity)
        );
        for (int at = 0; at < recipients.size(); ++at) {
            plane->send_to(
                recipients[at],
                route,
                channel_property,
                p_payload,
                set->get_reliable(),
                0,
                String(),
                false
            );
        }
    }
}

void SyncPipeline::handle_signal(
    const Ref<NetwEntity> &p_entity,
    Node *p_comp_node,
    const PackedByteArray &p_payload,
    int64_t p_sender
) {
    NetwMultiplayer *plane = core();
    if (plane == nullptr || p_comp_node == nullptr || p_entity.is_null()) {
        return;
    }
    wire::ReadStream reader(p_payload);
    const Ref<Script> script = p_comp_node->get_script();
    Variant token;
    if (!netw::script::model::read_token(reader, token)) {
        return;
    }
    StringName signal_name;
    if (token.get_type() == Variant::INT) {
        signal_name = script.is_valid()
            ? netw::script::model::get_signal_name_by_id(script, int64_t(token))
            : StringName();
    } else {
        signal_name = StringName(token);
    }
    if (String(signal_name).is_empty()) {
        return;
    }

    NETW_NETWORK_PROFILE(plane->profiler().name_arrival(plane->profiler_member(
        plane->liveness_route_of(p_entity.ptr()),
        channel_signal,
        p_comp_node,
        signal_name
    )));
    const Ref<NetwMemberConfig> opt = script.is_valid()
        ? netw::script::model::get_signal_config(script, signal_name)
        : Ref<NetwMemberConfig>();
    bool reliable = true;
    if (opt.is_valid()) {
        reliable
            = opt->get_transfer_mode() == NetwMemberConfig::TRANSFER_RELIABLE;
    }
    const Array quantizers = opt.is_valid() ? opt->get_quantizers() : Array();
    const Array types = script.is_valid()
        ? netw::script::model::get_signal_arg_types(script, signal_name)
        : Array();
    Array args;
    if (!netw::call_args::values_read(reader, quantizers, types, args)
        || !reader.align_verify() || reader.bits_remaining() != 0) {
        return;
    }

    if (p_sender != SERVER_PEER && opt.is_null()) {
        return;
    }
    if (!entity::Control::script_admits(
            p_comp_node,
            signal_name,
            true,
            p_sender,
            p_entity->steering()
        )) {
        NETW_WARN(
            sys::WIRE,
            "'%s' was emitted by peer %d, which may not emit it, so it was "
            "dropped. Set NetwMemberConfig.write_policy if that peer should "
            "emit it.",
            signal_name,
            p_sender
        );
        return;
    }

    record_interpolated_signal_args(p_comp_node, args, opt);

    Array call_args;
    call_args.push_back(signal_name);
    call_args.append_array(args);
    Callable(p_comp_node, StringName("emit_signal")).callv(call_args);

    if (plane->has_server_role()) {
        const int64_t route = plane->liveness_route_of(p_entity.ptr());
        const PackedInt32Array recipients = NetwSyncModel::event_recipients(
            true,
            plane->get_unique_id(),
            p_sender,
            plane->rpc_get_recipients(p_entity)
        );
        for (int at = 0; at < recipients.size(); ++at) {
            plane->send_to(
                recipients[at],
                route,
                channel_signal,
                p_payload,
                reliable,
                0,
                String(),
                false
            );
        }
    }
}

void SyncPipeline::record_interpolated_signal_args(
    Node *p_comp_node,
    const Array &p_args,
    const Ref<NetwMemberConfig> &p_opt
) {
    NetwMultiplayer *plane = core();
    if (plane == nullptr || p_opt.is_null()
        || p_opt->get_interpolators().is_empty()) {
        return;
    }
    const int64_t tick = receive_tick();
    const Array interpolators = p_opt->get_interpolators();
    for (int at = 0; at < interpolators.size(); ++at) {
        const Ref<NetwInterpolate> spec = interpolators[at];
        if (spec.is_null() || String(spec->get_target()).is_empty()
            || at >= p_args.size()) {
            continue;
        }
        plane->display_record(
            p_comp_node,
            spec->get_target(),
            p_args[at],
            tick,
            spec,
            false
        );
    }
}

Error SyncPipeline::apply_one_value(const Array &p_values) {
    if (p_values.size() != 1 || stage_node == nullptr) {
        return ERR_INVALID_DATA;
    }
    stage_node->set(stage_property, p_values[0]);
    return OK;
}

int64_t SyncPipeline::receive_tick() const {
    NetwMultiplayer *plane = core();
    if (plane == nullptr) {
        return 0;
    }
    const ClockEngine &clock = plane->clock_engine();
    if (clock.get_configured()) {
        return clock.get_tick();
    }
    return plane->get_frame_counter();
}

} // namespace netw
