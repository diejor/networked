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

## The session lifecycle machine, driven by peer assignment. Never
## [code]null[/code]. See [NetwSessionInterface].
var session: NetwSessionInterface

## The replicated-scene registry and operation surface. Never
## [code]null[/code]. See [NetwSceneInterface].
var scenes: NetwSceneInterface

## The bootstrap phase of this session's authoring, advanced once by the
## installing embedding through [method settle].
##
## Bootstrap ordering is owned, not emergent: every embedding runs the identical
## [constant DECLARING] to [constant LIVE] sequence, so the resolve order no
## longer depends on [method Node._enter_tree] traversal. Authoring registration
## is only in-contract during [constant DECLARING].
## [codeblock]
## DECLARING   object_configuration_add lands scene and session configs
## SETTLING    settle() resolves the winning declaration in one ordered step
## LIVE        authoring resolved; session bring-up may proceed
## [/codeblock]
enum Phase {
	## Nodes and scripts register configs through
	## [method object_configuration_add]. Nothing acts on them yet.
	DECLARING,
	## The provider signalled the authored world is loaded; the session resolves
	## the winning declarations as one ordered step.
	SETTLING,
	## Authoring resolved, session bring-up may proceed. Orthogonal to
	## [enum NetwSessionInterface.State]: a [constant LIVE] session is still
	## [constant NetwSessionInterface.State.OFFLINE] until it hosts or joins.
	LIVE,
}

## Handles application-defined authentication packets after Networked
## classifies its reserved protocol frames.
##
## Networked always owns [member SceneMultiplayer.auth_callback] on
## [member inner] so same-port probes remain isolated from session peers. Set
## this callback instead of reaching through [member inner]. Probe packets are
## consumed internally and every other packet is forwarded unchanged. When
## set, this callback takes precedence over the configured [NetwAuthFlow].
var auth_callback: Callable = Callable():
	set(value):
		auth_callback = value
		if session:
			session.set_auth_callback(value)

## Seconds an authenticating peer may remain pending before Godot disconnects
## it. Mirrors [member SceneMultiplayer.auth_timeout].
var auth_timeout: float:
	get:
		return inner.auth_timeout
	set(value):
		inner.auth_timeout = value

## Root path used by the wrapped replicator for relative node addressing.
## Mirrors [member SceneMultiplayer.root_path].
var root_path: NodePath:
	get:
		return inner.root_path
	set(value):
		inner.root_path = value

## Whether RPC payloads may decode serialized objects.
## Mirrors [member SceneMultiplayer.allow_object_decoding].
var allow_object_decoding: bool:
	get:
		return inner.allow_object_decoding
	set(value):
		inner.allow_object_decoding = value

## Whether new peer connections are rejected.
## Mirrors [member SceneMultiplayer.refuse_new_connections].
var refuse_new_connections: bool:
	get:
		return inner.refuse_new_connections
	set(value):
		inner.refuse_new_connections = value

## Whether the server relays peer packets between clients.
## Mirrors [member SceneMultiplayer.server_relay].
var server_relay: bool:
	get:
		return inner.server_relay
	set(value):
		inner.server_relay = value

## Maximum reliable replication packet size in bytes.
## Mirrors [member SceneMultiplayer.max_sync_packet_size].
var max_sync_packet_size: int:
	get:
		return inner.max_sync_packet_size
	set(value):
		inner.max_sync_packet_size = value

## Maximum unreliable replication packet size in bytes.
## Mirrors [member SceneMultiplayer.max_delta_packet_size].
var max_delta_packet_size: int:
	get:
		return inner.max_delta_packet_size
	set(value):
		inner.max_delta_packet_size = value

## Emitted when Godot begins authenticating [param peer_id]. Application auth
## code can answer through [method send_auth] and [method complete_auth].
signal peer_authenticating(peer_id: int)

## Emitted when Godot rejects or times out [param peer_id] during
## authentication.
signal peer_authentication_failed(peer_id: int)

## Emitted when a [Node] registers as a session service through
## [method register_service], so the connect kit and game code can react to a
## late-added service instead of scanning the tree for it.
signal service_registered(service: Node)

## Emitted when a service leaves through [method unregister_service].
signal service_unregistered(service: Node)

# Per-session service registry. Lives on the API so any node reaches it through
# node.multiplayer with per-branch scoping for free, and a bare API with no tree
# still answers get_service. See NetwService for the sealed registration base.
var _services: ServiceRegistry = ServiceRegistry.new()

# The connected-peer roster and per-peer participant handles. They live on the
# API so a bare session with no tree still answers get_participant and
# get_peer_context. Participants are keyed by peer id and read their accepted
# join, identity, and context back through this same API.
var _roster: SessionRoster = SessionRoster.new()
var _participants: Dictionary[int, NetwParticipant] = { }

# Weak registry of every constructed extension, backing live_sessions().
# Weakrefs because a strong static list would keep disposed sessions alive.
static var _session_refs: Array[WeakRef] = []

# Memoized backing for [member connect].
var _connect: NetwConnect

# Cache backing for [member root], and the path it was resolved for so a
# root_path change (mount, adopt_inner) self-invalidates without a reset hook.
var _root: Node
var _root_path: NodePath

## The node the replicator roots relative addressing at, resolved from
## [member inner]'s [member SceneMultiplayer.root_path] the same way the native
## replicator resolves its own. In every shipped configuration this is the
## owning [MultiplayerTree], but it stays valid when no tree owns the API. The
## resolved node is cached and re-resolves only if it was freed or
## [member SceneMultiplayer.root_path] changed.
var root: Node:
	get:
		if is_instance_valid(_root) and inner.root_path == _root_path:
			return _root
		_root_path = inner.root_path
		var scene_tree := Engine.get_main_loop() as SceneTree
		_root = scene_tree.root.get_node_or_null(_root_path) if scene_tree else null
		return _root


func _init(inner_api: SceneMultiplayer = null) -> void:
	inner = inner_api if inner_api else SceneMultiplayer.new()
	var adopted_auth_callback := inner.auth_callback
	replication = NetwReplicationInterface.new(self)
	rpc_interface = NetwRpcInterface.new(self, replication)
	clock = NetwClockInterface.new(self)
	lag_compensation = NetwLagCompensationInterface.new(self)
	persistence = NetwPersistenceInterface.new(self)
	liveness = NetwLivenessInterface.new(self)
	interpolation = NetwInterpolationInterface.new(self)
	interest = NetwInterestInterface.new(self)
	session = NetwSessionInterface.new(self)
	scenes = NetwSceneInterface.new(self)
	scenes.local_scene_changed.connect(local_scene_changed.emit)
	session.session_entered.connect(session_entered.emit)
	session.session_ended.connect(session_ended.emit)
	session.session_ended.connect(_on_session_ended)
	session.paused.connect(tree_paused.emit)
	session.unpaused.connect(tree_unpaused.emit)
	session.kicked.connect(kicked.emit)
	auth_callback = adopted_auth_callback
	# Dead routes drop their unreliable-property sequence records so they never
	# outlive the entity they track.
	liveness.entity_dead.connect(replication.clear_route)
	# local_player follows the liveness bus so a root-installed session with no
	# owning MultiplayerTree still tracks the represented entity.
	liveness.entity_live.connect(_on_liveness_entity_live)
	liveness.entity_dead.connect(_on_liveness_entity_dead)
	# The tick pump binds once at construction. The signal lives on the
	# interface, so an inert clock simply never fires it.
	clock.after_tick.connect(_on_clock_tick)
	_bind_inner_signals()
	# A roster row exists for every connected peer, joined or not, so the roster
	# is native peer truth the join frame only enriches. Retiring the row rides
	# the same relayed peer_disconnected the freshness books clear on.
	peer_connected.connect(_ensure_participant_row)
	# Per-peer teardown rides this extension's own relayed peer_disconnected, so a
	# bare API with no owning tree still clears freshness books and RPC state.
	peer_disconnected.connect(_clear_disconnected_peer)
	_session_refs.append(weakref(self))


## True while this extension is the installed [MultiplayerAPI].
##
## A tree-scoped session is active while its owning [MultiplayerTree] holds it as
## multiplayer. A root-installed session (no tree, see
## [method install_as_default]) is active while it is the [SceneTree] default.
func is_active() -> bool:
	var t := root as MultiplayerTree
	if t != null and is_instance_valid(t):
		return t.multiplayer == self
	var loop := Engine.get_main_loop() as SceneTree
	return loop != null and loop.get_multiplayer() == self


## Installs a fresh [NetwMultiplayer] as [param scene_tree]'s default
## [MultiplayerAPI] and returns it, so every node's [member Node.multiplayer]
## resolves to the session and it survives a native scene change.
##
## The session has no [MultiplayerTree]. Spawns anchor at [code]/root[/code], a
## sibling of [member SceneTree.current_scene], so a
## [method SceneTree.change_scene_to_file] that frees the scene leaves the
## session and its replicated content standing. This is the one-session shipping
## mode. Multiplexed processes (the harness, the tiling rig) keep the stock
## default and scope each session to a [MultiplayerTree] subtree instead.
## [codeblock]
## # once at startup (the networked/install_as_default setting does this):
## NetwMultiplayer.install_as_default(get_tree())
## multiplayer.multiplayer_peer = peer   # multiplayer is now the session
## [/codeblock]
static func install_as_default(scene_tree: SceneTree) -> NetwMultiplayer:
	var api := NetwMultiplayer.new()
	api.inner.root_path = ^"/root"
	scene_tree.set_multiplayer(api)
	return api


## Restores a stock [SceneMultiplayer] as [param scene_tree]'s default, undoing
## [method install_as_default]. The installed session is closed and disposed,
## so the provider releases its complete owned graph in one teardown call.
static func uninstall_default(scene_tree: SceneTree) -> void:
	var installed := scene_tree.get_multiplayer() as NetwMultiplayer
	scene_tree.set_multiplayer(SceneMultiplayer.new())
	if installed == null:
		return
	if installed.has_multiplayer_peer():
		installed.multiplayer_peer.close()
		installed.multiplayer_peer = null
	installed.dispose()


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
	if node == null or not node.is_inside_tree():
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
	if not inner.peer_authenticating.is_connected(_on_inner_peer_authenticating):
		inner.peer_authenticating.connect(_on_inner_peer_authenticating)
	if not inner.peer_authentication_failed.is_connected(
		_on_inner_peer_authentication_failed,
	):
		inner.peer_authentication_failed.connect(
			_on_inner_peer_authentication_failed,
		)


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
	if inner.peer_authenticating.is_connected(_on_inner_peer_authenticating):
		inner.peer_authenticating.disconnect(_on_inner_peer_authenticating)
	if inner.peer_authentication_failed.is_connected(
		_on_inner_peer_authentication_failed,
	):
		inner.peer_authentication_failed.disconnect(
			_on_inner_peer_authentication_failed,
		)


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


func _on_inner_peer_authenticating(peer_id: int) -> void:
	peer_authenticating.emit(peer_id)


func _on_inner_peer_authentication_failed(peer_id: int) -> void:
	peer_authentication_failed.emit(peer_id)


## True once [method dispose] has begun a deliberate teardown.
##
## The session machine reads this so a peer this extension closes itself during
## teardown is never mistaken for a spontaneous server crash, which is the only
## drop that ends the session reactively.
func is_disposing() -> bool:
	return _disposing

# Set true the moment dispose() begins, so the session machine can tell a local
# teardown from a server crash. See is_disposing().
var _disposing: bool = false


## Breaks the signal-connection cycles between this extension and the
## [RefCounted] objects it owns ([member inner], [member clock]) so the whole
## group can be released, since a signal connection strong-references its
## target. Called by [MultiplayerTree] when the tree is deleted. The extension
## is unusable afterwards.
func dispose() -> void:
	if _disposing:
		return
	_disposing = true
	auth_callback = Callable()
	if scenes.local_scene_changed.is_connected(local_scene_changed.emit):
		scenes.local_scene_changed.disconnect(local_scene_changed.emit)
	scenes.dispose()
	if session.session_entered.is_connected(session_entered.emit):
		session.session_entered.disconnect(session_entered.emit)
	if session.session_ended.is_connected(session_ended.emit):
		session.session_ended.disconnect(session_ended.emit)
	if session.session_ended.is_connected(_on_session_ended):
		session.session_ended.disconnect(_on_session_ended)
	if session.paused.is_connected(tree_paused.emit):
		session.paused.disconnect(tree_paused.emit)
	if session.unpaused.is_connected(tree_unpaused.emit):
		session.unpaused.disconnect(tree_unpaused.emit)
	if session.kicked.is_connected(kicked.emit):
		session.kicked.disconnect(kicked.emit)
	session.dispose()
	_unbind_inner_signals()
	if clock.after_tick.is_connected(_on_clock_tick):
		clock.after_tick.disconnect(_on_clock_tick)
	if liveness.entity_dead.is_connected(replication.clear_route):
		liveness.entity_dead.disconnect(replication.clear_route)
	if liveness.entity_live.is_connected(_on_liveness_entity_live):
		liveness.entity_live.disconnect(_on_liveness_entity_live)
	if liveness.entity_dead.is_connected(_on_liveness_entity_dead):
		liveness.entity_dead.disconnect(_on_liveness_entity_dead)
	if peer_connected.is_connected(_ensure_participant_row):
		peer_connected.disconnect(_ensure_participant_row)
	if peer_disconnected.is_connected(_clear_disconnected_peer):
		peer_disconnected.disconnect(_clear_disconnected_peer)
	interpolation.dispose()
	replication.dispose()
	rpc_interface.dispose()
	_services.clear()
	clear_roster()
	_root = null


## Replaces [member inner] in place, rebinding this same [NetwMultiplayer]
## installation to a new [SceneMultiplayer]. Used by backends that bring their
## own transport. The extension object, its installation on the tree's branch,
## and every cached [code]NetwMultiplayer.of(node)[/code] reference stay valid
## across the swap.
func adopt_inner(new_inner: SceneMultiplayer) -> void:
	if new_inner == inner:
		return
	if new_inner.auth_callback.is_valid():
		auth_callback = new_inner.auth_callback
	_unbind_inner_signals()
	inner = new_inner
	_bind_inner_signals()
	session.adopt_inner(inner)


## Sends application authentication [param data] to [param peer_id].
##
## Forwards to [method SceneMultiplayer.send_auth]. Networked reserves its own
## framed packets, so application payloads must not use an [AuthProtocol]
## header.
func send_auth(peer_id: int, data: PackedByteArray) -> Error:
	return inner.send_auth(peer_id, data)


## Completes local authentication for [param peer_id].
func complete_auth(peer_id: int) -> Error:
	return inner.complete_auth(peer_id)


## Returns peer ids currently waiting in Godot's authentication phase.
func get_authenticating_peers() -> PackedInt32Array:
	return inner.get_authenticating_peers()


## Disconnects [param peer_id] from the session.
func disconnect_peer(peer_id: int) -> void:
	inner.disconnect_peer(peer_id)


## Clears the wrapped [SceneMultiplayer] replication state.
func clear() -> void:
	inner.clear()


## Sends application bytes through the wrapped [SceneMultiplayer].
func send_bytes(
		bytes: PackedByteArray,
		peer_id: int = 0,
		mode: MultiplayerPeer.TransferMode = MultiplayerPeer.TRANSFER_MODE_RELIABLE,
		channel: int = 0,
) -> Error:
	return inner.send_bytes(bytes, peer_id, mode, channel)

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
var _last_poll_usec: int = 0

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
## caller staging masked-delta rows can key them by the seq that will
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
	var transfer_mode := MultiplayerPeer.TRANSFER_MODE_RELIABLE \
	if reliable else MultiplayerPeer.TRANSFER_MODE_UNRELIABLE
	inner.send_bytes(
		framed,
		peer_id,
		transfer_mode,
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
# promotes peer's masked-lane in-flight rows through the pipeline; a
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
## this as each recipient's confirmed baseline seq.
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
##   ┠╴ masked_frames_out: int      # masked-lane volatile frames sent
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
func _on_session_ended() -> void:
	_clear_session_state.call_deferred()


func _clear_session_state() -> void:
	_unreliable_send_seqs.clear()
	_inbound_freshest_seq.clear()
	_peer_state_ack.clear()
	_last_echoed_seq.clear()
	replication.clear_session()
	rpc_interface.clear_session()


func _clear_disconnected_peer(peer_id: int) -> void:
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
## Emitted when [member local_player] is assigned or cleared.
signal local_player_changed(player: NetwEntity)
## Emitted when [member local_participant] changes [member NetwParticipant.current_scene].
signal local_scene_changed(from: MultiplayerScene, to: MultiplayerScene)
## Emitted on clients when the server notifies it is shutting down.
signal server_disconnecting(reason: String)
## Emitted on the server when a client requests to kick a peer.
signal kick_requested(requester_id: int, target_id: int, reason: String)
## Emitted on the server when a client requests permission to leave.
signal disconnect_requested(peer_id: int, reason: String)
## Emitted on the kicked peer when the server kicks them.
signal kicked(reason: String)
## Emitted on every peer when the game is paused via [method pause].
signal tree_paused(reason: String)
## Emitted on every peer when the game is unpaused via [method unpause].
signal tree_unpaused()
## Emitted when the session reaches
## [constant NetwSessionInterface.State.ONLINE] with its role resolved. Pairs
## with [signal session_ended].
signal session_entered()
## Emitted when the session leaves
## [constant NetwSessionInterface.State.ONLINE]. Pairs with
## [signal session_entered].
signal session_ended()
## Emitted when [member phase] advances. The installing embedding is the only
## caller, through [method settle].
signal phase_changed(phase: Phase)

## The current bootstrap [enum Phase]. Read-only; the installing embedding
## advances it once through [method settle]. Pairs with [signal phase_changed].
var phase: Phase:
	get:
		return _phase

var _phase: Phase = Phase.DECLARING

# The one direct packed level a scoped embedding offers for automatic adoption,
# captured by the provider before settle. Null under a root install.
var _bare_level_candidate: Node


## Records the [param level] a scoped embedding offers as its default single
## scene, consumed by the next [method settle]. The provider captures it
## synchronously so a node dropped in after install never becomes the candidate.
func offer_bare_level(level: Node) -> void:
	_bare_level_candidate = level


## Resolves this session's authored declarations as one ordered step and advances
## [member phase] to [constant LIVE]. The installing embedding calls it once at
## the first idle frame after install: a [MultiplayerTree] defers it from
## [method Node._enter_tree] so the call lands after the enclosing authoring wave,
## and the root autoload calls it on a one-shot [signal SceneTree.process_frame].
## Idempotent, so a call after [constant DECLARING] returns without effect.
func settle() -> void:
	if _phase != Phase.DECLARING:
		return
	_set_phase(Phase.SETTLING)
	_run_settle_resolve()
	_set_phase(Phase.LIVE)


func _set_phase(value: Phase) -> void:
	if _phase == value:
		return
	_phase = value
	phase_changed.emit(_phase)


# The authoring resolution the embedding used to defer piecemeal from
# _enter_tree, now one fixed-order step: adopt the offered bare level (which
# fixes the resolved concurrency), then ensure the host scene view keyed on it.
func _run_settle_resolve() -> void:
	scenes._adopt_bare_level(_bare_level_candidate)
	_bare_level_candidate = null
	scenes._ensure_host_scene_view()

## Pre-game connect / server browser facade over this session's
## [NetwConnector] and [NetwDiscovery]. Built lazily on first access (and
## memoized) so repeated access does not stack signal relays. See [NetwConnect].
var connect: NetwConnect:
	get:
		if _connect:
			return _connect
		_connect = NetwConnect.new(self)
		return _connect

## The session's [NetwInterpolationInterface], pumped every frame from the
## session poll. Owned for the session lifetime, so it needs no scene anchor.
var interpolation: NetwInterpolationInterface


## Returns the [NetwPeerContext] for [param peer_id], creating one on first
## access.
func get_peer_context(peer_id: int) -> NetwPeerContext:
	return _roster.get_peer_context(peer_id)


## Returns [code]true[/code] if a [NetwPeerContext] exists for [param peer_id].
func has_peer_context(peer_id: int) -> bool:
	return _roster.has_peer_context(peer_id)


## Returns the accepted [ResolvedJoin] for [param peer_id], or [code]null[/code].
func get_accepted_join(peer_id: int) -> ResolvedJoin:
	return _roster.get_accepted_join(peer_id)


## Registers [param service] as a session service, keyed by [param type] or its
## own script. Idempotent for the same instance. Fires [signal service_registered].
func register_service(service: Node, type: Script = null) -> void:
	_services.register_service(service, type)
	service_registered.emit(service)


## Unregisters [param service]. Fires [signal service_unregistered].
func unregister_service(service: Node, type: Script = null) -> void:
	_services.unregister_service(service, type)
	service_unregistered.emit(service)


## Returns the service registered for [param type], or [code]null[/code].
func get_service(type: Script) -> Node:
	return _services.get_service(type)


## Returns every registered service whose script is [param base] or a
## subclass of it, in registration order.
func get_services(base: Script) -> Array[Node]:
	return _services.get_services(base)


## Clears the whole service registry. Called during teardown.
func clear_services() -> void:
	_services.clear()


## Retires [param peer_id] from the roster and drops its participant handle.
func forget_peer(peer_id: int) -> void:
	_roster.forget_peer(peer_id)
	_participants.erase(peer_id)


## Clears the connected-peer roster and every participant handle. Called during
## session teardown so a same-session re-host starts from an empty roster.
func clear_roster() -> void:
	_roster.clear()
	_participants.clear()

## All active player identities across all scenes or the sceneless world.
var all_players: Array[NetwEntity]:
	get:
		return scenes.get_all_players()

## Accepted participants known by this peer.
var participants: Array[NetwParticipant]:
	get:
		return get_participants()


## Returns the [NetwParticipant] for [param peer_id], or [code]null[/code].
func participant(peer_id: int) -> NetwParticipant:
	return get_participant(peer_id)


## Returns the [NetwParticipant] for [param peer_id], or [code]null[/code]. A
## participant exists once its peer has an accepted [ResolvedJoin].
func get_participant(peer_id: int) -> NetwParticipant:
	if get_accepted_join(peer_id) == null:
		return null
	if not _participants.has(peer_id):
		_participants[peer_id] = NetwParticipant.new(self, peer_id)
	return _participants[peer_id]


## Every accepted participant known by this peer.
func get_participants() -> Array[NetwParticipant]:
	var result: Array[NetwParticipant] = []
	for rj: ResolvedJoin in _roster.get_accepted_joins():
		var accepted := get_participant(rj.peer_id)
		if accepted:
			result.append(accepted)
	return result

## Every connected peer as a roster row, joined or not.
##
## A row exists from the moment a peer connects. The join frame enriches it with
## a [ResolvedJoin], which is what promotes the row into [member participants]
## and [method get_participant]. This is the whole roster, the un-joined
## observers included.
var connected_participants: Array[NetwParticipant]:
	get:
		var result: Array[NetwParticipant] = []
		for peer_id: int in _participants:
			result.append(_participants[peer_id])
		return result


# Opens a roster row for a freshly connected peer. The row carries only the peer
# id until a join frame enriches it, so an un-joined peer is still a known row.
func _ensure_participant_row(peer_id: int) -> void:
	if not _participants.has(peer_id):
		_participants[peer_id] = NetwParticipant.new(self, peer_id)


## Returns [code]true[/code] if this tree is acting as a listen-server host.
##
## Use this as the single source of truth for all listen-server checks
## instead of comparing [member MultiplayerTree.role] directly.
func is_listen_server() -> bool:
	return session.role == NetwSessionInterface.Role.LISTEN_SERVER

## The original name of the [MultiplayerTree] node.
var tree_name: String:
	get:
		var mt := root as MultiplayerTree
		return mt.get_tree_name() if mt else ""


## Returns [code]true[/code] if the multiplayer peer is in an active connection.
func is_online() -> bool:
	return session.state == NetwSessionInterface.State.ONLINE


## Starts the instance as a network host using [param join_payload].
##
## A tree-scoped session hosts through its [MultiplayerTree], which sources the
## transport scheme from its exported authoring. A root-installed session has no
## tree, so the caller supplies [param config] and the session brings itself up
## as a listen host directly through [method NetwSessionInterface.open_host].
func host(join_payload: JoinPayload, config: NetwHostConfig = null) -> Error:
	var mt := root as MultiplayerTree
	if mt:
		return await mt.host(join_payload)
	if config == null:
		return ERR_UNCONFIGURED
	var prepare_err := await session.prepare_join(join_payload)
	if prepare_err != OK:
		return prepare_err
	var host_err := await session.open_host(config)
	if host_err != OK:
		return host_err
	session.submit_join(join_payload)
	return OK


## Opens the transport against the [param target] address and submits
## [param join_payload] once connected.
##
## See [method MultiplayerTree.join].
func join(
		target: NetwConnectTarget,
		join_payload: JoinPayload,
		timeout: float = 5.0,
		quiet: bool = false,
) -> Error:
	var mt := root as MultiplayerTree
	if mt:
		return await mt.join(target, join_payload, timeout, quiet)
	# Root-installed session: join through the connect kit. The client peer the
	# connector assigns drives the machine to ONLINE through on_peer_assigned and
	# the connected-to-server edge, which submits the prepared join.
	var prepare_err := await session.prepare_join(join_payload)
	if prepare_err != OK:
		return prepare_err
	var attempt := connect.connector().join(target, join_payload)
	if not attempt.is_done():
		await attempt.finished
	var res: NetwConnectResult = attempt.result
	if res == null or not res.is_ok():
		if not quiet:
			Netw.dbg.error(
				"Failed to join: %s",
				[res.message if res else "no transport"],
			)
		return ERR_CANT_CONNECT
	return OK


## Probes the target address. Joins if reachable, hosts otherwise.
##
## A tree-scoped session sources its host [param config] from the tree's exported
## authoring. A root-installed session has no tree, so the caller supplies
## [param config] and the session probes and falls back through the connect kit,
## which drives the winning host or join edge online and admits the local player.
##
## See [method MultiplayerTree.join_or_host].
func join_or_host(
		target: NetwConnectTarget,
		join_payload: JoinPayload,
		config: NetwHostConfig = null,
) -> Error:
	var mt := root as MultiplayerTree
	if mt:
		return await mt.join_or_host(target, join_payload)
	if config == null:
		return ERR_UNCONFIGURED
	var attempt := connect.connector().join_or_host(target, config, join_payload)
	if not attempt.is_done():
		await attempt.finished
	var res: NetwConnectResult = attempt.result
	if res == null or not res.is_ok():
		return ERR_CANT_CONNECT
	return OK

## The current connection state.
var state: NetwSessionInterface.State:
	get:
		return session.state

## The current role in the session.
var role: NetwSessionInterface.Role:
	get:
		return session.role

## Whether the local peer hosts the session, as either a listen or a dedicated
## server. Mirrors [member MultiplayerTree.is_host] but resolves through the
## session, so a root-installed session with no owning [MultiplayerTree] still
## answers.
var is_host: bool:
	get:
		return role == NetwSessionInterface.Role.DEDICATED_SERVER \
				or role == NetwSessionInterface.Role.LISTEN_SERVER

## Whether the local peer plays a client, including a listen-server host that is
## also its own client. Mirrors [member MultiplayerTree.is_local_client] but
## resolves through the session, so a root-installed session with no owning
## [MultiplayerTree] still answers.
var is_local_client: bool:
	get:
		return role == NetwSessionInterface.Role.CLIENT \
				or role == NetwSessionInterface.Role.LISTEN_SERVER

## The local player identity for this session, or [code]null[/code].
##
## Tracked off the liveness bus: the represented entity is the one whose route
## goes live carrying the local peer id, cleared when that route dies. Riding
## the bus rather than a per-registration write drops the clear-and-reset
## flicker a reparent used to cause, since a reparent keeps the route live and
## never emits [signal NetwLivenessInterface.entity_dead].
##
## [signal local_player_changed] fires whenever this member changes.
var local_player: NetwEntity:
	set(value):
		if local_player == value:
			return
		local_player = value
		local_player_changed.emit(value)

## Accepted [NetwParticipant] for this tree, or [code]null[/code].
var local_participant: NetwParticipant:
	get:
		return get_participant(get_unique_id())


# Adopts a newly live entity as local_player when it represents the local peer.
# A session-less peer (no multiplayer_peer) has no local player, matching the
# represented-peer test that treats a null peer as not-local.
func _on_liveness_entity_live(_route: int, entity: NetwEntity) -> void:
	if not has_multiplayer_peer():
		return
	if entity.peer_id == 0 or entity.peer_id != get_unique_id():
		return
	local_player = entity


func _on_liveness_entity_dead(route: int) -> void:
	if local_player and local_player.route == route:
		local_player = null


## Resolves the correct spawn location and causal token for a new player.
func get_spawn_slot(spawner_path: SceneNodePath) -> SpawnSlot:
	var mt := root as MultiplayerTree
	if not mt:
		return SpawnSlot.new()
	return mt.get_spawn_slot(spawner_path)


## Pauses the game on every peer via [code]get_tree().paused = true[/code].
##
## The pause is sent to each connected peer individually.
## [br][br][b]Server Only.[/b]
func pause(reason: String = "") -> void:
	session.pause(reason)


## Unpauses the game on every peer via [code]get_tree().paused = false[/code].
##
## [br][br][b]Server Only.[/b]
func unpause() -> void:
	session.unpause()


## Disconnects [param peer_id] from the session.
##
## If [param reason] is non-empty, the peer receives [signal kicked] before
## the connection is closed.
## [br][br][b]Server Only.[/b]
func kick(peer_id: int, reason: String = "") -> void:
	session.kick(peer_id, reason)


## Asks the server to kick [param peer_id].
##
## The server emits [signal kick_requested] and decides whether to honor it.
## [br][br][b]Player request.[/b]
func request_kick(peer_id: int, reason: String = "") -> void:
	session.request_kick(peer_id, reason)


## Saves game state, closes the multiplayer peer, and waits for the server
## to acknowledge leaving.
func leave() -> void:
	await session.leave()


## Asks the server for permission to leave.
##
## The server emits [signal disconnect_requested] and decides whether to honor it.
## [br][br][b]Player request.[/b]
func request_leave(reason: String = "") -> void:
	session.request_leave(reason)


## Notifies all clients that the server is shutting down.
##
## Clients receive [signal server_disconnecting]. The notice rides
## [constant NetwFrameEnvelope.Channel.SESSION_SHUTDOWN] on
## [method NetwSessionInterface.notify_shutdown], so a root-installed session
## with no [MultiplayerTree] warns its clients through its own verb.
## [br][br][b]Server Only.[/b]
func notify_shutdown(reason: String = "") -> void:
	session.notify_shutdown(reason)

#endregion

func _poll() -> Error:
	var err := inner.poll()
	clock.poll_step()
	liveness.poll()
	_frame_counter += 1
	replication.on_poll()
	# The poll runs once per idle frame, so the wall-clock gap since the last
	# poll is that frame's delta, which drives display smoothing.
	var now_usec := Time.get_ticks_usec()
	var frame_delta := float(now_usec - _last_poll_usec) / 1_000_000.0 \
	if _last_poll_usec > 0 else 0.0
	_last_poll_usec = now_usec
	interpolation.pump(frame_delta)
	rpc_interface.sweep_deferred_calls()
	rpc_interface.sweep_transactions(_receive_tick())
	return err


# Intercepts native @rpc dispatch. A call on a node inside a live NetwEntity is
# upgraded to a route-addressed NetwRpcInterface call, so it inherits liveness
# gating and interest-scoped fan-out and never races the target's spawn edge the
# way a NodePath-addressed native RPC does. Every other call, including
# session-lifecycle RPCs on nodes outside any entity, rides inner unchanged.
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
	if configuration is NetwClockConfig:
		clock.configure(object as MultiplayerClock, configuration as NetwClockConfig)
		clock._configured = true
		return OK
	if configuration is NetwLagCompensationConfig:
		lag_compensation.configure(
			object as LagCompensation,
			configuration as NetwLagCompensationConfig,
		)
		lag_compensation._configured = true
		return OK
	if configuration is NetwSessionConfig:
		session.configure(configuration as NetwSessionConfig)
		return OK
	if configuration is NetwSceneConfig:
		# A scene declaration that lands after the session settled is a scene
		# change, not initial authoring. It is honored so the drop-in path still
		# works, but logged so a late declaration fails loud instead of racing.
		if _phase == Phase.LIVE:
			Netw.dbg.warn(
				"NetwMultiplayer: scene declaration registered after settle is off-contract; authoring is only in-contract while %s.",
				[Phase.keys()[Phase.DECLARING]],
			)
		scenes.configure(configuration as NetwSceneConfig)
		return OK
	# A spawner registration is consumed, not forwarded, so the native
	# replicator never tracks the node and a double spawn is unrepresentable.
	# The node replicates through the Networked pipeline via NetwSpawnerCompat.
	if configuration is MultiplayerSpawner:
		return replication._spawner_compat.consume(
			object as Node,
			configuration as MultiplayerSpawner,
		)
	# A synchronizer registration is consumed, not forwarded, so the native
	# replicator never tracks the node and its sync loop iterates empty sets.
	# The node synchronizes through the Networked pump via NetwSyncCompat.
	if configuration is MultiplayerSynchronizer and object is Node:
		return replication._sync_compat.consume(
			object as Node,
			configuration as MultiplayerSynchronizer,
		)
	return inner.object_configuration_add(object, configuration)


func _object_configuration_remove(object: Object, configuration: Variant) -> Error:
	if configuration is NetwClockConfig:
		clock._configured = false
		return OK
	if configuration is NetwLagCompensationConfig:
		lag_compensation._configured = false
		return OK
	if configuration is NetwSessionConfig:
		session.deconfigure()
		return OK
	if configuration is NetwSceneConfig:
		scenes.deconfigure()
		return OK
	if configuration is MultiplayerSpawner:
		return replication._spawner_compat.consume_remove(
			object as Node,
			configuration as MultiplayerSpawner,
		)
	if configuration is MultiplayerSynchronizer and object is Node:
		return replication._sync_compat.consume_remove(
			object as Node,
			configuration as MultiplayerSynchronizer,
		)
	return inner.object_configuration_remove(object, configuration)


func _set_multiplayer_peer(p_peer: MultiplayerPeer) -> void:
	inner.multiplayer_peer = p_peer
	# The session machine reacts to the one edge every connect path crosses.
	session.on_peer_assigned(p_peer)


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
