## The steady-state half of the replication core owned by [NetwMultiplayer],
## covering per-tick synchronizer pumping, on-demand property and signal
## replication, the per-peer aggregation buffers that flush once per tick, and
## the receive-side dispatch that routes entity traffic by route id.
##
## A Godot RPC is addressed to a node, so it errors when the receiver has not
## spawned that node yet. Entity traffic is addressed to [NetwMultiplayer]
## instead, which exists before any entity does, and [NetwLivenessInterface]
## resolves each frame's route to its [NetwEntity] afterward, so a frame never
## targets a node that has not spawned. Games rarely touch this interface
## directly. They call the [Netw] facade, which encodes arguments and resolves
## this interface for a node.
##
## [br][br][b]Resolve, gate, dispatch[/b]
## [br]An arriving frame resolves its route through [NetwLivenessInterface], and the
## route's [enum NetwLivenessInterface.State] decides its fate. Absence is normal while a
## spawn packet is still in flight, so it is a drop or a short deferral, never an
## error.
## [codeblock]
## route state    arriving frame
## LIVE       ->  dispatch to the resolved node
## UNKNOWN    ->  reliable CALL defers via when_live, otherwise drop (counted)
## LINGERING  ->  drop (counted)
## DEAD       ->  drop (counted)
## [/codeblock]
##
## [b]Authority[/b]
## [br]A discrete channel payload validates its sender on the receiver against
## the target's own script, never against anything on the wire. A frame from an
## unauthorized sender is dropped and counted.
## [codeblock]
## payload           sender check
## entity call       the method's @rpc mode
## variable, signal  its registered write policy
## any               the server is always trusted
## [/codeblock]
##
## [b]Observability[/b]
## [br]The drop-if-absent contract means a spawn or despawn edge produces healthy
## drops. [method NetwMultiplayer.monitor_snapshot] surfaces per-reason drop and
## packet counters so a gap the next snapshot heals stays visible instead of
## assumed.
class_name NetwReplicationInterface
extends RefCounted

# The owning NetwMultiplayer. A weakref because the owner holds this interface
# strongly and both are reference counted.
var _api_ref: WeakRef

var _handlers: Dictionary[int, Callable] = { }
var _defer_channels: Dictionary[int, bool] = { }

var _unreliable_buffers: Dictionary = { } # peer_id -> PackedByteArray
var _reliable_buffers: Dictionary = { } # peer_id -> PackedByteArray

var _drops_unknown_route: int = 0
var _drops_not_live: int = 0
var _drops_no_node: int = 0
# Receiver: comp==255 path was hostile (absolute, parent-relative, or escaped
# the entity subtree). Class 3, security. Sender-facing counters below are the
# quiet-in-logs, loud-in-metrics view of sends that never left this peer.
var _drops_traversal: int = 0
# Receiver: comp==255 path was shape-safe but did not resolve, usually a
# sub-node that has not spawned yet. Class 2, self-healing race.
var _drops_comp_unresolved: int = 0

# Sync half of the replication core: the tick pump, on-demand property/signal
# sends, the datagram freshness books, and the STATE/INPUT/PROPERTY_SYNC/
# SIGNAL receive apply. See NetwSyncPipeline.
var _sync_pipeline: NetwSyncPipeline

# Spawn half of the replication core: the spawn book, the replicate/spawn verb
# tails, the frame codecs, and the receive pipeline. See NetwSpawnPipeline.
var _spawn_pipeline: NetwSpawnPipeline

# Consumes MultiplayerSpawner registrations so a stock project replicates
# through the pipeline instead of the native replicator. See NetwSpawnerCompat.
var _spawner_compat: NetwSpawnerCompat

# Consumes MultiplayerSynchronizer registrations so a stock project
# synchronizes through the sync pump instead of the native replicator. See
# NetwSyncCompat.
var _sync_compat: NetwSyncCompat


func _init(api: NetwMultiplayer) -> void:
	_api_ref = weakref(api) if api else null
	_sync_pipeline = NetwSyncPipeline.new(api)
	_spawn_pipeline = NetwSpawnPipeline.new(api)
	_spawner_compat = NetwSpawnerCompat.new(api)
	_sync_compat = NetwSyncCompat.new(api)


func _api() -> NetwMultiplayer:
	return _api_ref.get_ref() as NetwMultiplayer if _api_ref else null


## Whether the spawn pipeline is synchronously placing a replicated node right
## now. A marked scene's detach hook reads this at [signal Node.tree_entered]
## to tell a framework spawn from a native [method Node.change_scene_to_file].
var is_applying_remote_frame: bool:
	get:
		return _spawn_pipeline._applying_remote_frame


## Registers [param handler] to receive payloads for [param channel].
##
## [param handler] is called as:
## [code]handler(entity: NetwEntity, payload: PackedByteArray, sender: int)[/code].
## If [param defer_when_unknown] is set to [code]true[/code], incoming packets
## for this channel targeting unknown routes will be deferred until the route
## transitions to [constant NetwLivenessInterface.State.LIVE].
func register_channel(
		channel: NetwFrameEnvelope.Channel,
		handler: Callable,
		defer_when_unknown: bool = false,
) -> void:
	_handlers[channel] = handler
	if defer_when_unknown:
		_defer_channels[channel] = true
	else:
		_defer_channels.erase(channel)


## Returns the derived [NetwSyncSetBinding] [param node] declares for
## [param record], or [code]null[/code] when it marks no set of that kind.
func derived_binding(node: Node, record: int) -> NetwSyncSetBinding:
	return _sync_pipeline.derived_binding(node, record)


## Returns [param route]'s derived [NetwSyncSetBinding]s in wire-ordinal order,
## delegating to [method NetwSyncPipeline.derived_group].
func derived_group(route: int) -> Array[NetwSyncSetBinding]:
	var api := _api()
	if not api:
		var none: Array[NetwSyncSetBinding] = []
		return none
	return _sync_pipeline.derived_group(route, api.liveness)


## Forwards [param peer_id]'s advanced datagram ack to
## [method NetwSyncPipeline.note_peer_ack], called by
## [method NetwMultiplayer._note_state_ack] whenever the peer's confirmed seq
## advances, so the masked lane's per-recipient baselines promote.
func note_peer_ack(peer_id: int, acked_seq: int) -> void:
	_sync_pipeline.note_peer_ack(peer_id, acked_seq)


## Sends [param payload] to [param peer_id].
func send_to(
		peer_id: int,
		route: int,
		channel: NetwFrameEnvelope.Channel,
		payload: PackedByteArray,
		reliable: bool,
		comp: int = 0,
		path: String = "",
		batched: bool = false,
) -> void:
	if route < 0:
		return
	var api := _api()
	if not api:
		return
	if api.inner.multiplayer_peer and peer_id == api.get_unique_id():
		_dispatch(route, comp, channel, payload, path, peer_id, reliable)
		return

	var framed := NetwFrameEnvelope.pack(route, comp, channel, payload, path)
	# Aggregation is flushed by the tick pump. Without a running clock there is
	# no flush, so a frame would sit buffered forever. Only batch when the pump
	# is live, otherwise send immediately. Per-tick carriers batch by default;
	# a custom channel opts in through [param batched].
	var should_aggregate := api.clock.is_configured() and (\
			batched \
					or channel in [NetwFrameEnvelope.Channel.PROPERTY_SYNC, NetwFrameEnvelope.Channel.SIGNAL, NetwFrameEnvelope.Channel.SYNC, NetwFrameEnvelope.Channel.SYNC_DELTA])

	if should_aggregate:
		var buffers = _reliable_buffers if reliable else _unreliable_buffers
		var buf: PackedByteArray = buffers.get_or_add(peer_id, PackedByteArray())
		if not reliable:
			var max_size := maxi(128, api.inner.max_sync_packet_size - 150)
			if buf.size() + framed.size() > max_size:
				_flush_buffer(peer_id, false)
				buf = _unreliable_buffers.get_or_add(peer_id, PackedByteArray())
		buf.append_array(framed)
		buffers[peer_id] = buf
	else:
		api.send_packet(peer_id, framed, reliable)


## Fans a control change for [param entity] out to [param peer], reaching every
## peer currently live for the entity's route. Late observers instead learn the
## controller from the spawn packet. The matching decode already lives in this
## dispatcher, so the record never touches the wire for control.
## [br][br][b]Server Only.[/b]
func broadcast_control(entity: NetwEntity, peer: int) -> void:
	var api := _api()
	if not api or not api.inner.multiplayer_peer:
		return
	var w := NetwBitBuffer.Writer.new()
	NetwCodec.put_varint(w, peer)
	var payload := w.to_bytes()
	for peer_id: int in api.liveness.live_peers(entity):
		send_to(
			peer_id,
			entity.route,
			NetwFrameEnvelope.Channel.CONTROL_APPLY,
			payload,
			true,
		)


## Sends [param entity]'s local control request to the server, which arbitrates.
## [br][br][b]Player request.[/b]
func request_control(entity: NetwEntity) -> void:
	send_to(
		MultiplayerPeer.TARGET_PEER_SERVER,
		entity.route,
		NetwFrameEnvelope.Channel.CONTROL_REQUEST,
		PackedByteArray(),
		true,
	)


## Flushes all buffered aggregates.
func flush_all_buffers() -> void:
	for peer_id in _unreliable_buffers.duplicate():
		_flush_buffer(peer_id, false)
	_unreliable_buffers.clear()
	for peer_id in _reliable_buffers.duplicate():
		_flush_buffer(peer_id, true)
	_reliable_buffers.clear()


# Flushes one peer's aggregated buffer, reporting the assigned unreliable seq
# to the sync pipeline so a masked-delta send staged this pass commits
# its pending row under the seq that will carry its acknowledgment. A reliable
# flush or an empty buffer reports nothing, matching send_packet's -1 sentinel.
func _flush_buffer(peer_id: int, reliable: bool) -> void:
	var buffers = _reliable_buffers if reliable else _unreliable_buffers
	var buf: PackedByteArray = buffers.get(peer_id, PackedByteArray())
	if buf.is_empty():
		return
	var api := _api()
	if api:
		var seq := api.send_packet(peer_id, buf, reliable)
		if not reliable and seq >= 0:
			_sync_pipeline.commit_pending_masked(peer_id, seq)
	buffers.erase(peer_id)


## Unpacks a received carrier datagram and dispatches each frame as it is read,
## so a datagram of many aggregated frames never materializes into an
## intermediate array. Called by [NetwMultiplayer] with the sender id and
## transfer mode of the packet. [param seq] is an unreliable datagram's
## freshness stamp, negative for reliable delivery and loopback dispatch,
## which are ordered by construction and never gated.
func receive_carrier(framed_bytes: PackedByteArray, sender: int, reliable: bool = true, seq: int = -1) -> void:
	if framed_bytes.is_empty():
		return
	# Frames of one stream aggregated into the same datagram share its stamp,
	# so the first frame's accept-or-drop verdict holds for all of them. The
	# memo lives per datagram: a duplicated datagram re-evaluates against the
	# books and drops whole.
	var verdicts := { }
	var r := NetwBitBuffer.Reader.new(framed_bytes)
	while r.remaining_bytes() > 0:
		var frame := NetwFrameEnvelope.unpack_next(r)
		if frame.is_empty():
			break
		_dispatch(frame["route"], frame["comp"], frame["channel"], frame["payload"], frame["path"], sender, reliable, seq, verdicts)


# Stamps the frame's sender on the api for the duration of dispatch, so a
# handler reached through the carrier reads it through
# [method MultiplayerAPI.get_remote_sender_id]. Save and restore keeps nested
# dispatch (a handler that loopback-sends another frame) correct.
func _dispatch(
		route: int,
		comp: int,
		channel: NetwFrameEnvelope.Channel,
		payload: PackedByteArray,
		path: String,
		sender: int,
		reliable: bool = true,
		seq: int = -1,
		verdicts: Dictionary = { },
) -> void:
	var api := _api()
	if not api:
		return
	var prev := api._relay_sender
	api._relay_sender = sender
	_dispatch_frame(route, comp, channel, payload, path, sender, reliable, seq, verdicts)
	api._relay_sender = prev


func _dispatch_frame(
		route: int,
		comp: int,
		channel: NetwFrameEnvelope.Channel,
		payload: PackedByteArray,
		path: String,
		sender: int,
		reliable: bool = true,
		seq: int = -1,
		verdicts: Dictionary = { },
) -> void:
	var api := _api()
	if not api:
		return
	var liveness := api.liveness
	var rpc_interface := api.rpc_interface

	if route == 0:
		# Peer-scoped protocol frames carry no entity route.
		match channel:
			NetwFrameEnvelope.Channel.INTEREST_VISIBILITY:
				api.interest._handle_visibility_events(payload, sender)
			NetwFrameEnvelope.Channel.INTEREST_OBSERVER:
				api.interest._handle_observer_events(payload, sender)
			NetwFrameEnvelope.Channel.CLOCK_HANDSHAKE:
				api.clock._handle_handshake(payload, sender)
			NetwFrameEnvelope.Channel.CLOCK_HANDSHAKE_REPLY:
				api.clock._handle_handshake_reply(payload, sender)
			NetwFrameEnvelope.Channel.CLOCK_PING:
				api.clock._handle_ping(payload, sender)
			NetwFrameEnvelope.Channel.CLOCK_PONG:
				api.clock._handle_pong(payload, sender)
			NetwFrameEnvelope.Channel.LAGCOMP_DENY:
				api.lag_compensation._handle_deny(payload, sender)
			NetwFrameEnvelope.Channel.SPAWN:
				_spawn_pipeline._handle_spawn_frame(payload, sender)
			NetwFrameEnvelope.Channel.DESPAWN:
				_spawn_pipeline._handle_despawn_frame(payload, sender)
			NetwFrameEnvelope.Channel.REPARENT:
				_spawn_pipeline._handle_reparent_frame(payload, sender)
			NetwFrameEnvelope.Channel.SESSION_JOIN:
				api.session._handle_join_frame(payload, sender)
			NetwFrameEnvelope.Channel.SESSION_ACCEPT:
				api.session._handle_accept_frame(payload, sender)
			NetwFrameEnvelope.Channel.SESSION_ROSTER:
				api.session._handle_roster_frame(payload, sender)
			NetwFrameEnvelope.Channel.SESSION_PAUSE:
				api.session._handle_pause_frame(payload, sender)
			NetwFrameEnvelope.Channel.SESSION_UNPAUSE:
				api.session._handle_unpause_frame(sender)
			NetwFrameEnvelope.Channel.SESSION_KICKED:
				api.session._handle_kicked_frame(payload, sender)
			NetwFrameEnvelope.Channel.SESSION_SCENE_REQUEST:
				api.scenes._handle_scene_request_frame(payload, sender)
			NetwFrameEnvelope.Channel.SESSION_SCENE_RESULT:
				api.scenes._handle_scene_result_frame(payload, sender)
			_:
				if channel >= 100 and channel <= 254:
					var handler: Callable = _handlers.get(channel, Callable())
					if handler.is_valid():
						handler.call(null, payload, sender)
		return

	# Unreliable route traffic is freshest-wins per stream: a frame applies
	# only when no fresher datagram was already accepted for its
	# (sender, route, channel) stream. A reordered datagram therefore loses
	# only the streams that genuinely have fresher state.
	if seq >= 0 and not _sync_pipeline.accept_unreliable(sender, route, channel, seq, verdicts):
		return

	if channel == NetwFrameEnvelope.Channel.REPLY:
		var r := NetwBitBuffer.Reader.new(payload)
		var txn := NetwCodec.get_safe_varint(r)
		if txn >= 0:
			var values := NetwScriptModel.read_values(r, [], [])
			var value: Variant = values[0] if not values.is_empty() else null
			if value is NetwNodeRef:
				var ref: NetwNodeRef = value
				if ref.route > 0 and liveness.route_state(ref.route) == NetwLivenessInterface.State.UNKNOWN:
					liveness.when_live(
						ref.route,
						func() -> void:
							var ent := liveness.entity_of(ref.route)
							rpc_interface.handle_reply(txn, sender, resolve_comp_node(ent, ref.comp, ref.path) if ent else null)
					)
				else:
					var ent := liveness.entity_of(ref.route)
					rpc_interface.handle_reply(txn, sender, resolve_comp_node(ent, ref.comp, ref.path) if ent else null)
			else:
				rpc_interface.handle_reply(txn, sender, value)
		return

	var state := liveness.route_state(route)
	if state == NetwLivenessInterface.State.UNKNOWN:
		# A reliable call that beat its target's spawn parks until the route
		# binds. An unreliable call is freshest-wins per-tick traffic, so a lost
		# frame is superseded by the next one, never deferred.
		if channel == NetwFrameEnvelope.Channel.CALL and reliable:
			rpc_interface.defer_call(
				sender,
				route,
				func() -> void:
					_dispatch(route, comp, channel, payload, path, sender, reliable)
			)
			return
		_drops_unknown_route += 1
		return
	if state == NetwLivenessInterface.State.DEAD or state == NetwLivenessInterface.State.LINGERING:
		# A frame in flight when the entity despawned. A healthy race, not an
		# error. The counter is the standing signal; the trace is for anyone
		# actively chasing a "why did my call vanish" question.
		_drops_not_live += 1
		if Netw.dbg.is_enabled():
			Netw.dbg.trace("NetwReplicationInterface: dropped channel %d for route %d (%s)", [channel, route, "dead/lingering"])
		return

	var entity := liveness.entity_of(route)
	if not entity or not is_instance_valid(entity.owner):
		_drops_no_node += 1
		return

	var comp_node: Node = entity.owner
	if comp == 255:
		if not NetwRpcInterface._CallRouter.is_path_shape_safe(path):
			# Hostile shape: absolute, parent-relative, or resource path.
			Netw.dbg.warn("NetwReplicationInterface: path traversal clamp rejected '%s' on '%s'", [path, entity.owner.name])
			_drops_traversal += 1
			return
		comp_node = entity.owner.get_node_or_null(path)
		if not is_instance_valid(comp_node):
			# Shape-safe but unresolved, usually a sub-node still spawning.
			_drops_comp_unresolved += 1
			if Netw.dbg.is_enabled():
				Netw.dbg.trace("NetwReplicationInterface: component '%s' on '%s' not resolved yet", [path, entity.owner.name])
			return
		if not (comp_node == entity.owner or entity.owner.is_ancestor_of(comp_node)):
			# Resolved outside the entity subtree. Treat as hostile.
			Netw.dbg.warn("NetwReplicationInterface: component path '%s' escaped entity subtree on '%s'", [path, entity.owner.name])
			_drops_traversal += 1
			return
	elif comp > 0:
		if entity.components.poisoned:
			_drops_no_node += 1
			return
		var rel_path := entity.components.path_for_id(comp)
		if rel_path.is_empty():
			_drops_no_node += 1
			return
		comp_node = entity.owner.get_node_or_null(rel_path)

	if not is_instance_valid(comp_node):
		_drops_no_node += 1
		return

	match channel:
		NetwFrameEnvelope.Channel.CALL:
			rpc_interface.handle_call(entity, comp_node, payload, sender)
		NetwFrameEnvelope.Channel.CONTROL_REQUEST:
			entity._handle_control_request(sender)
		NetwFrameEnvelope.Channel.CONTROL_APPLY:
			var cr := NetwBitBuffer.Reader.new(payload)
			entity._handle_control_apply(NetwCodec.get_safe_varint(cr))
		NetwFrameEnvelope.Channel.PROPERTY_SYNC:
			_sync_pipeline._property_signal_router.handle_property_sync(entity, comp_node, payload, sender)
		NetwFrameEnvelope.Channel.SIGNAL:
			_sync_pipeline._property_signal_router.handle_signal(entity, comp_node, payload, sender)
		NetwFrameEnvelope.Channel.SYNC:
			var s_ordinal := NetwCodec.get_safe_varint(NetwBitBuffer.Reader.new(payload))
			if s_ordinal >= _sync_compat.route_set_count(route):
				_sync_pipeline.handle_derived_sync(entity, s_ordinal, payload, sender)
			else:
				_sync_compat.handle_sync(entity, payload, sender)
		NetwFrameEnvelope.Channel.SYNC_DELTA:
			var d_ordinal := NetwCodec.get_safe_varint(NetwBitBuffer.Reader.new(payload))
			if d_ordinal >= _sync_compat.route_set_count(route):
				_sync_pipeline.handle_derived_delta(entity, d_ordinal, payload, sender)
			else:
				_sync_compat.handle_sync_delta(entity, payload, sender)
		NetwFrameEnvelope.Channel.ACTION:
			var handler: Callable = _handlers.get(channel, Callable())
			if handler.is_valid():
				handler.call(entity, payload, sender)
		_:
			if channel >= 100 and channel <= 254:
				var handler: Callable = _handlers.get(channel, Callable())
				if handler.is_valid():
					handler.call(entity, payload, sender)


## Sends an on-demand property sync for [param property] on [param node].
##
## The server broadcasts to every live peer, a client requests the server. The
## server validates write authority before applying and rebroadcasting.
func send_property(node: Node, property: StringName) -> void:
	_sync_pipeline.send_property(node, property)


## Sends a networked emission of [param signal_name] on [param node] with
## [param args]. Same routing and authority rules as [method send_property].
func send_signal(
		node: Node,
		signal_name: StringName,
		args: Array,
) -> void:
	_sync_pipeline.send_signal(node, signal_name, args)


# Resolves the component addressing for a target node under its entity root.
# Returns the 1-byte component id and, when the table cannot compress it, the
# fallback relative path string. comp 0 is the entity root.
func _resolve_comp(entity: NetwEntity, node: Node) -> Dictionary:
	if node == entity.owner:
		return { "comp": 0, "path": "" }
	var rel := entity.relative_path(entity.owner, node)
	if entity.components.poisoned or not entity.components.has_path(rel):
		return { "comp": 255, "path": String(rel) }
	return { "comp": entity.components.id_for_path(rel), "path": "" }


## Resolves a component addressing pair back to a node under
## [member NetwEntity.owner], the inverse of the [param comp] and [param path]
## produced for a target. [param comp] [code]0[/code] is the entity root, a
## value [code]1[/code] to [code]254[/code] indexes the registered component
## table, and [code]255[/code] falls back to [param path] relative to the root.
##
## Returns [code]null[/code] when the address is unsafe (a [code]255[/code]
## path that is absolute, parent-relative, or escapes the entity subtree),
## unresolved (the child has not spawned), or the component table cannot map
## it. An addressing pair arriving off the wire is untrusted, so the same
## traversal clamp the receive dispatch applies is enforced here.
func resolve_comp_node(entity: NetwEntity, comp: int, path: String) -> Node:
	if comp == 0:
		return entity.owner
	if comp == 255:
		if not NetwRpcInterface._CallRouter.is_path_shape_safe(path):
			return null
		var node := entity.owner.get_node_or_null(path)
		if not is_instance_valid(node):
			return null
		if not (node == entity.owner or entity.owner.is_ancestor_of(node)):
			return null
		return node
	if entity.components.poisoned:
		return null
	var rel_path := entity.components.path_for_id(comp)
	if rel_path.is_empty():
		return null
	var node := entity.owner.get_node_or_null(rel_path)
	return node if is_instance_valid(node) else null


## Pumps every registered binding for [param tick], then flushes the per-peer
## aggregation buffers so a tick's [constant NetwFrameEnvelope.Channel.SYNC]
## and [constant NetwFrameEnvelope.Channel.SYNC_DELTA] frames leave together.
## Driven once per simulation tick by [NetwMultiplayer]'s clock binding.
func on_clock_tick(tick: int) -> void:
	_sync_pipeline.pump(tick)
	_sync_compat.pump()
	flush_all_buffers()
	# After the flush, echo a standalone transport ack to any peer whose freshest
	# inbound datagram no outbound frame this tick piggybacked, so a non-reciprocal
	# masked flow (a client author to a silent observer) still promotes baselines.
	var api := _api()
	if api:
		api.flush_standalone_acks()


## Pumps the consumed synchronizers once per [method MultiplayerAPI.poll] when
## no [MultiplayerClock] is configured, the native per-network-process cadence
## a stock project with zero Networked nodes runs at. With a clock configured
## the consumed pump rides [method on_clock_tick] instead.
func on_poll() -> void:
	var api := _api()
	if not api or api.clock.is_configured():
		return
	_sync_compat.pump()
	flush_all_buffers()
	api.flush_standalone_acks()


## Drops all per-session replication state so no registered sender, aggregation
## buffer, or freshness record outlives its session. Called by
## [NetwMultiplayer] on session teardown.
func clear_session() -> void:
	_unreliable_buffers.clear()
	_reliable_buffers.clear()
	_sync_pipeline.clear_session()
	_spawn_pipeline.clear_session()
	_sync_compat.clear_session()


## Drops [param route]'s datagram freshness books when its [NetwEntity]
## despawns, so per-stream freshness state never outlives the entity it
## tracks.
func clear_route(route: int) -> void:
	_sync_pipeline.clear_route(route)
	_spawn_pipeline.clear_route(route)
	_sync_compat.clear_route(route)


## Drops the freshness state held against [param peer_id] when it disconnects,
## so a reconnecting peer's restarted datagram sequence is accepted fresh.
func clear_peer(peer_id: int) -> void:
	_sync_pipeline.clear_peer(peer_id)
	_sync_compat.clear_peer(peer_id)


## Breaks the mutual strong reference with the internal property and signal
## router so both can be released. Called from [method NetwMultiplayer.dispose].
## The interface is unusable afterward.
func dispose() -> void:
	_sync_pipeline.dispose()


## Returns this interface's contribution to [method NetwMultiplayer.monitor_snapshot].
func counters() -> Dictionary:
	var result := {
		&"drops_unknown_route": _drops_unknown_route,
		&"drops_not_live": _drops_not_live,
		&"drops_no_node": _drops_no_node,
		&"drops_traversal": _drops_traversal,
		&"drops_comp_unresolved": _drops_comp_unresolved,
	}
	result.merge(_sync_pipeline.counters())
	result.merge(_spawn_pipeline.counters())
	result.merge(_spawner_compat.counters())
	result.merge(_sync_compat.counters())
	return result

#region Spawn

## Replicates the orphan [param node] to every connected peer, reconstructing
## it from its [member Node.scene_file_path]. The api-scoped form of
## [method Netw.replicate].
##
## Identity is stamped synchronously before this method returns, so
## [method Node._enter_tree] and [method Node._ready] observe a valid
## [NetwEntity] on every peer. The SPAWN frame is snapshotted at end-of-frame
## of tree entry, so the window between this call and
## [method Node.add_child] is where async hydration belongs.
## [codeblock]
## var player := PlayerScene.instantiate()
## var entity := api.replication.replicate(player, participant)
## await save.hydrate(entity)
## arena.add_child(player)
## [/codeblock]
## [br][br][b]Server Only.[/b]
func replicate(node: Node, owner: NetwParticipant = null) -> NetwEntity:
	return _spawn_pipeline.replicate(node, owner)


## Constructs a node by running the spawn function [param fn] with
## [param args] locally, replicating the same construction to every peer. The
## api-scoped form of [method Netw.spawn].
##
## [param fn] must be registered with [method Netw.configure_spawn] on a host
## object that exists on every peer, must build from its arguments only, and
## must return an orphan [Node]. The returned node is stamped and armed like
## [method replicate]. The caller places it.
## [br][br][b]Server Only.[/b]
func spawn(fn: Callable, args: Array = [], owner: NetwParticipant = null) -> Node:
	return _spawn_pipeline.spawn(fn, args, owner)


## Mints a route for [param root], a node every peer already holds at the same
## tree location, and issues an [constant NetwSpawnBook.Recipe.ADOPT] frame that
## stamps the same identity onto the peers' in-place instances.
##
## No structure is reconstructed, so a node the peers built for themselves gains
## a route and joins replication without a scene spawn. The frame flushes
## synchronously because the node is already placed.
## [codeblock]
## # every peer built the same node at the same path
## var entity := api.replication.adopt_in_place(shared_node)
## [/codeblock]
## [br][br][b]Server Only.[/b]
func adopt_in_place(root: Node) -> NetwEntity:
	return _spawn_pipeline.adopt_in_place(root)


## Returns the spawn-packet contribution of [param root]'s subtree, one entry
## per property marked [method NetwScriptModel.PropertyConfig.on_spawn], in the
## preorder the receiver applies them.
## [codeblock]
## contribution
## ┠╴ node   the Node declaring the property
## ┠╴ prop   the marked property name
## ┖╴ cfg    its NetwScriptModel.SyncConfig
## [/codeblock]
## Used by [method NetwEntity.instantiate_from] to copy a template's marked
## values onto a fresh instance before it enters the tree.
func spawn_state_of(root: Node) -> Array[Dictionary]:
	return _spawn_pipeline._collect_spawn_state(root)


## Returns [code]true[/code] when [param route] is tracked by the spawn
## ledger on this peer, as an issued authority spawn or a received
## materialization. [NetwLivenessInterface] consults this to grant tracked
## roots the end-of-frame reparent grace.
func owns_spawned_route(route: int) -> bool:
	return _spawn_pipeline.owns_spawned_route(route)

#endregion
