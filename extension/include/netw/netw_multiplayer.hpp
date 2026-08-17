#pragma once

#include "godot/callable.hpp"
#include "godot/gdvirtual.hpp"
#include "godot/local_vector.hpp"
#include "godot/multiplayer.hpp"
#include "godot/node.hpp"
#include "godot/packed_scene.hpp"
#include "godot/ref_counted.hpp"
#include "godot/variant.hpp"
#include "godot/multiplayer.hpp"
#include "netw/carrier_buffers.hpp"
#include "netw/channel_book.hpp"
#include "netw/carrier_frame.hpp"
#include "netw/clock_core.hpp"
#include "netw/datagram_seq_book.hpp"
#include "netw/effect_ledger.hpp"
#include "netw/entity_record.hpp"
#include "netw/event_plane.hpp"
#include "godot/script.hpp"
#include "netw/gate_verdict_book.hpp"
#include "netw/handle_ledger.hpp"
#include "netw/interest_engine.hpp"
#include "netw/liveness_core.hpp"
#include "netw/display_book.hpp"
#include "netw/scene_core.hpp"
#include "netw/quantize.hpp"
#include "netw/rate_window.hpp"
#include "netw/session_core.hpp"
#include "netw/settle_queue.hpp"
#include "netw/table/schema_core.hpp"
#include "netw/table/table_core.hpp"
#include "netw/wire/registry.hpp"

namespace netw {

using namespace godot;

// The session, and the owner of one core per plane for the session's whole
// life. A plane's interface reads its core from here rather than constructing
// one, so an interface and the session it belongs to cannot come to disagree
// about which records are live.
//
// It is a multiplayer API so that it can eventually be installed as one. The
// bands that reach a node, a script or the wire are not here yet, and the
// verbs carrying them refuse rather than answer: an API that silently accepted
// an rpc or a configuration would drop it, and a dropped registration reads
// exactly like one that was never made.
class NetwMultiplayerCore : public MultiplayerApiBase {
    GDCLASS(NetwMultiplayerCore, MultiplayerApiBase)

public:
    // Local configuration rather than shape, so a param never enters the wire
    // hash and a later value is not a wire event.
    enum TableParam {
        TABLE_PARAM_RELIABLE = 0,
    };

private:
    Ref<MultiplayerPeer> peer;

    Ref<godot::SceneMultiplayer> inner;

    PackedInt32Array peer_ids;
    Ref<NetwLivenessCore> liveness_core;
    Ref<NetwSessionCore> session_core;
    Ref<NetwClockCore> clock_core;
    Ref<NetwSceneCore> scene_core;
    Ref<NetwDisplayBook> display_book;
    Ref<NetwRateWindow> scene_request_window;
    Ref<NetwInterestEngine> interest_engine;

    int64_t sent_packets = 0;
    int64_t sent_bytes = 0;
    int64_t received_packets = 0;
    int64_t received_bytes = 0;
    int64_t state_acks_out = 0;
    int64_t state_acks_in = 0;
    int64_t standalone_acks_out = 0;
    int64_t frame_counter = 0;
    int64_t last_poll_usec = 0;
    DatagramSeqBook seq_book;
    Ref<NetwCarrierBuffers> carrier;
    Ref<NetwChannelBook> channel_book;
    wire::WireRegistry channels = wire::WireRegistry::create_default();
    // The channel ids the admission gates compare an inbound frame against,
    // resolved once from the declarations rather than written a second time as
    // constants here. A gate runs per frame and a name lookup does not.
    struct GateChannels {
        uint8_t spawn = 0;
        uint8_t despawn = 0;
        uint8_t reparent = 0;
        uint8_t table = 0;
    } gate_channels;
    // The control channels whose whole handling is an announcement, resolved
    // the same way and for the same reason as the gates above.
    struct ControlChannels {
        uint8_t kicked = 0;
        uint8_t shutdown = 0;
        uint8_t kick_request = 0;
        uint8_t leave_request = 0;
        uint8_t pause = 0;
        uint8_t unpause = 0;
    } control_channels;
    GateVerdictBook verdict_book;
    SettleQueue settle_queue;
    Ref<NetwHandleLedger> schemas;
    Ref<SchemaCore> schema_core;
    Ref<NetwHandleLedger> tables;
    Ref<TableCore> table_core;
    Ref<NetwEffectLedger> effects;
    EventPlane plane;
    HashSet<StringName> misused_seams;
    HashMap<StringName, bool> seam_overrides;
    ObjectID seam_script;
    // The two indexes a table binding needs and the store does not: a schema
    // name back to the one table bound to it, and a table back to the schema
    // handle it binds. Keyed by RID id rather than by RID, which is the one
    // spelling both tiers hash.
    HashMap<StringName, RID> table_by_name;
    HashMap<int64_t, RID> table_schema;

    /* One service and the script that names its type, held by id rather than
     * by reference: a service is a node the game owns, and a registry that
     * kept it alive would outlive the scene that made it.
     *
     * A vector rather than a map because `service_all` answers in registration
     * order, and one service is one row, so the order IS the storage.
     */
    struct ServiceRow {
        godot::ObjectID type;
        godot::ObjectID service;
    };
    godot::LocalVector<ServiceRow> services;

    /* The handle-to-wrapper index, live and retired, keyed by handle id.
     *
     * THIS IS THE ONE PLACE AN ENTITY WRAPPER IS HELD. A wrapper is reference
     * counted and nothing else keeps it alive, so the index holds a reference
     * rather than an id: an id-only index frees the wrapper the moment the
     * caller that made it lets go, and every later resolve answers a dead
     * object. It is the index's grip that gives a route a wrapper for as long
     * as the route names one.
     *
     * Nothing here reads the wrapper's behaviour, only its identity, which is
     * what lets the index cross while the wrapper itself is still GDScript.
     *
     * A death moves a row from live to retired rather than dropping it, so an
     * engine's next delta still resolves the entity it is naming. The retired
     * side clears one full cycle later, which is the moment the last grip goes.
     */
    godot::HashMap<int64_t, godot::Ref<godot::RefCounted>> live_wrappers;
    godot::HashMap<int64_t, godot::Ref<godot::RefCounted>> retired_wrappers;

    // The record beside each wrapper. Already a native class, so this is the
    // state the session can read for itself rather than through the wrapper it
    // deliberately does not ask about behaviour.
    godot::HashMap<int64_t, godot::Ref<NetwEntityRecord>> wrapper_records;

    /* The root node each handle names, and the way back.
     *
     * A node is a game object, so THIS one is held by id: the tree owns it and
     * an index that kept it alive would outlive the scene. The reverse map is
     * what lets an ancestor walk answer in handles, since the walk finds a
     * wrapper in node metadata and the caller asked about a record.
     */
    godot::HashMap<int64_t, godot::ObjectID> wrapper_owners;
    godot::HashMap<uint64_t, int64_t> handle_by_wrapper;
    godot::Callable spawn_state;
    godot::ObjectID replication;

    godot::Ref<godot::RefCounted> local_player;
    int64_t local_player_id = 0;

    /* One roster row a peer, held for the same reason the wrapper index is:
     * a participant is reference counted and this is what keeps it alive, so
     * two asks for one peer answer the same object rather than two.
     *
     * The row is minted by the caller and adopted here, because minting one
     * needs the session's public face, which is still GDScript.
     *
     * The seat rides the same row as the object it belongs to, so forgetting a
     * peer cannot leave a membership behind for the next peer to inherit.
     */
    struct ParticipantRow {
        godot::Ref<godot::RefCounted> row;
        godot::RID seat;
    };
    godot::HashMap<int64_t, ParticipantRow> participants;

    // The row p_type names, or -1. A freed service leaves a row that resolves
    // to nothing, which reads as absent rather than as a stale hit.
    int service_row(godot::Object *p_type) const;

    /* Connects p_source's p_signal to this session's signal of the same name.
     *
     * One name for both ends, so a fact cannot be called one thing on the core
     * that announces it and another on the session that publishes it. p_arity
     * is how many arguments the signal carries, which is what selects the
     * relay, and an unknown arity connects nothing.
     */
    void relay_from(
        godot::Object *p_source,
        const godot::StringName &p_signal,
        int p_arity
    );
    void stop_relay_from(
        godot::Object *p_source,
        const godot::StringName &p_signal,
        int p_arity
    );
    godot::Callable relay_for(const godot::StringName &p_signal, int p_arity);

    /* The renaming form, for the one case where the two ends genuinely differ:
     * a participant's own `scene_changed` is the LOCAL scene change once the
     * session has decided which participant is this peer.
     */
    void relay_named_from(
        godot::Object *p_source,
        const godot::StringName &p_source_signal,
        const godot::StringName &p_own_signal,
        int p_arity
    );
    void stop_relay_named_from(
        godot::Object *p_source,
        const godot::StringName &p_source_signal,
        const godot::StringName &p_own_signal,
        int p_arity
    );

    // The participant this peer is, while it holds one. Its scene change is
    // the session's local scene change, and nothing else's is.
    godot::Ref<godot::RefCounted> local_participant;
    void bind_local_participant(const godot::Ref<godot::RefCounted> &p_row);

    // The id p_name is declared under, or zero when nothing declares it. Zero
    // is a reserved id no frame carries, so an undeclared name refuses rather
    // than colliding with channel 0.
    uint8_t declared_channel(const char *p_name) const;

    // The relays, one per arity. A consumer holds the session rather than the
    // core that announced the fact, so every core signal arrives here too. The
    // signal name is last because `Callable::bind` appends what it binds.
    void relay_bare(const godot::StringName &p_signal);
    void relay_one(
        const godot::Variant &p_first,
        const godot::StringName &p_signal
    );
    void relay_two(
        const godot::Variant &p_first,
        const godot::Variant &p_second,
        const godot::StringName &p_signal
    );

    // The one peer edge that is a verdict as well as an announcement, so it
    // records before it republishes: a sink that refused the peer reads the
    // refusal in the same order the session decided it.
    void relay_auth_failed(int64_t p_peer);

protected:
    static void _bind_methods();

public:
    NetwMultiplayerCore();
    ~NetwMultiplayerCore() override;

    Error NETW_API_VIRTUAL(poll)() override;
    void NETW_API_VIRTUAL(set_multiplayer_peer)(
        const Ref<MultiplayerPeer> &p_peer
    ) override;
    Ref<MultiplayerPeer> NETW_API_VIRTUAL(get_multiplayer_peer)() override;
    int32_t NETW_API_VIRTUAL(get_unique_id)() NETW_API_CONST override;
    PackedInt32Array NETW_API_VIRTUAL(get_peer_ids)() NETW_API_CONST override;

    // TODO: answer these once the bands that own them cross. The rpc verb
    // waits on the entity plane, which is what decides whether a call is
    // route-addressed, and the configuration verbs wait on the declaration
    // registries, which are still GDScript.
    int32_t NETW_API_VIRTUAL(get_remote_sender_id)() NETW_API_CONST override;
    Error NETW_API_VIRTUAL(object_configuration_add)(
        Object *p_object,
        NETW_API_CONFIG_ARG p_configuration
    ) override;
    Error NETW_API_VIRTUAL(object_configuration_remove)(
        Object *p_object,
        NETW_API_CONFIG_ARG p_configuration
    ) override;

#if defined(NETW_MODULE)
    Error rpcp(
        Object *p_object,
        int p_peer_id,
        const StringName &p_method,
        const Variant **p_args,
        int p_argcount
    ) override;
#else
    Error _rpc(
        int32_t p_peer_id,
        Object *p_object,
        const StringName &p_method,
        const Array &p_args
    ) override;
#endif

    void set_peer_ids(const PackedInt32Array &p_peer_ids);

    Ref<NetwLivenessCore> get_liveness_core() const;
    Ref<NetwSessionCore> get_session_core() const;
    Ref<NetwClockCore> get_clock_core() const;
    Ref<NetwSceneCore> get_scene_core() const;
    Ref<NetwDisplayBook> get_display_book() const;
    Ref<NetwInterestEngine> get_interest_engine() const;
    Ref<NetwChannelBook> get_channel_book() const;

    void reset_interest();

    void interest_sync_record(godot::Object *p_wrapper);

    NetwSessionCore::State get_state() const;
    NetwSessionCore::Role get_role() const;
    bool is_online() const;
    bool is_host() const;
    bool is_local_client() const;

    void count_sent(int64_t p_bytes);
    void count_received(int64_t p_bytes);
    void count_state_ack_out();
    void count_state_ack_in();
    void count_standalone_ack_out();

    int64_t get_sent_packets() const;
    int64_t get_sent_bytes() const;
    int64_t get_received_packets() const;
    int64_t get_received_bytes() const;
    int64_t get_state_acks_out() const;
    int64_t get_state_acks_in() const;
    int64_t get_standalone_acks_out() const;

    double poll_delta(int64_t p_now_usec);

    void advance_frame();
    int64_t get_frame_counter() const;

    void set_inner(const Ref<godot::SceneMultiplayer> &p_inner);
    Ref<godot::SceneMultiplayer> get_inner() const { return inner; }

    int64_t datagram_budget() const;

    bool channel_aggregates(int64_t p_channel, bool p_requested) const;

    int64_t send_datagram(
        int64_t p_peer,
        const godot::PackedByteArray &p_payload,
        bool p_reliable
    );

    godot::Ref<NetwCarrierDatagram> frame_datagram(
        int64_t p_peer,
        const godot::PackedByteArray &p_payload,
        bool p_reliable
    );

    int64_t carrier_append(
        int64_t p_peer,
        const godot::PackedByteArray &p_frame,
        bool p_reliable
    );

    godot::PackedInt64Array carrier_flush();

    void carrier_clear();
    int64_t carrier_pending(int64_t p_peer, bool p_reliable) const;

    static bool seq_is_fresher(int64_t a, int64_t b);
    int64_t next_send_seq(int64_t p_peer);
    bool has_inbound_seq(int64_t p_peer) const;
    int64_t inbound_seq(int64_t p_peer) const;
    bool note_inbound_seq(int64_t p_peer, int64_t p_seq);
    bool note_peer_ack(int64_t p_peer, int64_t p_ack);
    int64_t peer_ack(int64_t p_peer) const;
    void note_echoed_seq(int64_t p_peer, int64_t p_seq);
    PackedInt64Array peers_owed_echo() const;
    void forget_peer_seqs(int64_t p_peer);
    void clear_seq_books();

    static bool counts_verdict(int64_t p_verdict);
    bool count_verdict(int64_t p_verdict, int64_t p_route = 0);
    int64_t verdict_total(int64_t p_verdict) const;
    bool claim_verdict_warning(int64_t p_verdict, int64_t p_route);
    int64_t sink_verdict(int64_t p_verdict, int64_t p_route);
    int64_t stage_verdict(
        int64_t p_stage,
        int64_t p_verdict,
        int64_t p_route
    );
    void clear_verdicts();

    void settle_schedule(const Callable &p_fn, const StringName &p_key);
    void settle_schedule_after(
        const Callable &p_fn,
        const StringName &p_key,
        int p_pumps
    );
    void settle_advance();
    void settle_cancel(const StringName &p_key);
    PackedStringArray settle_drain();
    void settle_clear();
    int settle_pending() const;
    bool settle_has_key(const StringName &p_key) const;
    static int settle_max_passes();

    Ref<SchemaCore> get_schema_core() const;

    RID schema_create(const StringName &p_name);
    int schema_add_column(
        const RID &p_schema,
        const StringName &p_key,
        int p_type,
        int p_stride
    );
    void schema_set_column_quantizer(
        const RID &p_schema,
        int p_column,
        const Ref<NetwQuantize> &p_quantizer
    );
    Error schema_seal(const RID &p_schema);
    RID schema_find(const StringName &p_name) const;
    int schema_get_hash(const RID &p_schema) const;
    int schema_get_column_count(const RID &p_schema) const;
    StringName schema_get_column_key(const RID &p_schema, int p_column) const;
    int schema_get_column_type(const RID &p_schema, int p_column) const;
    int schema_get_column_stride(const RID &p_schema, int p_column) const;

    Ref<TableCore> get_table_core() const;

    RID table_create(const RID &p_schema);
    RID table_get_schema(const RID &p_table) const;
    void table_set_param(const RID &p_table, int p_param, const Variant &p_value);
    RID table_find(const StringName &p_name) const;
    int table_get_wire_hash(const RID &p_table) const;
    Error table_write_routes(const RID &p_table, const PackedInt64Array &p_routes);
    Error table_write_column(const RID &p_table, int p_column, const Variant &p_data);
    Error table_commit(const RID &p_table);
    PackedInt64Array table_read_routes(const RID &p_table) const;
    Variant table_read_column(const RID &p_table, int p_column) const;
    PackedInt64Array table_read_births(const RID &p_table) const;
    PackedInt64Array table_read_deaths(const RID &p_table) const;
    int table_get_row(const RID &p_table, int64_t p_route) const;
    PackedInt32Array table_get_rows(
        const RID &p_table,
        const PackedInt64Array &p_routes
    ) const;
    int64_t table_get_tick(const RID &p_table) const;

    void table_publish_intake();

    void table_publish(const RID &p_table);

    bool session_publish_control(
        int64_t p_channel,
        int64_t p_sender,
        const godot::PackedByteArray &p_payload
    );

    godot::Ref<NetwCarrierFrame> receive_header(
        int64_t p_peer,
        const godot::PackedByteArray &p_packet
    );

    // How long an armed effect waits for an answer before its own denial. Long
    // enough that a round trip under ordinary loss still resolves the act.
    static constexpr int EFFECT_TIMEOUT_TICKS = 120;

    Ref<NetwEffectLedger> get_effect_ledger() const;

    void effect_arm(
        const StringName &p_key,
        const Callable &p_revert,
        int p_timeout_ticks
    );
    bool effect_watch(
        const StringName &p_key,
        const Callable &p_confirmed,
        const Callable &p_denied
    );
    void effect_adopt(const StringName &p_key);
    void effect_discard(const StringName &p_key);
    bool effect_pending(const StringName &p_key) const;
    int64_t effect_count() const;
    void effect_sweep(int64_t p_tick);

    int64_t event_watch(
        const PackedInt64Array &p_events,
        const Dictionary &p_target,
        const Dictionary &p_predicate,
        const Callable &p_sink,
        const Dictionary &p_opts
    );
    bool event_unwatch(int64_t p_id);
    Array event_watches() const;
    Array event_ring(int64_t p_route);
    void event_ring_clear(int64_t p_route);
    void event_arm(bool p_enabled);
    bool event_wants(int64_t p_event, int64_t p_route) const;

    void event_emit(
        int64_t p_event,
        int64_t p_route,
        const Dictionary &p_detail,
        const StringName &p_entity_id,
        int64_t p_peer,
        int64_t p_verdict,
        const Dictionary &p_model
    );

    Error spawn_admit_frame_default(
        int64_t p_sender,
        int64_t p_route,
        int64_t p_channel,
        const PackedByteArray &p_payload
    );

    Error table_admit_frame_default(
        int64_t p_sender,
        int64_t p_channel,
        const PackedByteArray &p_payload
    );

    void service_register(godot::Object *p_type, godot::Object *p_service);

    void service_unregister(godot::Object *p_type, godot::Object *p_service);

    godot::Object *service_of(godot::Object *p_type) const;
    godot::Variant service_held(godot::Object *p_type) const;

    godot::TypedArray<godot::Object> service_all(godot::Object *p_base) const;

    void service_clear();

    void wrapper_adopt(
        const godot::RID &p_entity,
        const godot::Ref<godot::RefCounted> &p_wrapper,
        godot::Object *p_owner
    );

    godot::RID entity_at_or_above(godot::Object *p_node) const;

    static godot::StringName wrapper_meta();

    static void set_wrapper_factory(const godot::Callable &p_factory);
    static bool has_wrapper_factory();
    static void clear_wrapper_factory();
    static godot::Callable wrapper_factory();

    static godot::Ref<godot::RefCounted> wrapper_at(godot::Object *p_node);
    static godot::Ref<godot::RefCounted> wrapper_ensure(godot::Object *p_root);
    static godot::Ref<godot::RefCounted> wrapper_resolve(godot::Object *p_node);
    static godot::Variant wrapper_held(
        godot::Object *p_node,
        const godot::StringName &p_entity_id,
        int64_t p_peer_id
    );

    static godot::Object *wrapper_bind(
        godot::Object *p_node,
        const godot::StringName &p_entity_id,
        int64_t p_peer_id
    );

    godot::RID entity_parent_of(const godot::RID &p_entity) const;

    godot::RID entity_scene_of(const godot::RID &p_entity) const;

    godot::Ref<godot::RefCounted> scene_handle_of(const godot::RID &p_entity);

    godot::Ref<godot::RefCounted> entity_scene_facet(
        const godot::Ref<NetwEntityRecord> &p_record,
        godot::Object *p_wrapper
    );

    void scene_publish_live(
        int64_t p_route,
        const godot::RID &p_container,
        const godot::String &p_name
    );

    bool entity_reparent_crosses(
        const godot::RID &p_entity,
        const godot::RID &p_destination
    );

    void entity_reparent(
        const godot::Ref<NetwEntityRecord> &p_record,
        godot::Object *p_owner,
        godot::Object *p_new_parent,
        const godot::Ref<NetwReparentOpts> &p_opts
    );

    static void entity_move(
        const godot::Ref<NetwEntityRecord> &p_record,
        godot::Object *p_owner,
        godot::Object *p_new_parent,
        const godot::Ref<NetwReparentOpts> &p_opts
    );

    void set_spawn_state_gather(const godot::Callable &p_gather);

    void set_replication_plane(godot::Object *p_plane);

    static void entity_enter_tree(
        godot::Object *p_wrapper,
        godot::Object *p_owner,
        const godot::Ref<NetwEntityRecord> &p_record,
        godot::Object *p_session,
        bool p_is_authority
    );

    static void entity_request_control(
        godot::Object *p_wrapper,
        godot::Object *p_owner,
        godot::Object *p_plane
    );

    static void entity_broadcast_control(
        godot::Object *p_wrapper,
        godot::Object *p_plane,
        int64_t p_peer
    );
    godot::Object *replication_plane() const;

    godot::Ref<godot::RefCounted> entity_derived_binding(
        godot::Object *p_owner,
        int64_t p_record,
        int64_t p_route
    );
    godot::Array entity_derived_group(int64_t p_route);

    bool entity_governs_property(
        godot::Object *p_owner,
        const godot::NodePath &p_path,
        godot::Object *p_exclude,
        int64_t p_route
    );
    godot::Callable spawn_state_gather() const { return spawn_state; }

    godot::Node *entity_instantiate_from(
        godot::Object *p_template,
        const godot::Callable &p_configure
    );

    static godot::Node *entity_instantiate_copy(
        godot::Object *p_template,
        const godot::Callable &p_configure
    );

    godot::Node *entity_spawn_under(
        godot::Object *p_owner,
        godot::Object *p_parent,
        const godot::StringName &p_id
    );

    static godot::Node *entity_spawn_copy_under(
        godot::Object *p_owner,
        godot::Object *p_parent,
        const godot::StringName &p_id
    );

    godot::Node *entity_instantiate_player(
        godot::Object *p_owner,
        godot::Object *p_participant
    );

    godot::Node *entity_spawn_player(
        godot::Object *p_owner,
        godot::Object *p_participant,
        godot::Object *p_scene
    );

    void entity_linger(
        const godot::Ref<NetwEntityRecord> &p_record,
        godot::Object *p_owner,
        int64_t p_pumps
    );

    void entity_free_owner(godot::Object *p_owner);

    void entity_settle_reparented(
        godot::Object *p_wrapper,
        godot::Object *p_owner,
        const godot::Ref<NetwReparentOpts> &p_opts
    );

    godot::Dictionary entity_describe(int64_t p_route) const;

    /* Reports the stage p_record now holds against the one it held before an
     * act, and nothing at all when the act moved no stage.
     */
    void entity_note_stage(
        const godot::Ref<NetwEntityRecord> &p_record,
        int64_t p_from
    );

    void entity_announce_reparented(
        godot::Object *p_wrapper,
        godot::Object *p_owner,
        const godot::Ref<NetwReparentOpts> &p_opts
    );

    bool scene_request_flooded(int peer, int64_t now_msec);
    static godot::StringName scene_container_meta();
    static godot::Node *scene_build_container(bool p_hosting, bool p_own_world);
    static godot::StringName scene_packed_stem(
        const godot::Ref<godot::PackedScene> &p_packed
    );
    static void scene_install_level(
        godot::Object *p_container,
        godot::Object *p_level
    );

    static godot::NodePath relative_path(
        godot::Object *p_source,
        godot::Object *p_target
    );

    static godot::NodePath property_path(
        godot::Object *p_source,
        const godot::StringName &p_property,
        godot::Object *p_base
    );

    godot::Ref<godot::RefCounted> wrapper_of(const godot::RID &p_entity) const;

    godot::Object *wrapper_owner(const godot::RID &p_entity) const;
    godot::RID handle_of_wrapper(godot::Object *p_wrapper) const;
    godot::Ref<godot::RefCounted> wrapper_for_route(int64_t p_route) const;

    godot::Ref<godot::RefCounted> wrapper_for_id(int64_t p_id) const;

    godot::TypedArray<godot::Object> wrapper_live() const;

    void wrapper_sweep_retired();
    void wrapper_clear();

    bool liveness_bind(
        const godot::RID &p_entity,
        int64_t p_route,
        const godot::Ref<godot::RefCounted> &p_wrapper,
        const godot::Ref<NetwEntityRecord> &p_record,
        godot::Object *p_owner
    );

    godot::Error liveness_adopt_route(
        int64_t p_route,
        const godot::Ref<godot::RefCounted> &p_wrapper,
        const godot::Ref<NetwEntityRecord> &p_record,
        godot::Object *p_owner
    );

    void liveness_publish_live(int64_t p_route);

    void liveness_settle_local_player(int64_t p_route);

    void participant_adopt(
        int64_t p_peer,
        const godot::Ref<godot::RefCounted> &p_participant
    );
    godot::Ref<godot::RefCounted> participant_of(int64_t p_peer) const;
    bool participant_has(int64_t p_peer) const;
    godot::TypedArray<godot::Object> participant_all() const;
    void participant_forget(int64_t p_peer);
    void participant_clear();

    // The scene identity p_peer's participant is seated in, or an invalid RID.
    godot::RID participant_seat(int64_t p_peer) const;
    /* Seats p_peer in p_scene, answering whether the seat changed.
     *
     * The answer is what lets the caller announce a move exactly once, so a
     * re-seat into the scene already held announces nothing.
     */
    bool participant_take_seat(int64_t p_peer, const godot::RID &p_scene);
    /* Empties p_peer's seat only when p_scene is the one it holds.
     *
     * A move reassigns the seat before the scene it left releases it, so a
     * release naming the older scene must not evict the participant from the
     * newer one.
     */
    bool participant_leave_seat(int64_t p_peer, const godot::RID &p_scene);
    // Every peer seated in p_scene, in peer order.
    godot::PackedInt64Array participant_seated_in(const godot::RID &p_scene
    ) const;

    void participant_publish_joined(int64_t p_peer);

    bool liveness_linger(const godot::RID &p_entity);

    bool liveness_retire(int64_t p_route);

    int64_t liveness_reserve_route();

    int64_t liveness_allocate_route(godot::Object *p_wrapper);

    bool liveness_bind_route(int64_t p_route, godot::Object *p_wrapper);

    void liveness_bind_routes_data(const godot::PackedInt64Array &p_routes);
    void liveness_tombstone_routes_data(const godot::PackedInt64Array &p_routes
    );

    int64_t liveness_route_of(godot::Object *p_wrapper) const;
    int64_t liveness_state_of(godot::Object *p_wrapper) const;
    int64_t liveness_route_state(int64_t p_route) const;

    godot::RID liveness_adopt(godot::Object *p_wrapper);
    /* The entity handle p_node stands for, adopting the record on the way.
     *
     * It is `liveness_adopt` over the record at or above p_node, which is one
     * verb rather than two because every caller wanted both halves and a
     * caller that took only the first would hold a handle the index does not.
     */
    godot::RID entity_of(godot::Object *p_node);
    godot::StringName scene_stem(const godot::RID &p_scene) const;

    godot::StringName scene_layer_id(const godot::RID &p_scene) const;

    godot::Object *liveness_node_of(int64_t p_route) const;
    godot::TypedArray<godot::Object> liveness_live_entities() const;

    void liveness_when_live(
        int64_t p_route,
        const godot::Callable &p_callback,
        int64_t p_deadline,
        bool p_on_clock,
        const godot::Callable &p_on_timeout
    );
    int64_t liveness_pending_live_count() const;

    void liveness_poll(int64_t p_clock_tick);

    void liveness_clear_session();

    godot::Ref<godot::RefCounted> get_local_player() const;

private:
    void set_local_player(
        const godot::Ref<godot::RefCounted> &p_player,
        int64_t p_id
    );

    godot::Ref<NetwEntityRecord> record_of_wrapper(godot::Object *p_wrapper
    ) const;
    static godot::Object *owner_of_wrapper(godot::Object *p_wrapper);
    godot::Callable owner_exit_hook(godot::Object *p_wrapper);
    godot::Callable despawning_hook(godot::Object *p_wrapper);

public:
    void liveness_owner_exiting(godot::Object *p_wrapper);
    void liveness_resolve_tracked_exit(
        int64_t p_route,
        godot::Object *p_wrapper
    );
    void liveness_owner_despawning(
        const godot::StringName &p_reason,
        godot::Object *p_wrapper
    );
    void liveness_transition_dead(int64_t p_route);

public:

    // Whether the value is a suspended GDScript call rather than an answer.
    static bool is_coroutine(const Variant &p_value);

    // Whether p_script REDECLARES p_seam somewhere between itself and
    // p_base_name, rather than merely inheriting it. A method list carries
    // inherited entries too, so only the count separates the two.
    //
    // Resolved once per seam, because a script's method table cannot change
    // for a live session.
    bool overrides_seam(
        const Ref<godot::Script> &p_script,
        const StringName &p_base_name,
        const StringName &p_seam
    );
    void forget_seam_overrides();

    void seam_entered(
        const StringName &p_seam,
        int64_t p_event,
        int64_t p_route,
        const Dictionary &p_detail
    );
    Variant seam_settled(
        const StringName &p_seam,
        int64_t p_event,
        int64_t p_route,
        const Variant &p_result,
        const Variant &p_fallback
    );
};

} // namespace netw

VARIANT_ENUM_CAST(netw::NetwMultiplayerCore::TableParam);
