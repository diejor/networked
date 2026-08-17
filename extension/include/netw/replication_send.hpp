#pragma once

/* The shell's handle on the native send pipeline.
 *
 * A binding that exists so the GDScript shell can drive `netw::repl` before
 * `NetwMultiplayer` itself is native, which is the same arrangement every core
 * that has already crossed uses. It holds no logic: every verb forwards, and
 * the currency is plain data because a per-row script hop is the one crossing
 * the migration rules forbid.
 *
 * TODO: unregister this class and delete this header once NetwMultiplayer is
 * native and constructs `netw::repl::SessionSend` directly. Nothing outside
 * the shell may depend on it before then.
 */

#include "godot/callable.hpp"
#include "godot/ref_counted.hpp"
#include "godot/variant.hpp"
#include "netw/repl/row_frame.hpp"
#include "netw/repl/session_send.hpp"

namespace netw {

class NetwReplicationSend : public godot::RefCounted {
    GDCLASS(NetwReplicationSend, godot::RefCounted)

    repl::SessionSend impl;
    godot::Callable stage;
    wire::WireRegistry registry;
    // The sends the last deferred pass answered, in the order it listed them,
    // so a caller confirms one by the index it read rather than by a row
    // address it would have to spell back across the bridge.
    godot::LocalVector<repl::RowSend> answered;

    godot::Dictionary frames_of(
        const godot::Array &p_offers,
        const repl::SessionResult &p_result,
        bool p_deferred
    );

protected:
    static void _bind_methods();

public:
    void declare_channel(
        int64_t p_id,
        const godot::StringName &p_name,
        bool p_fitted
    );

    godot::Dictionary run(
        const godot::Array &p_offers,
        int64_t p_max_bits,
        int64_t p_seq,
        int64_t p_send_id
    );

    godot::Dictionary run_deferred(const godot::Array &p_offers);

    void confirm(int64_t p_index);

    void commit(int64_t p_peer, int64_t p_seq);
    int64_t pending_count(int64_t p_peer) const;

    void set_encode_stage(const godot::Callable &p_stage);
    bool has_encode_stage() const;

    void acknowledge(int64_t p_peer, int64_t p_acked_seq);
    void retain(const godot::PackedInt32Array &p_recipients);
    void retain_row(
        int64_t p_route,
        int64_t p_comp,
        const godot::PackedInt32Array &p_recipients
    );
    void forget_peer(int64_t p_peer);
    void close_route(int64_t p_route);

    godot::Dictionary apply(
        const godot::Ref<SchemaRecord> &p_schema,
        const godot::PackedByteArray &p_held,
        const godot::PackedByteArray &p_frame,
        bool p_masked
    );

    godot::Dictionary apply_window(
        const godot::Ref<SchemaRecord> &p_schema,
        const godot::PackedByteArray &p_frame
    );

    godot::Dictionary peek(const godot::PackedByteArray &p_frame) const;

    int64_t lane_count() const;
    int64_t retained_lane_count() const;
    int64_t window_lane_count() const;
    int64_t outstanding() const;
};

} // namespace netw
