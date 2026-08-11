## The steady-state half of the replication core owned by [NetwMultiplayer],
## covering per-tick synchronizer pumping, on-demand property and signal
## replication, the per-peer aggregation buffers that flush once per tick, and
## the receive-side dispatch that routes entity traffic by route id.
##
## A Godot RPC is addressed to a node, so it errors when the receiver has not
## spawned that node yet. Entity traffic is addressed to [NetwMultiplayer]
## instead, which exists before any entity does, and [LivenessShell]
## resolves each frame's route to its [NetwEntity] afterward, so a frame never
## targets a node that has not spawned. Games rarely touch this interface
## directly. They call the [Netw] facade, which encodes arguments and resolves
## this interface for a node.
##
## [br][br][b]Resolve, gate, dispatch[/b]
## [br]An arriving frame resolves its route through [LivenessShell], and the
## route's [enum LivenessShell.State] decides its fate. Absence is normal while a
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
## drops. [method NetwMultiplayer.stats_snapshot] surfaces per-reason drop and
## packet counters so a gap the next snapshot heals stays visible instead of
## assumed.
class_name ReplicationCore
extends RefCounted

const RpcCore := preload("res://addons/networked/replication/rpc_core.gd")

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

# Value-only declaration table shared by consumed and derived sync paths.
var sync_model := NetwSyncModel.new()


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
## to tell a framework spawn from a native [method SceneTree.change_scene_to_file].
var is_applying_remote_frame: bool:
	get:
		return _spawn_pipeline._applying_remote_frame


## Registers [param handler] to receive payloads for [param channel].
##
## [param handler] is called as:
## [code]handler(entity: NetwEntity, payload: PackedByteArray, sender: int)[/code].
## If [param defer_when_unknown] is set to [code]true[/code], incoming packets
## for this channel targeting unknown routes will be deferred until the route
## transitions to [constant LivenessShell.State.LIVE].
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


## Returns the derived [NetwPropertySetBinding] [param node] declares for
## [param record], or [code]null[/code] when it marks no set of that kind.
func derived_binding(node: Node, record: int) -> NetwPropertySetBinding:
	return _sync_pipeline.derived_binding(node, record)


## Returns [param route]'s derived [NetwPropertySetBinding]s in wire-ordinal order,
## delegating to [method NetwSyncPipeline.derived_group].
func derived_group(route: int) -> Array[NetwPropertySetBinding]:
	var api := _api()
	if not api:
		var none: Array[NetwPropertySetBinding] = []
		return none
	return _sync_pipeline.derived_group(route, api._liveness)


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
	var should_aggregate := api._clock.is_configured() and (\
			batched \
					or channel in [NetwFrameEnvelope.Channel.PROPERTY_SYNC, NetwFrameEnvelope.Channel.SIGNAL, NetwFrameEnvelope.Channel.SYNC, NetwFrameEnvelope.Channel.SYNC_DELTA, NetwFrameEnvelope.Channel.TABLE])

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
		api._send_packet(peer_id, framed, reliable)


## Fans a control change for [param entity] out to [param peer], reaching every
## peer currently live for the entity's route. Late observers instead learn the
## controller from the spawn packet. The matching decode already lives in this
## dispatcher, so the record never touches the wire for control.
## [br][br][b]Server Only.[/b]
func broadcast_control(entity: NetwEntity, peer: int) -> void:
	var api := _api()
	if not api or not api.inner.multiplayer_peer:
		return
	var w := NetwBitBufferWriter.new()
	NetwCodec.put_varint(w, peer)
	var payload := w.to_bytes()
	for peer_id: int in live_peers(entity):
		send_to(
			peer_id,
			entity.route,
			NetwFrameEnvelope.Channel.CONTROL_APPLY,
			payload,
			true,
		)


## Returns [code]true[/code] when sending [param entity] traffic to
## [param peer_id] is meaningful: the entity is locally
## [constant LivenessShell.State.LIVE] and the peer's committed
## [InterestCore] admission allows it.
##
## The gate lives here because it is a join, and neither half owns the other.
## [LivenessShell] reports what a peer [i]has[/i] and
## [InterestCore] decides what it [i]should[/i] see, so the subsystem
## that fans carriers out is the one that asks both.
## [codeblock]
## is_live_for(peer, entity)
## ┠╴ route_state == LIVE          LivenessShell
## ┖╴ wire_admits(peer, entity)    InterestCore, when a filter exists
## [/codeblock]
## The verdict is a send gate, not a delivery guarantee. It is optimistic by
## construction: a true verdict means the spawn has been issued, not that it has
## been applied, because an unreliable packet can still physically overtake the
## reliable spawn. Receivers keep the other half of the contract, dropping what
## cannot be resolved so the next idempotent snapshot heals the gap.
func is_live_for(peer_id: int, entity: NetwEntity) -> bool:
	var api := _api()
	if not api:
		return true
	if api._liveness.state_of(entity) != LivenessShell.State.LIVE:
		return false
	if peer_id == 1 or peer_id == MultiplayerPeer.TARGET_PEER_SERVER:
		return true

	var interest := api._interest
	if not interest.has_filter(entity):
		return true

	return interest.wire_admits(peer_id, entity)


## Returns [code]true[/code] when [param sender] may author a write for
## [param node] under [param policy].
##
## The receive-side author gate, and the mirror of [method is_live_for]: the
## send gate asks who may receive, this asks who may write. Callers own the
## checks that sit outside the policy table, which is the server's blanket
## trust and any record-kind rule, and consult this only for the policy itself.
## [codeblock]
## policy      admits
## AUTHORITY   the node's multiplayer authority
## CONTROLLER  [member NetwEntity.controller], and nothing when the node
##               carries no [NetwEntity]
## ANY_PEER    everyone
## [/codeblock]
## [param node] resolves its own [NetwEntity], so a caller that already holds
## one may pass it as [param entity] to skip the lookup.
func policy_admits(
		policy: NetwScriptModel.Policy,
		sender: int,
		node: Node,
		entity: NetwEntity = null,
) -> bool:
	match policy:
		NetwScriptModel.Policy.AUTHORITY:
			return sender == node.get_multiplayer_authority()
		NetwScriptModel.Policy.CONTROLLER:
			var resolved := entity if entity else NetwEntity.of(node)
			return resolved != null and sender == resolved.controller
		NetwScriptModel.Policy.ANY_PEER:
			return true
	return false


## Returns every peer currently passing [method is_live_for] for
## [param entity]. On a client this is always the server. This is the recipient
## list carriers fan out to.
func live_peers(entity: NetwEntity) -> Array[int]:
	var api := _api()
	# An offline session self-dispatches through the loopback, and a client sends
	# only to the server, so both resolve to the single local recipient. Only a
	# connected server fans out to the peers live for the route.
	var peer := api.inner.multiplayer_peer
	if peer == null or peer is OfflineMultiplayerPeer or not api.is_server():
		return [1]

	var out: Array[int] = []
	for p in api.get_peers():
		if is_live_for(p, entity):
			out.append(p)
	return out


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
		var seq := api._send_packet(peer_id, buf, reliable)
		if not reliable and seq >= 0:
			api._note_sent(peer_id, seq)
	buffers.erase(peer_id)


## Unpacks a received carrier datagram and dispatches each frame as it is read,
## so a datagram of many aggregated frames never materializes into an
## intermediate array. Called by [NetwMultiplayer] with the sender id and
## transfer mode of the packet. [param seq] is an unreliable datagram's
## freshness stamp, negative for reliable delivery and loopback dispatch,
## which are ordered by construction and never gated.
func receive_carrier(
		framed_bytes: PackedByteArray,
		sender: int,
		reliable: bool = true,
		seq: int = -1,
) -> Error:
	if framed_bytes.is_empty():
		return OK
	# Frames of one stream aggregated into the same datagram share its stamp,
	# so the first frame's accept-or-drop verdict holds for all of them. The
	# memo lives per datagram: a duplicated datagram re-evaluates against the
	# books and drops whole.
	var verdicts := { }
	var r := NetwBitBufferReader.create(framed_bytes)
	var result := OK
	while r.remaining_bytes() > 0:
		var frame := NetwFrameEnvelope.unpack_next(r)
		if frame.is_empty():
			return ERR_INVALID_DATA
		var verdict := _dispatch(frame["route"], frame["comp"], frame["channel"], frame["payload"], frame["path"], sender, reliable, seq, verdicts)
		if result == OK and verdict != OK:
			result = verdict
	return result


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
) -> Error:
	var api := _api()
	if not api:
		return ERR_UNAVAILABLE
	if route != 0:
		var admission := _admit_entity_frame(
			sender,
			route,
			comp,
			channel,
			payload,
		)
		if admission != OK:
			_record_legacy_gate_verdict(admission)
			return admission
	var prev := api._relay_sender
	api._relay_sender = sender
	_dispatch_frame(route, comp, channel, payload, path, sender, reliable, seq, verdicts)
	api._relay_sender = prev
	return OK


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
	var liveness := api._liveness
	var rpc_interface := api._rpc_core

	if route == 0:
		# Peer-scoped protocol frames carry no entity route.
		match channel:
			NetwFrameEnvelope.Channel.INTEREST_AWARENESS:
				api._interest._handle_awareness_events(payload, sender)
			NetwFrameEnvelope.Channel.CLOCK_HANDSHAKE:
				api._clock._handle_handshake(payload, sender)
			NetwFrameEnvelope.Channel.CLOCK_HANDSHAKE_REPLY:
				api._clock._handle_handshake_reply(payload, sender)
			NetwFrameEnvelope.Channel.CLOCK_PING:
				api._clock._handle_ping(payload, sender)
			NetwFrameEnvelope.Channel.CLOCK_PONG:
				api._clock._handle_pong(payload, sender)
			NetwFrameEnvelope.Channel.LAGCOMP_DENY:
				api._lagcomp._handle_deny(payload, sender)
			NetwFrameEnvelope.Channel.TABLE:
				_handle_table_frame(payload, sender)
			NetwFrameEnvelope.Channel.SPAWN:
				_spawn_pipeline._handle_spawn_frame(payload, sender)
			NetwFrameEnvelope.Channel.DESPAWN:
				_spawn_pipeline._handle_despawn_frame(payload, sender)
			NetwFrameEnvelope.Channel.REPARENT:
				_spawn_pipeline._handle_reparent_frame(payload, sender)
			NetwFrameEnvelope.Channel.SESSION_JOIN:
				api._session._handle_join_frame(payload, sender)
			NetwFrameEnvelope.Channel.SESSION_ACCEPT:
				api._session._handle_accept_frame(payload, sender)
			NetwFrameEnvelope.Channel.SESSION_ROSTER:
				api._session._handle_roster_frame(payload, sender)
			NetwFrameEnvelope.Channel.SESSION_PAUSE:
				api._session._handle_pause_frame(payload, sender)
			NetwFrameEnvelope.Channel.SESSION_UNPAUSE:
				api._session._handle_unpause_frame(sender)
			NetwFrameEnvelope.Channel.SESSION_KICKED:
				api._session._handle_kicked_frame(payload, sender)
			NetwFrameEnvelope.Channel.SESSION_SCENE_REQUEST:
				api._scenes._handle_scene_request_frame(payload, sender)
			NetwFrameEnvelope.Channel.SESSION_SCENE_RESULT:
				api._scenes._handle_scene_result_frame(payload, sender)
			NetwFrameEnvelope.Channel.SESSION_SHUTDOWN:
				api._session._handle_shutdown_frame(payload, sender)
			NetwFrameEnvelope.Channel.SESSION_SCENE_RELEASED:
				api._scenes._handle_scene_released_frame(payload, sender)
			NetwFrameEnvelope.Channel.SESSION_KICK_REQUEST:
				api._session._handle_kick_request_frame(payload, sender)
			NetwFrameEnvelope.Channel.SESSION_LEAVE_REQUEST:
				api._session._handle_leave_request_frame(payload, sender)
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
		var r := NetwBitBufferReader.create(payload)
		var txn := NetwCodec.get_safe_varint(r)
		if txn >= 0:
			var values := NetwScriptModel.read_values(r, [], [])
			var value: Variant = values[0] if not values.is_empty() else null
			if value is NetwNodeRef:
				var ref: NetwNodeRef = value
				if ref.route > 0 and liveness.route_state(ref.route) == LivenessShell.State.UNKNOWN:
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
	if state == LivenessShell.State.UNKNOWN:
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
	if state == LivenessShell.State.DEAD or state == LivenessShell.State.LINGERING:
		# A frame in flight when the entity despawned. A healthy race, not an
		# error. The counter is the standing signal; the trace is for anyone
		# actively chasing a "why did my call vanish" question.
		_drops_not_live += 1
		if Netw.dbg.is_enabled():
			Netw.dbg.trace("ReplicationCore: dropped channel %d for route %d (%s)", [channel, route, "dead/lingering"])
		return

	var entity := liveness.entity_of(route)
	if not entity or not is_instance_valid(entity.owner):
		_drops_no_node += 1
		return

	var comp_node: Node = entity.owner
	if comp == 255:
		if not RpcCore._CallRouter.is_path_shape_safe(path):
			# Hostile shape: absolute, parent-relative, or resource path.
			Netw.dbg.warn("ReplicationCore: path traversal clamp rejected '%s' on '%s'", [path, entity.owner.name])
			_drops_traversal += 1
			return
		comp_node = entity.owner.get_node_or_null(path)
		if not is_instance_valid(comp_node):
			# Shape-safe but unresolved, usually a sub-node still spawning.
			_drops_comp_unresolved += 1
			if Netw.dbg.is_enabled():
				Netw.dbg.trace("ReplicationCore: component '%s' on '%s' not resolved yet", [path, entity.owner.name])
			return
		if not (comp_node == entity.owner or entity.owner.is_ancestor_of(comp_node)):
			# Resolved outside the entity subtree. Treat as hostile.
			Netw.dbg.warn("ReplicationCore: component path '%s' escaped entity subtree on '%s'", [path, entity.owner.name])
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
			var cr := NetwBitBufferReader.create(payload)
			entity._handle_control_apply(NetwCodec.get_safe_varint(cr))
		NetwFrameEnvelope.Channel.PROPERTY_SYNC:
			_sync_pipeline._property_signal_router.handle_property_sync(
				entity,
				comp_node,
				payload,
				sender,
				comp,
			)
		NetwFrameEnvelope.Channel.SIGNAL:
			_sync_pipeline._property_signal_router.handle_signal(entity, comp_node, payload, sender)
		NetwFrameEnvelope.Channel.SYNC:
			var s_reader := NetwBitBufferReader.create(payload)
			var s_ordinal := NetwCodec.get_safe_varint(s_reader)
			s_reader.get_aligned_u8()
			if s_ordinal >= _sync_compat.route_set_count(route):
				_sync_pipeline.handle_derived_sync(entity, s_ordinal, payload, sender)
			else:
				_sync_compat.handle_sync(entity, payload, sender)
		NetwFrameEnvelope.Channel.SYNC_DELTA:
			var d_ordinal := NetwCodec.get_safe_varint(NetwBitBufferReader.create(payload))
			if d_ordinal >= _sync_compat.route_set_count(route):
				_sync_pipeline.handle_derived_delta(entity, d_ordinal, payload, sender)
			else:
				_sync_compat.handle_sync_delta(entity, payload, sender)
		NetwFrameEnvelope.Channel.PREDICT_COMMAND, \
		NetwFrameEnvelope.Channel.PREDICT_ACK, \
		NetwFrameEnvelope.Channel.PREDICT_RELAY, \
		NetwFrameEnvelope.Channel.PREDICT_RELAY_REQUEST:
			var handler: Callable = _handlers.get(channel, Callable())
			if handler.is_valid():
				handler.call(entity, payload, sender)
		NetwFrameEnvelope.Channel.ACTION:
			var handler: Callable = _handlers.get(channel, Callable())
			if handler.is_valid():
				handler.call(entity, payload, sender)
		_:
			if channel >= 100 and channel <= 254:
				var handler: Callable = _handlers.get(channel, Callable())
				if handler.is_valid():
					handler.call(entity, payload, sender)


# Runs the hostile-input gate before route resolution for gated entity lanes.
func _admit_entity_frame(
		sender: int,
		route: int,
		comp: int,
		channel: NetwFrameEnvelope.Channel,
		payload: PackedByteArray,
) -> Error:
	var api := _api()
	if not api:
		return ERR_UNAVAILABLE
	var verdict := OK
	match channel:
		NetwFrameEnvelope.Channel.SYNC:
			var reader := NetwBitBufferReader.create(payload)
			NetwCodec.get_safe_varint(reader)
			var flags := reader.get_aligned_u8()
			verdict = api._sync_admit_frame(
				sender,
				route,
				comp,
				channel,
				flags,
				-1,
				payload,
			)
		NetwFrameEnvelope.Channel.SYNC_DELTA:
			verdict = api._sync_admit_frame(
				sender,
				route,
				comp,
				channel,
				0,
				-1,
				payload,
			)
		NetwFrameEnvelope.Channel.PREDICT_COMMAND, \
		NetwFrameEnvelope.Channel.PREDICT_ACK, \
		NetwFrameEnvelope.Channel.PREDICT_RELAY, \
		NetwFrameEnvelope.Channel.PREDICT_RELAY_REQUEST:
			verdict = api._predict_admit_frame(
				sender,
				route,
				channel,
				payload,
			)
	return api._finish_gate_verdict(verdict, route)


# Preserves the relay counters that predate the unified gate verdict stats.
func _record_legacy_gate_verdict(verdict: Error) -> void:
	match verdict:
		ERR_DOES_NOT_EXIST:
			_drops_unknown_route += 1
		ERR_SKIP:
			_drops_not_live += 1
		ERR_UNAVAILABLE:
			_drops_no_node += 1


## Sends an on-demand property sync for [param property] on [param node].
##
## The server broadcasts to every live peer, a client requests the server. The
## server validates write authority before applying and rebroadcasting.
func send_property(node: Node, property: StringName) -> void:
	var api := _api()
	var entity := NetwEntity.of(node)
	if not api or not entity:
		_sync_pipeline.send_property(node, property)
		return
	var target := _resolve_comp(entity, node)
	api._sink_verdict(
		api.sync_send_property(entity.rid, int(target[&"comp"]), property),
		entity.route,
	)


## Sends a networked emission of [param signal_name] on [param node] with
## [param args]. Same routing and authority rules as [method send_property].
func send_signal(
		node: Node,
		signal_name: StringName,
		args: Array,
) -> void:
	var api := _api()
	var entity := NetwEntity.of(node)
	if not api or not entity:
		_sync_pipeline.send_signal(node, signal_name, args)
		return
	var target := _resolve_comp(entity, node)
	api._sink_verdict(
		api.sync_send_signal(
			entity.rid,
			int(target[&"comp"]),
			signal_name,
			args,
		),
		entity.route,
	)


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
	# A nodeless route has no owner to address a component under, and every
	# branch below traverses from one. An address arriving off the wire chooses
	# its own comp, so this is reachable from a remote peer and not only from a
	# local mistake.
	if entity == null or not is_instance_valid(entity.owner):
		return null
	if comp == 0:
		return entity.owner
	if comp == 255:
		if not RpcCore._CallRouter.is_path_shape_safe(path):
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
	pump_tables(tick)
	flush_all_buffers()
	# After the flush, echo a standalone transport ack to any peer whose freshest
	# inbound datagram no outbound frame this tick piggybacked, so a non-reciprocal
	# masked flow (a client author to a silent observer) still promotes baselines.
	var api := _api()
	if api:
		api.flush_standalone_acks()


## Delivers the tick's table traffic in both directions, between the sync pumps
## and the flush so table frames ride the same datagram wave as everything else.
##
## The tick boundary is where a commit becomes bytes and where an applied wave
## becomes [signal NetwMultiplayer.table_received]. Several commits inside one
## tick therefore collapse to the last, and the twenty two frames one commit
## may need produce exactly one emission.
## [codeblock]
## on_clock_tick(tick)
## ┠╴ received   one table_received per table an intake touched
## ┠╴ lifecycle  reliable tombstones, ahead of anything that names them
## ┖╴ dirty      per table: reliable removals, then the upsert frames
## [/codeblock]
## A session with no configured clock never reaches here, so it carries no
## table wire at all, exactly as the derived sync path already behaves.
func pump_tables(tick: int) -> void:
	var api := _api()
	if not api:
		return
	var core := api._table_core
	for table in core.touched_tables():
		api.table_received.emit(table, core.tick_of(table))
	core.begin_intake()

	if not api.is_server() or not api.inner.multiplayer_peer:
		return
	var budget := maxi(128, api.inner.max_sync_packet_size - 150)
	var retired := core.take_lifecycle_removals()
	if not retired.is_empty():
		_broadcast_table_frames(
			TableCore.encode_lifecycle(retired, tick, budget),
			true,
		)
	for table in core.dirty_tables():
		var gone := core.take_pending_removals(table)
		if not gone.is_empty():
			_broadcast_table_frames(
				core.encode_removal(table, gone, tick, budget),
				true,
			)
		_broadcast_table_frames(
			core.encode_frames(table, budget),
			core.is_reliable(table),
		)
		core.clear_dirty(table)
		api.table_received.emit(table, core.tick_of(table))


# Sends one table's frames to every connected peer on route 0.
func _broadcast_table_frames(
		frames: Array[PackedByteArray],
		reliable: bool,
) -> void:
	if frames.is_empty():
		return
	var api := _api()
	if not api:
		return
	for peer_id in api.get_peers():
		for frame in frames:
			send_to(
				peer_id,
				0,
				NetwFrameEnvelope.Channel.TABLE,
				frame,
				reliable,
				0,
				"",
				true,
			)


# Runs the TABLE gate, then applies the frame and binds whatever routes it
# introduced. Row identity is a route like any other, so an unseen one becomes
# a live nodeless entity rather than a number the rest of the addon cannot
# resolve.
func _handle_table_frame(payload: PackedByteArray, sender: int) -> void:
	var api := _api()
	if not api:
		return
	var verdict := api._table_admit_frame(
		sender,
		NetwFrameEnvelope.Channel.TABLE,
		payload,
	)
	if api._finish_gate_verdict(verdict, 0) != OK:
		return
	var result := api._table_core.apply_frame(payload)
	var bound: PackedInt64Array = result[&"bound"]
	if not bound.is_empty():
		api._liveness.bind_routes_data(bound)
	var retired: PackedInt64Array = result[&"retired"]
	if not retired.is_empty():
		api._liveness.tombstone_routes_data(retired)


## Replays every table's committed rows to one peer as an ordered reliable
## snapshot, the late-join heal beside the spawn book's own replay.
##
## The first frame of each table carries
## [constant TableCore.FLAG_SNAPSHOT], so the joiner clears whatever it held and
## the remaining frames apply as ordinary upserts. Ordered delivery is the
## whole assembly protocol.
## [br][br][b]Server Only.[/b]
func replay_tables(peer_id: int) -> void:
	var api := _api()
	if not api or not api.is_server() or not api.inner.multiplayer_peer:
		return
	var core := api._table_core
	var budget := maxi(128, api.inner.max_sync_packet_size - 150)
	for table in core.published_tables():
		for frame in core.encode_frames(table, budget, true):
			send_to(
				peer_id,
				0,
				NetwFrameEnvelope.Channel.TABLE,
				frame,
				true,
				0,
				"",
				true,
			)


## Flushes the per-peer aggregation buffers again once the physics frame's own
## producers have run, so a frame's frames leave in the frame that wrote them.
##
## [method on_clock_tick] flushes what a tick produced, from inside the tick
## loop. A [constant NetwPredict.Schedule.FRAME]
## entity drives after that loop closes, so its owner-lane frames are buffered
## behind a flush that has already run. Without this the wait is not even
## constant: a frame that emits no tick runs no flush at all, so its commands
## leave paired with the next tick's and authority receives two transitions where
## it can only spend one physics step on them.
## [codeblock]
## before_tick_loop   frame opens
##   on_tick          property pump buffers this tick's frames
##   after_tick       on_clock_tick   ──> flush
## after_tick_loop    FRAME entities drive, buffering owner-lane frames
##                    on_frame_end    ──> flush, or they wait for the next tick
## [/codeblock]
## Batching is unaffected. The buffer a peer fills at tick cadence and the one it
## fills at frame cadence belong to different roles — only a predicting owner
## writes owner-lane frames — so on any one peer the second flush finds an empty
## buffer and sends nothing.
func on_frame_end() -> void:
	flush_all_buffers()


## Pumps the consumed synchronizers once per [method MultiplayerAPI.poll] when
## no [MultiplayerClock] is configured, the native per-network-process cadence
## a stock project with zero Networked nodes runs at. With a clock configured
## the consumed pump rides [method on_clock_tick] instead.
func on_poll() -> void:
	var api := _api()
	if not api or api._clock.is_configured():
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
	sync_model.clear()
	_sync_pipeline.clear_session()
	_spawn_pipeline.clear_session()
	_sync_compat.clear_session()


## Drops [param route]'s datagram freshness books when its [NetwEntity]
## despawns, so per-stream freshness state never outlives the entity it
## tracks.
func clear_route(route: int) -> void:
	sync_model.clear_route(route)
	_sync_pipeline.clear_route(route)
	_spawn_pipeline.clear_route(route)
	_sync_compat.clear_route(route)


## Drops the freshness state held against [param peer_id] when it disconnects,
## so a reconnecting peer's restarted datagram sequence is accepted fresh.
func clear_peer(peer_id: int) -> void:
	_sync_pipeline.clear_peer(peer_id)
	_sync_compat.clear_peer(peer_id)


## Breaks the mutual strong reference with the internal property and signal
## router so both can be released. Called from
## [method NetwEmbeddingHandle.dispose].
## The interface is unusable afterward.
func dispose() -> void:
	_sync_pipeline.dispose()
	_sync_compat.dispose()


## Returns this interface's contribution to [method NetwMultiplayer.stats_snapshot].
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
## var entity := api._replication.replicate(player, participant)
## await save.hydrate(entity)
## arena.add_child(player)
## [/codeblock]
## [br][br][b]Server Only.[/b]
func replicate(node: Node, owner: NetwParticipant = null) -> NetwEntity:
	var api := _api()
	var entity := api.replicate(node, owner) if api else RID()
	return api._entity_wrapper(entity) if api else null


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
	var api := _api()
	var entity := api.spawn_fn(fn, args, owner) if api else RID()
	return api.entity_get_node(entity) if api else null


## Registers [param fn] as a host-less spawn constructor under [param id], so
## [method spawn_registered] reconstructs it on every peer with no host node.
func register_spawn_constructor(id: StringName, fn: Callable) -> void:
	var api := _api()
	if api:
		api.spawn_register_constructor(id, fn)


## Constructs a node by running the constructor registered under [param id] with
## [param args] on every peer, returning the local node for the caller to place.
##
## Mirrors [method spawn] but resolves the function from the registry instead of
## a host node, so a session with no host node still spawns.
## [br][br][b]Server Only.[/b]
func spawn_registered(
		id: StringName,
		args: Array = [],
		owner: NetwParticipant = null,
) -> Node:
	var api := _api()
	var entity := api.spawn_registered(id, args, owner) if api else RID()
	return api.entity_get_node(entity) if api else null


## Mints a route for [param root], a node every peer already holds at the same
## tree location, and issues an [constant NetwSpawnBook.Recipe.ADOPT] frame that
## stamps the same identity onto the peers' in-place instances.
##
## No structure is reconstructed, so a node the peers built for themselves gains
## a route and joins replication without a scene spawn. The frame flushes
## synchronously because the node is already placed.
## [codeblock]
## # every peer built the same node at the same path
## var entity := api._replication.adopt_in_place(shared_node)
## [/codeblock]
## [br][br][b]Server Only.[/b]
func adopt_in_place(root: Node) -> NetwEntity:
	var api := _api()
	var entity := api.adopt_in_place(root) if api else RID()
	return api._entity_wrapper(entity) if api else null


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
	var api := _api()
	var wrapper := NetwEntity.of(root)
	if api and wrapper and wrapper.rid.is_valid():
		return api.spawn_get_state(wrapper.rid)
	return _spawn_pipeline._collect_spawn_state(root)


## Returns [code]true[/code] when [param route] is tracked by the spawn
## ledger on this peer, as an issued authority spawn or a received
## materialization. [LivenessShell] consults this to grant tracked
## roots the end-of-frame reparent grace.
func owns_spawned_route(route: int) -> bool:
	return _spawn_pipeline.owns_spawned_route(route)

#endregion
