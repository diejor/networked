## The one [MultiplayerAPIExtension] a [MultiplayerTree] ever installs.
##
## A tree whose installed API is not [NetwMultiplayer] is unrepresentable.
## [MultiplayerTree] constructs exactly one per session and nothing else
## installs an API on the tree's branch. It wraps an
## [member inner] [SceneMultiplayer] rather than reimplementing peer transport,
## and plays three roles on top of it.
## [codeblock]
## wrapper   inner keeps the MultiplayerPeer, the connection
##           lifecycle, and the auth handshake
## seam      every node-facing call (spawner and synchronizer
##           registration, native @rpc dispatch) passes through
##           here on its way to inner
## carrier   every Networked frame leaves through send_packet over
##           SceneMultiplayer.send_bytes and arrives back through
##           inner's raw packet signal, where a NetwFrameEnvelope
##           magic byte selects Networked framing versus
##           peer_packet pass-through for application bytes
## [/codeblock]
class_name NetwMultiplayer
extends MultiplayerAPIExtension

## The wrapped [SceneMultiplayer]. Owns the [MultiplayerPeer], connection
## lifecycle, [method SceneMultiplayer.send_bytes], and the auth protocol.
var inner: SceneMultiplayer

## The steady-state half of the replication core, covering the sender tick
## pump, per-peer aggregation buffers, receive-side dispatch, and on-demand
## property and signal sync. Constructed and owned by this extension.
## Never [code]null[/code]. See [NetwReplicationInterface].
var replication: NetwReplicationInterface

## The RPC half of the replication core, covering [method Netw.rpc] and
## [method Netw.request] framing, transactions, and deferred-call parking.
## Constructed and owned by this extension. Never [code]null[/code]. See
## [NetwRpcInterface].
var rpc_interface: NetwRpcInterface

## The tick engine. Never [code]null[/code], inert until a [MultiplayerClock]
## configurator registers through
## [method MultiplayerAPI.object_configuration_add]. See [NetwClockInterface].
var clock: NetwClockInterface

## The lag-compensation engine. Never [code]null[/code], inert until a
## [LagCompensation] configurator registers through
## [method MultiplayerAPI.object_configuration_add]. See
## [NetwLagCompensationInterface].
var lag_compensation: NetwLagCompensationInterface

## The persistence engine. Never [code]null[/code]. Registers nothing on clients
## and no-ops without a [method Netw.configure_persistence] archetype. See
## [NetwPersistenceInterface].
var persistence: NetwPersistenceInterface

## Liveness and routing facade for this tree. See [NetwLivenessInterface].
var liveness: NetwLivenessInterface

## Visibility and interest facade for this tree. See [NetwInterestInterface].
var interest: NetwInterestInterface

# The one weakref in this design. Resolved through [member tree].
var _tree_ref: WeakRef

# Weak registry of every constructed extension, backing live_sessions().
# Weakrefs because a strong static list would keep disposed sessions alive.
static var _session_refs: Array[WeakRef] = []

# Memoized backing for [member connect].
var _connect: NetwConnect

## The owning [MultiplayerTree], or [code]null[/code] after teardown.
var tree: MultiplayerTree:
	get:
		return _tree_ref.get_ref() as MultiplayerTree if _tree_ref else null


func _init(inner_api: SceneMultiplayer = null, owner_tree: MultiplayerTree = null) -> void:
	inner = inner_api if inner_api else SceneMultiplayer.new()
	# The tree weakref is set before any interface constructs so an interface
	# _init can already read api.tree.
	if owner_tree:
		_tree_ref = weakref(owner_tree)
	replication = NetwReplicationInterface.new(self)
	rpc_interface = NetwRpcInterface.new(self, replication)
	clock = NetwClockInterface.new(self)
	lag_compensation = NetwLagCompensationInterface.new(self)
	persistence = NetwPersistenceInterface.new(self)
	liveness = NetwLivenessInterface.new(self)
	interest = NetwInterestInterface.new(self)
	# Dead routes drop their unreliable-property sequence records so they never
	# outlive the entity they track.
	liveness.entity_dead.connect(replication.clear_route)
	# The tick pump binds once at construction. The signal lives on the
	# interface, so an inert clock simply never fires it.
	clock.after_tick.connect(_on_clock_tick)
	_bind_inner_signals()
	if owner_tree:
		_bind_tree_signals(owner_tree)
	_session_refs.append(weakref(self))


## True while this extension is installed as the [MultiplayerAPI] for
## [member tree]'s branch.
func is_active() -> bool:
	var t := tree
	return t != null and is_instance_valid(t) and t.multiplayer == self


## Returns every [NetwMultiplayer] currently passing [method is_active], in
## construction order. [method Netw.replicate] resolves its session through
## this list when exactly one session is active. Multi-session hosts (the
## test harness, the debugger's tiled participants) address a session
## directly through [method Netw.of] instead.
static func live_sessions() -> Array[NetwMultiplayer]:
	var out: Array[NetwMultiplayer] = []
	var kept: Array[WeakRef] = []
	for ref in _session_refs:
		var api := ref.get_ref() as NetwMultiplayer
		if api == null:
			continue
		kept.append(ref)
		if api.is_active():
			out.append(api)
	_session_refs = kept
	return out


## Returns the [NetwMultiplayer] installed on [param node]'s branch, or
## [code]null[/code] off-tree, outside a [MultiplayerTree] branch, or before
## the tree has installed its API.
static func of(node: Node) -> NetwMultiplayer:
	if not node.is_inside_tree():
		return null
	return node.multiplayer as NetwMultiplayer


func _bind_inner_signals() -> void:
	if not inner.peer_packet.is_connected(_on_inner_peer_packet):
		inner.peer_packet.connect(_on_inner_peer_packet)
	if not inner.peer_connected.is_connected(_on_inner_peer_connected):
		inner.peer_connected.connect(_on_inner_peer_connected)
	if not inner.peer_disconnected.is_connected(_on_inner_peer_disconnected):
		inner.peer_disconnected.connect(_on_inner_peer_disconnected)
	if not inner.connected_to_server.is_connected(_on_inner_connected_to_server):
		inner.connected_to_server.connect(_on_inner_connected_to_server)
	if not inner.connection_failed.is_connected(_on_inner_connection_failed):
		inner.connection_failed.connect(_on_inner_connection_failed)
	if not inner.server_disconnected.is_connected(_on_inner_server_disconnected):
		inner.server_disconnected.connect(_on_inner_server_disconnected)


func _unbind_inner_signals() -> void:
	if inner.peer_packet.is_connected(_on_inner_peer_packet):
		inner.peer_packet.disconnect(_on_inner_peer_packet)
	if inner.peer_connected.is_connected(_on_inner_peer_connected):
		inner.peer_connected.disconnect(_on_inner_peer_connected)
	if inner.peer_disconnected.is_connected(_on_inner_peer_disconnected):
		inner.peer_disconnected.disconnect(_on_inner_peer_disconnected)
	if inner.connected_to_server.is_connected(_on_inner_connected_to_server):
		inner.connected_to_server.disconnect(_on_inner_connected_to_server)
	if inner.connection_failed.is_connected(_on_inner_connection_failed):
		inner.connection_failed.disconnect(_on_inner_connection_failed)
	if inner.server_disconnected.is_connected(_on_inner_server_disconnected):
		inner.server_disconnected.disconnect(_on_inner_server_disconnected)


# Re-emits [member inner]'s connection-lifecycle signals on this extension, the
# standard [MultiplayerAPIExtension] wrapper pattern, so consumers that bind to
# [code]tree.api.peer_connected[/code] and friends never need to reach into
# [member inner] directly.
func _on_inner_peer_connected(id: int) -> void:
	peer_connected.emit(id)


func _on_inner_peer_disconnected(id: int) -> void:
	peer_disconnected.emit(id)


func _on_inner_connected_to_server() -> void:
	connected_to_server.emit()


func _on_inner_connection_failed() -> void:
	connection_failed.emit()


func _on_inner_server_disconnected() -> void:
	server_disconnected.emit()


## Breaks the signal-connection cycles between this extension and the
## [RefCounted] objects it owns ([member inner], [member clock]) so the whole
## group can be released, since a signal connection strong-references its
## target. Called by [MultiplayerTree] when the tree is deleted. The extension
## is unusable afterwards.
func dispose() -> void:
	_unbind_inner_signals()
	if clock.after_tick.is_connected(_on_clock_tick):
		clock.after_tick.disconnect(_on_clock_tick)
	if liveness.entity_dead.is_connected(replication.clear_route):
		liveness.entity_dead.disconnect(replication.clear_route)
	replication.dispose()
	rpc_interface.dispose()


## Replaces [member inner] in place, rebinding this same [NetwMultiplayer]
## installation to a new [SceneMultiplayer]. Used by backends that bring their
## own transport. The extension object, its installation on the tree's branch,
## and every cached [code]NetwMultiplayer.of(node)[/code] reference stay valid
## across the swap.
func adopt_inner(new_inner: SceneMultiplayer) -> void:
	if new_inner == inner:
		return
	_unbind_inner_signals()
	inner = new_inner
	_bind_inner_signals()


#region Carrier

## Emitted for raw packets received through
## [method SceneMultiplayer.send_bytes] that do not carry Networked framing.
## Networked reserves the two [NetwFrameEnvelope] magic first bytes, so
## application byte traffic must not start with them.
signal peer_packet(id: int, packet: PackedByteArray)

# Aggregation packet counters
var _sent_packets: int = 0
var _sent_bytes: int = 0
var _received_packets: int = 0
var _received_bytes: int = 0

var _frame_counter: int = 0

# The sender of the carrier frame currently being dispatched, or 0 when no
# relayed dispatch is on the stack. NetwReplicationInterface._dispatch stamps it
# so _get_remote_sender_id answers with the frame's sender for handlers reached
# through the carrier, the same value a native @rpc handler would read. Nested
# dispatch saves and restores it.
var _relay_sender: int = 0

# Outbound unreliable datagram sequence, one u16 counter per destination peer.
# Every unreliable datagram carries the next value so the receiver can drop
# state that a fresher datagram already superseded. Reliable datagrams are
# ordered by the transport and carry none.
var _unreliable_send_seqs: Dictionary = { }

# The freshest inbound unreliable datagram seq seen from each sender, peer -> u16.
# An outbound unreliable datagram to a peer this map knows echoes this value in
# the acked shape, telling that peer the newest datagram of theirs we hold.
var _inbound_freshest_seq: Dictionary = { }

# The freshest seq each peer has echoed back for our own sends, peer -> u16. This
# is the transport ack: it names the newest datagram of ours that peer provably
# holds, the conservative baseline every delta-against-baseline lane diffs against
# (masked state, masked broadcast). It flows consumer to author per peer-pair.
# Distinct from the consumption ack the SYNC_FLAG_ACKED bit carries, which names
# the input tick the server simulated, not a datagram it received.
var _peer_state_ack: Dictionary = { }

# The freshest inbound seq we have echoed back to each peer, peer -> u16. Compared
# against _inbound_freshest_seq after a tick's flush so a peer we hold fresh state
# for but sent no piggybacked echo to this pass gets a standalone acked datagram.
# This keeps the transport ack unconditional for a non-reciprocal masked flow, a
# client author broadcasting to a silent observer that sends it no datagram to
# piggyback on.
var _last_echoed_seq: Dictionary = { }

# Acked-shape datagram counters, free delivery observability.
var _state_acks_out: int = 0
var _state_acks_in: int = 0
# Of the acked-shape datagrams sent, those that carried no piggyback (a standalone
# transport ack this tick's flush emitted).
var _standalone_acks_out: int = 0


## Sends one framed carrier datagram to [param peer_id], prefixed with the
## [NetwFrameEnvelope] magic byte for its transfer mode. An unreliable datagram
## additionally carries a per-peer [code]u16[/code] sequence after the magic
## byte, the freshness stamp [NetwSyncPipeline] gates unreliable entity frames
## on. Aggregated sends arrive here from
## [method NetwReplicationInterface.send_to]. Returns the assigned unreliable
## seq, or [code]-1[/code] for a reliable send or a dropped/empty packet, so a
## caller staging masked-delta rows (§5.6) can key them by the seq that will
## carry their acknowledgment.
func send_packet(peer_id: int, bytes: PackedByteArray, reliable: bool) -> int:
	if bytes.is_empty():
		return -1
	# A peer that left mid-poll can still sit in a recipient list drawn from
	# liveness books that trail the connection by a cleanup signal. Native
	# send_bytes treats a departed target as a bug, so the carrier drops it here.
	if peer_id != 0 and peer_id not in inner.get_peers():
		return -1
	_sent_packets += 1
	_sent_bytes += bytes.size()
	var framed := PackedByteArray()
	var assigned_seq := -1
	if reliable:
		framed.resize(1)
		framed[0] = NetwFrameEnvelope.CARRIER_MAGIC_RELIABLE
	else:
		var seq: int = (int(_unreliable_send_seqs.get(peer_id, 0)) + 1) & 0xFFFF
		_unreliable_send_seqs[peer_id] = seq
		assigned_seq = seq
		if _inbound_freshest_seq.has(peer_id):
			# We have heard from this peer, so echo the newest datagram of theirs
			# we hold in the acked shape: [magic | seq u16 | ack u16 | frames].
			framed.resize(5)
			framed[0] = NetwFrameEnvelope.CARRIER_MAGIC_UNRELIABLE_ACKED
			framed.encode_u16(1, seq)
			framed.encode_u16(3, int(_inbound_freshest_seq[peer_id]))
			_state_acks_out += 1
			# This real datagram already carried the echo, so the end-of-tick
			# standalone pass owes this peer nothing.
			_last_echoed_seq[peer_id] = int(_inbound_freshest_seq[peer_id])
		else:
			framed.resize(3)
			framed[0] = NetwFrameEnvelope.CARRIER_MAGIC_UNRELIABLE
			framed.encode_u16(1, seq)
	framed.append_array(bytes)
	inner.send_bytes(
		framed,
		peer_id,
		MultiplayerPeer.TRANSFER_MODE_RELIABLE
		if reliable
		else MultiplayerPeer.TRANSFER_MODE_UNRELIABLE,
	)
	return assigned_seq


# Demuxes the raw byte channel: the magic first byte claims the packet for the
# replication core, anything else re-emits for the application. The sender id
# rides the signal, so relayed dispatch never consults
# [method MultiplayerAPI.get_remote_sender_id].
func _on_inner_peer_packet(id: int, packet: PackedByteArray) -> void:
	if packet.is_empty():
		return
	var magic := packet[0]
	if magic == NetwFrameEnvelope.CARRIER_MAGIC_RELIABLE:
		_received_packets += 1
		_received_bytes += packet.size() - 1
		replication.receive_carrier(packet.slice(1), id, true)
		return
	if magic == NetwFrameEnvelope.CARRIER_MAGIC_UNRELIABLE:
		# The u16 after the magic is the datagram's freshness stamp. A packet
		# too short to carry it is not valid Networked framing.
		if packet.size() < 3:
			return
		_received_packets += 1
		_received_bytes += packet.size() - 3
		var seq := packet.decode_u16(1)
		_note_inbound_seq(id, seq)
		replication.receive_carrier(packet.slice(3), id, false, seq)
		return
	if magic == NetwFrameEnvelope.CARRIER_MAGIC_UNRELIABLE_ACKED:
		# The acked shape carries the freshness u16 then the echo u16 of the
		# newest datagram of ours this peer holds, before the frames.
		if packet.size() < 5:
			return
		_received_packets += 1
		_received_bytes += packet.size() - 5
		_state_acks_in += 1
		var seq := packet.decode_u16(1)
		_note_inbound_seq(id, seq)
		_note_state_ack(id, packet.decode_u16(3))
		replication.receive_carrier(packet.slice(5), id, false, seq)
		return
	peer_packet.emit(id, packet)


# Records [param seq] as the freshest inbound datagram from [param sender] when it
# is newer across the u16 half window, so a reordered datagram never rolls the
# echo backward.
func _note_inbound_seq(sender: int, seq: int) -> void:
	if not _inbound_freshest_seq.has(sender) \
			or _seq_is_fresher(seq, int(_inbound_freshest_seq[sender])):
		_inbound_freshest_seq[sender] = seq


# Records [param ack] as [param peer]'s confirmation of our sends when it is newer
# across the u16 half window. The confirmed seq only advances, so a stalled echo
# from a silent peer holds its baseline rather than corrupting it. Advancing it
# promotes peer's masked-lane in-flight rows through the pipeline (§5.6); a
# stalled ack (this branch not taken) correctly leaves those rows untouched.
func _note_state_ack(peer: int, ack: int) -> void:
	if not _peer_state_ack.has(peer) \
			or _seq_is_fresher(ack, int(_peer_state_ack[peer])):
		_peer_state_ack[peer] = ack
		replication.note_peer_ack(peer, ack)


# Returns true when [param a] is fresher than [param b] on the u16 sequence ring,
# judged across the half window so wraparound stays correct.
static func _seq_is_fresher(a: int, b: int) -> bool:
	return a != b and ((a - b) & 0xFFFF) < 32768


## Returns the freshest datagram seq [param peer] has echoed as held, or
## [code]-1[/code] when that peer has acked nothing. The masked delta lane reads
## this as each recipient's confirmed baseline seq (§5.6).
func peer_state_ack(peer: int) -> int:
	return int(_peer_state_ack.get(peer, -1))


## Emits a standalone transport ack to every peer whose freshest inbound
## datagram seq no outbound datagram this tick already echoed, so the masked
## delta lane's confirmed baseline advances even for a peer that sends the
## author nothing to piggyback the echo on.
## [codeblock]
## author --seq N--> silent observer   (a client-authored broadcast)
## author <--ack N-- standalone echo   (the observer had no reply to ride)
##
## chatty pair: the echo piggybacks the real reply, nothing extra is sent
## [/codeblock]
## Only a fresh inbound seq produces an echo, so the rule self-throttles to the
## author's send rate. Driven once per tick by
## [method NetwReplicationInterface.on_clock_tick] after the aggregation
## buffers flush, so a real datagram's piggybacked echo always wins.
func flush_standalone_acks() -> void:
	if not inner.multiplayer_peer:
		return
	for peer_id in _inbound_freshest_seq:
		var fresh := int(_inbound_freshest_seq[peer_id])
		if _last_echoed_seq.get(peer_id) == fresh:
			continue
		if peer_id != 0 and peer_id not in inner.get_peers():
			continue
		_send_standalone_ack(peer_id, fresh)


# Sends a zero-frame acked datagram to [param peer_id] carrying [param ack], the
# freshest inbound seq of theirs we hold. The acked shape's [magic | seq | ack]
# header round-trips through the receive path with no frames to dispatch
# (receive_carrier no-ops on the empty remainder), so this is the standalone form
# of the echo send_packet piggybacks on a real datagram. It bypasses that path's
# empty-payload drop deliberately: the whole point is a datagram with no payload.
func _send_standalone_ack(peer_id: int, ack: int) -> void:
	var seq: int = (int(_unreliable_send_seqs.get(peer_id, 0)) + 1) & 0xFFFF
	_unreliable_send_seqs[peer_id] = seq
	var framed := PackedByteArray()
	framed.resize(5)
	framed[0] = NetwFrameEnvelope.CARRIER_MAGIC_UNRELIABLE_ACKED
	framed.encode_u16(1, seq)
	framed.encode_u16(3, ack)
	_last_echoed_seq[peer_id] = ack
	_state_acks_out += 1
	_standalone_acks_out += 1
	_sent_packets += 1
	_sent_bytes += framed.size()
	inner.send_bytes(framed, peer_id, MultiplayerPeer.TRANSFER_MODE_UNRELIABLE)


# Clock tick callback that pumps registered senders.
func _on_clock_tick(_delta: float, tick: int) -> void:
	replication.on_clock_tick(tick)


# The tick a received payload is stamped with: the session tick when the clock
# engine is configured, otherwise a local frame counter.
func _receive_tick() -> int:
	return clock.tick if clock.is_configured() else _frame_counter


## Returns drop and occupancy counters for the debug monitor.
##
## A nonzero drop count across a spawn or despawn edge is the healthy outcome
## of the drop-if-absent contract. Sustained growth in steady state means a
## route never became [constant NetwLivenessInterface.State.LIVE] on the receiver.
## [codeblock]
## {
##   ┠╴ drops_unknown_route: int    # no binding on this peer yet
##   ┠╴ drops_not_live: int         # route known but LINGERING or DEAD
##   ┠╴ drops_no_node: int          # binding exists, owner node freed
##   ┠╴ drops_backlog_limit: int    # messages dropped due to buffer limit
##   ┠╴ drops_traversal: int        # hostile comp path rejected (security)
##   ┠╴ drops_comp_unresolved: int  # comp path safe but sub-node not spawned
##   ┠╴ sends_dropped_unroutable: int # sender: target not in a NetwEntity
##   ┠╴ sends_dropped_not_live: int   # sender: target has no live route
##   ┠╴ derived_sets_active: int    # derived set bindings on the tick pump
##   ┠╴ masked_frames_out: int      # masked-lane volatile frames sent (§5.6)
##   ┠╴ masked_frames_full: int     # of those, every field masked in (gain edge or loss heal)
##   ┠╴ sync_sets_active: int       # consumed stock synchronizers on the pump
##   ┠╴ sync_frames_out: int        # consumed SYNC frames sent
##   ┠╴ sync_frames_in: int         # consumed SYNC frames applied
##   ┠╴ delta_frames_out: int       # consumed SYNC_DELTA frames sent
##   ┠╴ delta_frames_in: int        # consumed SYNC_DELTA frames applied
##   ┠╴ drops_sync_no_set: int      # SYNC ordinal with no binding yet
##   ┠╴ drops_sync_bad_sender: int  # SYNC from a non-authority sender
##   ┠╴ drops_sync_poisoned: int    # SYNC on a binding whose schema disagreed
##   ┠╴ drops_sync_unknown_flag: int # SYNC flag bit this version cannot read
##   ┠╴ spawn_book_armed: int       # verb ran, node not yet placed
##   ┠╴ spawn_book_spawned: int     # authority-issued spawns (replay book)
##   ┠╴ spawn_book_recv: int        # routes materialized from the server
##   ┠╴ sent_packets: int           # aggregated packets sent
##   ┠╴ sent_bytes: int             # total bytes sent
##   ┠╴ received_packets: int       # aggregated packets received
##   ┠╴ received_bytes: int         # total bytes received
##   ┠╴ state_acks_out: int         # acked-shape datagrams sent (echoing inbound seq)
##   ┠╴ state_acks_in: int          # acked-shape datagrams received
##   ┖╴ standalone_acks_out: int    # of state_acks_out, those with no piggyback
## }
## [/codeblock]
func monitor_snapshot() -> Dictionary:
	var repl := replication.counters()
	var rpc_counters := rpc_interface.counters()
	return {
		&"drops_unknown_route": repl[&"drops_unknown_route"],
		&"drops_not_live": repl[&"drops_not_live"],
		&"drops_no_node": repl[&"drops_no_node"],
		&"drops_backlog_limit": rpc_counters[&"drops_backlog_limit"],
		&"drops_traversal": repl[&"drops_traversal"],
		&"drops_comp_unresolved": repl[&"drops_comp_unresolved"],
		&"sends_dropped_unroutable": (
				repl[&"sends_dropped_unroutable"] + rpc_counters[&"sends_dropped_unroutable"]
		),
		&"sends_dropped_not_live": (
				repl[&"sends_dropped_not_live"] + rpc_counters[&"sends_dropped_not_live"]
		),
		&"sync_drops_stale": repl[&"sync_drops_stale"],
		&"derived_sets_active": repl[&"derived_sets_active"],
		&"derived_frames_in": repl[&"derived_frames_in"],
		&"drops_derived_no_set": repl[&"drops_derived_no_set"],
		&"drops_derived_bad_sender": repl[&"drops_derived_bad_sender"],
		&"drops_derived_schema": repl[&"drops_derived_schema"],
		&"masked_frames_out": repl[&"masked_frames_out"],
		&"masked_frames_full": repl[&"masked_frames_full"],
		&"sync_sets_active": repl[&"sync_sets_active"],
		&"sync_frames_out": repl[&"sync_frames_out"],
		&"sync_frames_in": repl[&"sync_frames_in"],
		&"delta_frames_out": repl[&"delta_frames_out"],
		&"delta_frames_in": repl[&"delta_frames_in"],
		&"drops_sync_no_set": repl[&"drops_sync_no_set"],
		&"drops_sync_bad_sender": repl[&"drops_sync_bad_sender"],
		&"drops_sync_poisoned": repl[&"drops_sync_poisoned"],
		&"drops_sync_unknown_flag": repl[&"drops_sync_unknown_flag"],
		&"spawn_book_armed": repl[&"spawn_book_armed"],
		&"spawn_book_spawned": repl[&"spawn_book_spawned"],
		&"spawn_book_recv": repl[&"spawn_book_recv"],
		&"sent_packets": _sent_packets,
		&"sent_bytes": _sent_bytes,
		&"received_packets": _received_packets,
		&"received_bytes": _received_bytes,
		&"state_acks_out": _state_acks_out,
		&"state_acks_in": _state_acks_in,
		&"standalone_acks_out": _standalone_acks_out,
	}


# Drops all per-session state so the tick pump has nothing to touch after the
# session tears down. Deferred to avoid mutating registries mid-teardown,
# mirroring NetwLivenessInterface.
func _on_tree_session_ended() -> void:
	_clear_session_state.call_deferred()


func _clear_session_state() -> void:
	_unreliable_send_seqs.clear()
	_inbound_freshest_seq.clear()
	_peer_state_ack.clear()
	_last_echoed_seq.clear()
	replication.clear_session()
	rpc_interface.clear_session()


func _on_tree_peer_disconnected(peer_id: int) -> void:
	# A reconnecting peer restarts its datagram sequence, so neither side may
	# keep the old connection's freshness state against it.
	_unreliable_send_seqs.erase(peer_id)
	_inbound_freshest_seq.erase(peer_id)
	_peer_state_ack.erase(peer_id)
	_last_echoed_seq.erase(peer_id)
	replication.clear_peer(peer_id)
	rpc_interface.handle_disconnect(peer_id)

#endregion


#region Session surface

## Emitted once for each accepted participant known to this peer.
##
## Fresh accepts emit on every peer. Late joiners also receive one emission per
## participant accepted before they connected.
signal participant_joined(participant: NetwParticipant)
## Emitted when this peer's participant has been accepted by the server.
signal local_participant_joined(participant: NetwParticipant)
## Emitted when [member local_participant] changes [member NetwParticipant.current_scene].
signal local_scene_changed(from: NetwScene, to: NetwScene)
## Emitted on clients when the server notifies it is shutting down.
signal server_disconnecting(reason: String)
## Emitted on the server when a client requests to kick a peer.
signal kick_requested(requester_id: int, target_id: int, reason: String)
## Emitted on the kicked peer when the server kicks them.
signal kicked(reason: String)
## Emitted on every peer when the game is paused via [method pause].
signal tree_paused(reason: String)
## Emitted on every peer when the game is unpaused via [method unpause].
signal tree_unpaused()
## Emitted when the session reaches [constant MultiplayerTree.State.ONLINE] and
## services are ready. Pairs with [signal session_ended].
signal session_entered()
## Emitted when the session leaves [constant MultiplayerTree.State.ONLINE] and
## tears down. Pairs with [signal session_entered].
signal session_ended()


# Relays the owning tree's session signals onto this extension once at
# construction, so consumers bind to a single session object instead of
# reaching for a per-call facade bundle.
func _bind_tree_signals(mt: MultiplayerTree) -> void:
	mt.participant_joined.connect(participant_joined.emit)
	mt.local_participant_joined.connect(local_participant_joined.emit)
	mt.local_scene_changed.connect(local_scene_changed.emit)
	mt.server_disconnecting.connect(server_disconnecting.emit)
	mt.kick_requested.connect(kick_requested.emit)
	mt.kicked.connect(kicked.emit)
	mt.tree_paused.connect(tree_paused.emit)
	mt.tree_unpaused.connect(tree_unpaused.emit)
	mt.session_entered.connect(session_entered.emit)
	mt.session_ended.connect(session_ended.emit)
	mt.session_ended.connect(_on_tree_session_ended)
	mt.peer_disconnected.connect(_on_tree_peer_disconnected)


## Pre-game connect / server browser facade over the tree's canonical
## [ConnectSession]. Built lazily on first access (and memoized) so repeated
## access does not stack signal relays. [code]null[/code] when no enclosing
## [MultiplayerTree] is found. See [NetwConnect].
var connect: NetwConnect:
	get:
		if _connect:
			return _connect
		var mt := tree
		if not is_instance_valid(mt):
			return null
		var session := mt.get_connect_session()
		if session == null:
			return null
		_connect = NetwConnect.new(session)
		return _connect

## The [MultiplayerSceneManager] service, or [code]null[/code].
var scene_manager: MultiplayerSceneManager:
	get:
		var mt := tree
		return mt.get_service(MultiplayerSceneManager) if mt else null

## The [NetwInterpolationInterface] service, or [code]null[/code].
var interpolation: NetwInterpolationInterface:
	get:
		var mt := tree
		if not mt:
			return null
		var service := mt.get_service(NetwInterpolationInterface) \
				as NetwInterpolationInterface
		if service:
			return service
		return mt.find_service_node(NetwInterpolationInterface) \
				as NetwInterpolationInterface


## Returns the [NetwPeerContext] for [param peer_id].
func get_peer_context(peer_id: int) -> NetwPeerContext:
	var mt := tree
	return mt.get_peer_context(peer_id) if mt else null


## Returns the service registered for [param type], or [code]null[/code].
func get_service(type: Script) -> Node:
	var mt := tree
	return mt.get_service(type) if mt else null


## Returns every registered service whose script is [param base] or a
## subclass of it. See [method MultiplayerTree.get_services].
func get_services(base: Script) -> Array[Node]:
	var mt := tree
	return mt.get_services(base) if mt else []

## All active player identities across all scenes or the sceneless world.
var all_players: Array[NetwEntity]:
	get:
		var mt := tree
		return mt.get_all_players() if mt else []

## Accepted participants known by this peer.
var participants: Array[NetwParticipant]:
	get:
		var mt := tree
		return mt.get_participants() if mt else []


## Returns the [NetwParticipant] for [param peer_id], or [code]null[/code].
func participant(peer_id: int) -> NetwParticipant:
	var mt := tree
	return mt.get_participant(peer_id) if mt else null


## Returns [code]true[/code] if this tree is acting as a listen-server host.
##
## Use this as the single source of truth for all listen-server checks
## instead of comparing [member MultiplayerTree.role] directly.
func is_listen_server() -> bool:
	var mt := tree
	return mt.role == MultiplayerTree.Role.LISTEN_SERVER if mt else false

## The original name of the [MultiplayerTree] node.
var tree_name: String:
	get:
		var mt := tree
		return mt.get_tree_name() if mt else ""


## Returns [code]true[/code] if the multiplayer peer is in an active connection.
func is_online() -> bool:
	var mt := tree
	return mt.is_online() if mt else false


## Starts the instance as a network host using [param join_payload].
func host(join_payload: JoinPayload) -> Error:
	var mt := tree
	return await mt.host(join_payload) if mt else ERR_UNCONFIGURED


## Opens the transport against the [param target] address and submits
## [param join_payload] once connected.
##
## See [method MultiplayerTree.join].
func join(
		target: JoinTarget,
		join_payload: JoinPayload,
		timeout: float = 5.0,
		quiet: bool = false,
) -> Error:
	var mt := tree
	if not mt:
		return ERR_UNCONFIGURED
	return await mt.join(target, join_payload, timeout, quiet)


## Probes the target address. Joins if reachable, hosts otherwise.
##
## See [method MultiplayerTree.join_or_host].
func join_or_host(
		target: JoinTarget,
		join_payload: JoinPayload,
) -> Error:
	var mt := tree
	if not mt:
		return ERR_UNCONFIGURED
	return await mt.join_or_host(target, join_payload)

## The tree's configured [BackendPeer], or [code]null[/code].
var backend: BackendPeer:
	get:
		var mt := tree
		return mt.backend if mt else null

## The current connection state.
var state: MultiplayerTree.State:
	get:
		var mt := tree
		return mt.state if mt else MultiplayerTree.State.OFFLINE

## The current role in the session.
var role: MultiplayerTree.Role:
	get:
		var mt := tree
		return mt.role if mt else MultiplayerTree.Role.NONE

## The local player identity for this tree, or [code]null[/code].
var local_player: NetwEntity:
	get:
		var mt := tree
		return mt.local_player if mt else null

## Accepted [NetwParticipant] for this tree, or [code]null[/code].
var local_participant: NetwParticipant:
	get:
		var mt := tree
		return mt.local_participant if mt else null


## Resolves the correct spawn location and causal token for a new player.
func get_spawn_slot(spawner_path: SceneNodePath) -> SpawnSlot:
	var mt := tree
	if not mt:
		return SpawnSlot.new()
	return mt.get_spawn_slot(spawner_path)


## Pauses the game on every peer via [code]get_tree().paused = true[/code].
##
## The pause is sent to each connected peer individually.
## [br][br][b]Server Only.[/b]
func pause(reason: String = "") -> void:
	var mt := tree
	if mt:
		mt.pause(reason)


## Unpauses the game on every peer via [code]get_tree().paused = false[/code].
##
## [br][br][b]Server Only.[/b]
func unpause() -> void:
	var mt := tree
	if mt:
		mt.unpause()


## Disconnects [param peer_id] from the session.
##
## If [param reason] is non-empty, the peer receives [signal kicked] before
## the connection is closed.
## [br][br][b]Server Only.[/b]
func kick(peer_id: int, reason: String = "") -> void:
	var mt := tree
	if mt:
		mt.kick(peer_id, reason)


## Asks the server to kick [param peer_id].
##
## The server emits [signal kick_requested] and decides whether to honor it.
## [br][br][b]Player request.[/b]
func request_kick(peer_id: int, reason: String = "") -> void:
	var mt := tree
	if mt:
		mt.request_kick(peer_id, reason)


## Saves game state, closes the multiplayer peer, and waits for the server
## to acknowledge leaving.
func leave() -> void:
	var mt := tree
	if not mt:
		return
	await mt.leave()


## Asks the server for permission to leave.
##
## The server decides whether to honor it.
## [br][br][b]Player request.[/b]
func request_leave(reason: String = "") -> void:
	var mt := tree
	if mt:
		mt.request_leave(reason)


## Notifies all clients that the server is shutting down.
##
## Clients receive [signal server_disconnecting].
## [br][br][b]Server Only.[/b]
func notify_shutdown(reason: String = "") -> void:
	var mt := tree
	if mt:
		mt.notify_shutdown(reason)

#endregion


func _poll() -> Error:
	var err := inner.poll()
	liveness.poll()
	_frame_counter += 1
	replication.on_poll()
	rpc_interface.sweep_deferred_calls()
	rpc_interface.sweep_transactions(_receive_tick())
	return err


## Intercepts native [code]@rpc[/code] dispatch. A call on a node inside a live
## [NetwEntity] is upgraded to a route-addressed [NetwRpcInterface] call, so it
## inherits liveness gating and interest-scoped fan-out and never races the
## target's spawn edge the way a NodePath-addressed native RPC does. Every other
## call, including session-lifecycle RPCs on nodes outside any entity, rides
## [member inner] unchanged.
func _rpc(peer: int, object: Object, method: StringName, args: Array) -> Error:
	if object is Node:
		var entity := NetwEntity.of(object)
		if entity and liveness.route_of(entity) > 0:
			rpc_interface.rpc_call(Callable(object, method), args, peer)
			return OK
	return inner.rpc(peer, object, method, args)


# The registration verb for every declaration node, native and ours. Addon
# configurators bind their owned interface here. Unrecognized configurations
# always forward to inner.
func _object_configuration_add(object: Object, configuration: Variant) -> Error:
	if configuration is MultiplayerClock:
		(configuration as MultiplayerClock).attach_interface(clock)
		clock._configured = true
		return OK
	if configuration is LagCompensation:
		(configuration as LagCompensation).attach_interface(lag_compensation)
		lag_compensation._configured = true
		return OK
	# A spawner registration is consumed, not forwarded, so the native
	# replicator never tracks the node and a double spawn is unrepresentable.
	# The node replicates through the Networked pipeline via NetwSpawnerCompat.
	if configuration is MultiplayerSpawner:
		return replication._spawner_compat.consume(
			object as Node, configuration as MultiplayerSpawner
		)
	# A synchronizer registration is consumed, not forwarded, so the native
	# replicator never tracks the node and its sync loop iterates empty sets.
	# The node synchronizes through the Networked pump via NetwSyncCompat.
	if configuration is MultiplayerSynchronizer and object is Node:
		return replication._sync_compat.consume(
			object as Node, configuration as MultiplayerSynchronizer
		)
	return inner.object_configuration_add(object, configuration)


func _object_configuration_remove(object: Object, configuration: Variant) -> Error:
	if configuration is MultiplayerClock:
		clock._configured = false
		return OK
	if configuration is LagCompensation:
		lag_compensation._configured = false
		return OK
	if configuration is MultiplayerSpawner:
		return replication._spawner_compat.consume_remove(
			object as Node, configuration as MultiplayerSpawner
		)
	if configuration is MultiplayerSynchronizer and object is Node:
		return replication._sync_compat.consume_remove(
			object as Node, configuration as MultiplayerSynchronizer
		)
	return inner.object_configuration_remove(object, configuration)


func _set_multiplayer_peer(p_peer: MultiplayerPeer) -> void:
	inner.multiplayer_peer = p_peer


func _get_multiplayer_peer() -> MultiplayerPeer:
	return inner.multiplayer_peer


func _get_unique_id() -> int:
	# No peer (offline) and a present-but-disconnected peer (mid-connect or
	# mid-teardown) both make the native get_unique_id push an error, and with
	# no peer it returns an invalid id. Offline and disconnected windows count
	# as server (the addon-wide authority convention), and poll-time callers
	# like the sync pump and the debug reporter hit these every drained frame,
	# so mirror the server id without the error spam.
	var peer := inner.multiplayer_peer
	if peer == null \
			or peer.get_connection_status() == MultiplayerPeer.CONNECTION_DISCONNECTED:
		return 1
	return inner.get_unique_id()


func _get_peer_ids() -> PackedInt32Array:
	return inner.get_peers()


func _get_remote_sender_id() -> int:
	# A carrier frame under dispatch answers with its own sender, so a handler
	# reached through the carrier reads the same value a native @rpc handler
	# would. Peer ids are always positive, so 0 means no relayed dispatch.
	if _relay_sender != 0:
		return _relay_sender
	return inner.get_remote_sender_id()
