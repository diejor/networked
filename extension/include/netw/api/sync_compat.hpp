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
#include "netw/repl/watch_book.hpp"
#include "netw/spawn/book.hpp"
#include "netw/staged_writes.hpp"

namespace netw {

class NetwMultiplayer;

class SyncCompat {
    friend class NetwMultiplayer;

public:
    static const int WATCH_LIMIT = 64;

private:
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
    repl::WatchBook watch_book;
    godot::HashMap<int64_t, godot::Dictionary> pending_schema;
    int64_t next_row_id = 1;

    godot::Node *stage_root = nullptr;
    const godot::LocalVector<godot::NodePath> *stage_paths = nullptr;
    bool stage_allow_missing = false;
    godot::Array stage_readable;
    godot::PackedByteArray stage_payload;
    godot::Array stage_keys;
    bool stage_retained = false;
    StagedWrites stage_decoded;

    int64_t sync_frames_out = 0;
    int64_t sync_frames_in = 0;
    int64_t delta_frames_out = 0;
    int64_t delta_frames_in = 0;
    int64_t drops_sync_no_set = 0;
    int64_t drops_sync_bad_sender = 0;
    int64_t drops_sync_poisoned = 0;
    int64_t drops_sync_unknown_flag = 0;

    NetwMultiplayer *core() const;
    Consumed *row_of(godot::Object *p_sync) const;
    Consumed *row_by_ordinal(int64_t p_route, int64_t p_ordinal) const;
    void drop_row(uint32_t p_at);

    void refresh_row(Consumed *p_row);
    void disconnect_config(Consumed *p_row);
    bool config_still_held(
        const godot::ObjectID &p_config,
        Consumed *p_except
    ) const;
    static int64_t schema_fingerprint(const Consumed *p_row);
    void poison(
        Consumed *p_row,
        int64_t p_route,
        const godot::String &p_reason
    );
    void validate_schema(Consumed *p_row, int64_t p_route);

    int64_t ordinal_of(const Consumed *p_row) const;
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

    godot::Array gather_stage();
    godot::Error apply_stage(const godot::Array &p_staged);
    godot::Error decode_stage_body();

    godot::Array gather_paths(
        Consumed *p_row,
        const godot::LocalVector<godot::NodePath> &p_paths,
        bool p_allow_missing,
        godot::Array &r_readable
    );
    godot::Error apply_paths(
        Consumed *p_row,
        const godot::LocalVector<godot::NodePath> &p_paths,
        const godot::Array &p_values
    );

    void feed_interpolation(Consumed *p_row);
    void build_feed(Consumed *p_row, godot::MultiplayerSynchronizer *p_sync);

    void on_config_changed();
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
};

} // namespace netw
