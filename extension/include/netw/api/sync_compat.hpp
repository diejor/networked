#pragma once

#include <cstdint>

#include "godot/callable.hpp"
#include "godot/local_vector.hpp"
#include "godot/multiplayer_synchronizer.hpp"
#include "godot/node.hpp"
#include "godot/object.hpp"
#include "godot/rid.hpp"
#include "godot/templates.hpp"
#include "godot/variant.hpp"
#include "netw/api/entity.hpp"
#include "netw/api/interpolate.hpp"
#include "netw/api/sync_model.hpp"
#include "netw/held_bytes.hpp"
#include "netw/repl/watch_book.hpp"
#include "netw/spawn/book.hpp"
#include "netw/sync_kernel.hpp"

namespace netw {

class NetwMultiplayer;

class SyncCompat {
    friend class NetwMultiplayer;

public:
    static const int WATCH_LIMIT = 64;

    int64_t held_bytes() const {
        int64_t bytes = held::vector(rows) + held::map(rows_by_sync)
            + held::map_of_vectors(rows_by_root)
            + held::map_of_vectors(rows_by_route) + held::map(config_holds)
            + held::vector(pump_pass) + held::map(pending_schema)
            + held::map(fields);
        for (const Consumed *row : rows) {
            bytes += int64_t(sizeof(Consumed)) + held::vector(row->sync_paths)
                + held::vector(row->watch_paths)
                + held::vector(row->sync_fields)
                + held::vector(row->watch_fields)
                + held::map(row->intent_by_peer) + held::vector(row->feed_nodes)
                + held::vector(row->feed_props)
                + held::vector(row->feed_scripts)
                + held::vector(row->feed_live);
        }
        return bytes;
    }

private:
    bool hears_entity_live = false;

    struct Field {
        godot::NodePath target;
        godot::NodePath property;
    };

    struct Consumed {
        int64_t id = 0;
        godot::ObjectID sync;
        godot::ObjectID root;
        godot::ObjectID config;
        bool config_dirty = true;
        godot::RID entity;
        int64_t route = 0;
        int64_t comp = 0;
        godot::StringName order_key;
        godot::LocalVector<godot::NodePath> sync_paths;
        godot::LocalVector<godot::NodePath> watch_paths;
        godot::LocalVector<const Field *> sync_fields;
        godot::LocalVector<const Field *> watch_fields;
        godot::HashMap<int64_t, bool> intent_by_peer;
        int64_t schema_hash = 0;
        bool poisoned = false;
        bool schema_checked = false;
        bool adopt_attempted = false;
        int64_t last_sync_usec = -1;
        int64_t last_watch_usec = -1;
        godot::LocalVector<godot::ObjectID> feed_nodes;
        godot::LocalVector<godot::StringName> feed_props;
        bool feed_built = false;
        godot::LocalVector<uint64_t> feed_scripts;
        godot::LocalVector<uint8_t> feed_live;
        uint64_t feed_epoch = UINT64_MAX;
        bool feed_any = false;
        godot::MultiplayerSynchronizer *sync_at = nullptr;
        godot::Node *root_at = nullptr;
        godot::Ref<NetwEntity> entity_at;
        bool resident = false;
        int64_t ordinal = -1;
        uint64_t ordinal_revision = UINT64_MAX;

        godot::MultiplayerSynchronizer *sync_node() const;
        godot::Node *root_node() const;
    };

    godot::ObjectID core_id;
    NetwSyncModel *sync_model = nullptr;
    spawn::Book *spawn_book;
    godot::Callable encode_stage;
    godot::Callable decode_stage;
    godot::Callable adopt_seam;
    godot::Callable sweep_seam;
    int64_t channel_sync = 0;
    int64_t channel_sync_delta = 0;

    godot::LocalVector<Consumed *> rows;
    godot::HashMap<uint64_t, Consumed *> rows_by_sync;
    godot::HashMap<uint64_t, godot::LocalVector<Consumed *>> rows_by_root;
    godot::HashMap<int64_t, godot::LocalVector<Consumed *>> rows_by_route;
    godot::HashMap<uint64_t, int> config_holds;
    int64_t rows_examined = 0;
    bool tree_watched = false;
    int64_t pump_local_id = 0;
    bool pump_server = false;
    bool pump_encodes = false;
    int64_t stage_calls = 0;
    int64_t feed_lookups = 0;
    godot::PackedInt32Array pump_peers;
    godot::LocalVector<Consumed *> pump_pass;
    repl::WatchBook watch_book;
    godot::HashMap<int64_t, godot::Dictionary> pending_schema;
    int64_t next_row_id = 1;

    godot::HashMap<godot::String, Field> fields;
    int64_t fields_resolved = 0;
    godot::Array gathered;
    godot::Array gathered_readable;
    godot::PackedByteArray stage_payload;
    int64_t stage_columns = 0;
    bool stage_retained = false;
    bool stage_valid = false;
    sync_kernel::DecodedRow stage_decoded;

    int64_t sync_frames_out = 0;
    int64_t sync_frames_in = 0;
    int64_t delta_frames_out = 0;
    int64_t delta_frames_in = 0;
    int64_t drops_sync_no_set = 0;
    int64_t drops_sync_bad_sender = 0;
    int64_t drops_sync_poisoned = 0;
    int64_t drops_sync_unknown_flag = 0;

    NetwMultiplayer *core() const;
    Consumed *row_of(godot::Object *p_sync);
    Consumed *row_by_ordinal(int64_t p_route, int64_t p_ordinal);
    void index_row(Consumed *p_row);
    void set_route(Consumed *p_row, int64_t p_route);
    void rows_under(
        godot::Node *p_owner,
        godot::LocalVector<Consumed *> &r_rows
    );
    void drop_row(Consumed *p_row);
    void watch_tree();
    void forget_nodes(Consumed *p_row);
    bool locate(Consumed *p_row);
    godot::Ref<NetwEntity> entity_of(Consumed *p_row);

    void refresh_row(Consumed *p_row);
    void disconnect_config(Consumed *p_row);
    static int64_t schema_fingerprint(const Consumed *p_row);
    void poison(
        Consumed *p_row,
        int64_t p_route,
        const godot::String &p_reason
    );
    void validate_schema(Consumed *p_row, int64_t p_route);

    int64_t ordinal_of(Consumed *p_row);
    void capture_declaration(Consumed *p_row);
    void declare_model_row(const Consumed *p_row);
    void drop_declaration(Consumed *p_row);

    void refresh_row_intent(Consumed *p_row);
    void refresh_interest_intent(godot::Node *p_root);
    void prune();

    godot::PackedInt32Array recipients_for(
        Consumed *p_row,
        const godot::Ref<NetwEntity> &p_entity,
        int64_t p_route,
        godot::Node *p_root
    );

    godot::PackedByteArray encode_sync_frame(Consumed *p_row, int64_t p_route);
    void poll_watchers(Consumed *p_row);
    void send_deltas(
        Consumed *p_row,
        int64_t p_route,
        const godot::PackedInt32Array &p_recipients
    );
    godot::PackedByteArray run_encode_stage(
        int64_t p_peer,
        const godot::PackedByteArray &p_stock
    );
    godot::Error run_decode_stage(
        const godot::RID &p_entity,
        int64_t p_comp,
        int64_t p_flags,
        const godot::PackedByteArray &p_payload
    );

    godot::Error decode_stage_body();

    const Field *field_of(const godot::NodePath &p_path);
    void resolve_fields(
        const godot::LocalVector<godot::NodePath> &p_paths,
        godot::LocalVector<const Field *> &r_fields
    );
    bool gather(
        Consumed *p_row,
        const godot::LocalVector<const Field *> &p_fields,
        bool p_allow_missing
    );
    void apply(
        Consumed *p_row,
        const godot::LocalVector<const Field *> &p_fields,
        const godot::Array &p_values
    );

    void feed_interpolation(Consumed *p_row);
    void refresh_feed(Consumed *p_row);
    void build_feed(Consumed *p_row, godot::MultiplayerSynchronizer *p_sync);

    void on_config_changed();
    void on_node_removed(godot::Node *p_node);
    void on_sync_visibility_changed(int64_t p_peer, godot::Node *p_root);
    void on_entity_live(
        int64_t p_route,
        const godot::Ref<NetwEntity> &p_entity
    );

public:
    ~SyncCompat();

    void set_core(godot::Object *p_core);
    void set_sync_model(NetwSyncModel *p_model);
    void set_spawn_book(spawn::Book *p_book);
    void set_stage_seams(
        const godot::Callable &p_encode,
        const godot::Callable &p_decode
    );
    void set_spawn_seams(
        const godot::Callable &p_adopt,
        const godot::Callable &p_sweep
    );
    void set_channels(int64_t p_sync, int64_t p_sync_delta);

    godot::Error consume(godot::Node *p_root, godot::Object *p_sync);
    godot::Error consume_remove(godot::Node *p_root, godot::Object *p_sync);

    void refresh_interest_intents();
    int64_t local_peer_id();
    bool synchronizer_verdict(int64_t p_peer_id, godot::Node *p_node);

    void pump();

    void handle_sync(
        const godot::Ref<NetwEntity> &p_entity,
        const godot::PackedByteArray &p_payload,
        int64_t p_sender
    );
    void handle_sync_delta(
        const godot::Ref<NetwEntity> &p_entity,
        const godot::PackedByteArray &p_payload,
        int64_t p_sender
    );

    godot::PackedByteArray encode_descriptors(int64_t p_route);
    void note_schema(int64_t p_route, const godot::Dictionary &p_descriptors);

    void clear_session();
    void clear_route(int64_t p_route);
    void clear_peer(int64_t p_peer_id);
    void dispose();

    godot::Dictionary counters() const;
    int64_t get_rows_examined() const {
        return rows_examined;
    }
    int64_t get_fields_resolved() const {
        return fields_resolved;
    }
    int64_t get_stage_calls() const {
        return stage_calls;
    }
    int64_t get_feed_lookups() const {
        return feed_lookups;
    }
};

} // namespace netw
