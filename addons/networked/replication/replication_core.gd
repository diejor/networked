## The steady-state half of the replication core owned by [NetwMultiplayer],
## covering per-tick synchronizer pumping, on-demand property and signal
## replication, the per-peer aggregation buffers that flush once per tick, and
## the receive-side dispatch that routes entity traffic by route id.
##
## A Godot RPC is addressed to a node, so it errors when the receiver has not
## spawned that node yet. Entity traffic is addressed to [NetwMultiplayer]
## instead, which exists before any entity does, and [NetwMultiplayerCore]
## resolves each frame's route to its [NetwEntity] afterward, so a frame never
## targets a node that has not spawned. Games rarely touch this interface
## directly. They call the [Netw] facade, which encodes arguments and resolves
## this interface for a node.
##
## [br][br][b]Resolve, gate, dispatch[/b]
## [br]An arriving frame resolves its route through [NetwMultiplayerCore], and the
## route's [enum NetwLivenessCore.State] decides its fate. Absence is normal while a
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

# The session's channel book, held by NetwMultiplayerCore so a registration
# outlives this interface's dissolution.
var _channels: NetwChannelBook

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
	_channels = api._native_core.channel_book
	_sync_pipeline = NetwSyncPipeline.new(api)
	_spawn_pipeline = NetwSpawnPipeline.new(api)
	_spawner_compat = NetwSpawnerCompat.new(api)
	_sync_compat = NetwSyncCompat.new(api)
	register_protocol(
		NetwFrameEnvelope.Channel.TABLE,
		_handle_table_frame,
	)
	register_protocol(
		NetwFrameEnvelope.Channel.SPAWN,
		_spawn_pipeline._handle_spawn_frame,
	)
	register_protocol(
		NetwFrameEnvelope.Channel.DESPAWN,
		_spawn_pipeline._handle_despawn_frame,
	)
	register_protocol(
		NetwFrameEnvelope.Channel.REPARENT,
		_spawn_pipeline._handle_reparent_frame,
	)


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
## transitions to [constant NetwLivenessCore.STATE_LIVE].
func register_channel(
		channel: NetwFrameEnvelope.Channel,
		handler: Callable,
		defer_when_unknown: bool = false,
) -> void:
	_channels.register_channel(channel, handler, defer_when_unknown)


## Registers [param handler] to receive payloads for the peer-scoped
## [param channel], which carries no entity route.
##
## [param handler] is called as:
## [code]handler(payload: PackedByteArray, sender: int)[/code]. Each core
## registers its own protocol channels, so a channel's handler crosses with the
## core that owns it rather than through a central table.
func register_protocol(
		channel: NetwFrameEnvelope.Channel,
		handler: Callable,
) -> void:
	_channels.register_protocol(channel, handler)


## Whether every peer-scoped channel the wire declares holds a handler,
## answering [constant @GlobalScope.ERR_UNCONFIGURED] when one does not.
func settle_channels() -> Error:
	return _channels.settle_protocol()


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
	return _sync_pipeline.derived_group(route, api._native_core)


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
	# The tick pump is what flushes an aggregate, so without a running clock a
	# buffered frame would sit there forever.
	var should_aggregate: bool = (
			api._clock.is_configured()
			and api._native_core.channel_aggregates(channel, batched)
	)

	if should_aggregate:
		_note_staged(api, peer_id, api._native_core.carrier_append(
				peer_id, framed, reliable,
		))
	else:
		api._native_core.send_datagram(peer_id, framed, reliable)


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
## [constant NetwLivenessCore.STATE_LIVE] and the peer's committed
## [InterestCore] admission allows it.
##
## The gate lives here because it is a join, and neither half owns the other.
## [NetwMultiplayerCore] reports what a peer [i]has[/i] and
## [InterestCore] decides what it [i]should[/i] see, so the subsystem
## that fans carriers out is the one that asks both.
## [codeblock]
## is_live_for(peer, entity)
## ┠╴ route_state == LIVE          NetwMultiplayerCore
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
	if api._native_core.liveness_state_of(entity) != NetwLivenessCore.STATE_LIVE:
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
	var resolved := entity if entity else NetwEntity.of(node)
	return NetwEntityControl.policy_admits(
		policy,
		sender,
		node.get_multiplayer_authority(),
		resolved.controller if resolved else 0,
	)


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
	var api := _api()
	if api == null:
		return
	var staged: PackedInt64Array = api._native_core.carrier_flush()
	for i in range(0, staged.size(), 2):
		api._note_sent(staged[i], staged[i + 1])


# Reports an assigned unreliable seq to the sync pipeline, so a masked-delta
# send staged this pass commits its pending row under the seq that will carry
# its acknowledgment.
func _note_staged(api: NetwMultiplayer, peer_id: int, seq: int) -> void:
	if seq >= 0:
		api._note_sent(peer_id, seq)


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
	var frames := 0
	while r.remaining_bytes() > 0:
		var frame := NetwFrameEnvelope.unpack_next(r)
		if frame.is_empty():
			return _finish_receive_pass(frames, ERR_INVALID_DATA, sender)
		frames += 1
		var verdict := _dispatch(frame["route"], frame["comp"], frame["channel"], frame["payload"], frame["path"], sender, reliable, seq, verdicts)
		if result == OK and verdict != OK:
			result = verdict
	return _finish_receive_pass(frames, result, sender)


# Reports one receive pass, the mirror of the send pass GATHER reports. The
# count is what a stalled stream is read from: a datagram that carried nothing
# and one that carried frames every gate refused look identical from the
# per-frame rows alone.
func _finish_receive_pass(frames: int, verdict: Error, sender: int) -> Error:
	var api := _api()
	if api:
		api.report_event(
			NetwMultiplayerCore.APPLY,
			0,
			{ frames = frames },
			sender,
			&"",
			{ },
			verdict,
		)
	return verdict


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
	var native_core := api._native_core
	var rpc_interface := api._rpc_core

	if route == 0:
		var protocol := _channels.protocol_handler_of(channel)
		if protocol.is_valid():
			protocol.call(payload, sender)
		elif channel >= 100 and channel <= 254:
			# A user channel is one registration serving both routes, so a
			# peer-scoped frame on it reaches the entity handler with no entity.
			var handler := _channels.handler_of(channel)
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
			var values := NetwCodec.read_values(r, [], [])
			var value: Variant = values[0] if not values.is_empty() else null
			if value is NetwNodeRef:
				var ref: NetwNodeRef = value
				if ref.route > 0 and native_core.liveness_route_state(ref.route) == NetwLivenessCore.STATE_UNKNOWN:
					api.when_live(
						ref.route,
						func() -> void:
							var ent := native_core.wrapper_for_route(ref.route) as NetwEntity
							rpc_interface.handle_reply(txn, sender, resolve_comp_node(ent, ref.comp, ref.path) if ent else null)
					)
				else:
					var ent := native_core.wrapper_for_route(ref.route) as NetwEntity
					rpc_interface.handle_reply(txn, sender, resolve_comp_node(ent, ref.comp, ref.path) if ent else null)
			else:
				rpc_interface.handle_reply(txn, sender, value)
		return

	var state := native_core.liveness_route_state(route)
	if state == NetwLivenessCore.STATE_UNKNOWN:
		if _defers_unknown_route(channel, reliable):
			rpc_interface.defer_call(
				sender,
				route,
				func() -> void:
					_dispatch(route, comp, channel, payload, path, sender, reliable)
			)
			return
		_drops_unknown_route += 1
		return
	if state == NetwLivenessCore.STATE_DEAD or state == NetwLivenessCore.STATE_LINGERING:
		# A frame in flight when the entity despawned. A healthy race, not an
		# error. The counter is the standing signal; the trace is for anyone
		# actively chasing a "why did my call vanish" question.
		_drops_not_live += 1
		if Netw.dbg.is_enabled():
			Netw.dbg.trace("ReplicationCore: dropped channel %d for route %d (%s)", [channel, route, "dead/lingering"])
		return

	var entity := native_core.wrapper_for_route(route) as NetwEntity
	if not entity or not is_instance_valid(entity.owner):
		_drops_no_node += 1
		return

	var comp_node: Node = entity.owner
	match entity.components.classify(comp, path):
		NetwCompTable.ADDRESS_HOSTILE:
			Netw.dbg.warn(
				"ReplicationCore: path traversal clamp rejected '%s' on '%s'",
				[path, entity.owner.name],
			)
			_drops_traversal += 1
			return
		NetwCompTable.ADDRESS_UNMAPPED:
			_drops_no_node += 1
			return
		NetwCompTable.ADDRESS_RELATIVE:
			comp_node = entity.owner.get_node_or_null(path)
			if not is_instance_valid(comp_node):
				# Shape-safe but unresolved, usually a sub-node still spawning.
				_drops_comp_unresolved += 1
				if Netw.dbg.is_enabled():
					Netw.dbg.trace(
						"ReplicationCore: component '%s' on '%s' not "
						+ "resolved yet",
						[path, entity.owner.name],
					)
				return
			if not (
					comp_node == entity.owner
					or entity.owner.is_ancestor_of(comp_node)
			):
				# Resolved outside the entity subtree. Treat as hostile.
				Netw.dbg.warn(
					"ReplicationCore: component path '%s' escaped entity "
					+ "subtree on '%s'",
					[path, entity.owner.name],
				)
				_drops_traversal += 1
				return
		NetwCompTable.ADDRESS_MAPPED:
			comp_node = entity.owner.get_node_or_null(
				entity.components.path_for_id(comp),
			)

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
			_sync_compat.handle_sync(entity, payload, sender)
		NetwFrameEnvelope.Channel.SYNC_ROW:
			_sync_pipeline.handle_derived_row(entity, payload, sender)
		NetwFrameEnvelope.Channel.SYNC_ROW_DELTA:
			_sync_pipeline.handle_retained_row(entity, payload, sender)
		NetwFrameEnvelope.Channel.SYNC_ROW_WINDOW:
			_sync_pipeline.handle_window_row(entity, payload, sender)
		NetwFrameEnvelope.Channel.SYNC_DELTA:
			_sync_compat.handle_sync_delta(entity, payload, sender)
		NetwFrameEnvelope.Channel.PREDICT_COMMAND, \
		NetwFrameEnvelope.Channel.PREDICT_ACK, \
		NetwFrameEnvelope.Channel.PREDICT_RELAY, \
		NetwFrameEnvelope.Channel.PREDICT_RELAY_REQUEST:
			var handler := _channels.handler_of(channel)
			if handler.is_valid():
				handler.call(entity, payload, sender)
		NetwFrameEnvelope.Channel.ACTION:
			var handler := _channels.handler_of(channel)
			if handler.is_valid():
				handler.call(entity, payload, sender)
		_:
			if channel >= 100 and channel <= 254:
				var handler := _channels.handler_of(channel)
				if handler.is_valid():
					handler.call(entity, payload, sender)


# Whether a frame for a route this peer does not know yet waits for it.
#
# A reliable call parks because it beat its target's spawn and nothing will
# repeat it. An unreliable call does not: it is freshest-wins per-tick traffic,
# so a lost frame is superseded by the next one. Any other channel parks exactly
# when its registration asked to, whatever its reliability, because the caller
# that asked owns that trade.
func _defers_unknown_route(
		channel: NetwFrameEnvelope.Channel,
		reliable: bool,
) -> bool:
	if channel == NetwFrameEnvelope.Channel.CALL:
		return reliable
	return _channels.defers(channel)


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
	var gate := -1
	var verdict := OK
	match channel:
		NetwFrameEnvelope.Channel.SYNC:
			gate = NetwMultiplayerCore.GATE_SYNC
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
		NetwFrameEnvelope.Channel.SYNC_ROW, \
		NetwFrameEnvelope.Channel.SYNC_ROW_DELTA, \
		NetwFrameEnvelope.Channel.SYNC_ROW_WINDOW, \
		NetwFrameEnvelope.Channel.SYNC_DELTA:
			gate = NetwMultiplayerCore.GATE_SYNC
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
			gate = NetwMultiplayerCore.GATE_PREDICT
			verdict = api._predict_admit_frame(
				sender,
				route,
				channel,
				payload,
			)
	if gate < 0:
		return OK
	return api._finish_stage_verdict(gate, verdict, route)


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
## Returns [code]null[/code] for every address
## [method NetwComponentMap.classify] refuses and for one it admits: a
## [constant NetwCompTable.ADDRESS_RELATIVE] path that resolves outside the
## entity subtree, which only the resolved node can answer.
func resolve_comp_node(entity: NetwEntity, comp: int, path: String) -> Node:
	if entity == null:
		return null
	return entity.components.resolve_node(entity.owner, comp, path)


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
	api._native_core.table_publish_intake()

	if not api.is_server() or not api.inner.multiplayer_peer:
		return
	var budget := api._native_core.datagram_budget()
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
		api._native_core.table_publish(table)


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
	if api._finish_stage_verdict(
			NetwMultiplayerCore.GATE_TABLE,
			verdict,
			0,
	) != OK:
		return
	var result := api._table_core.apply_frame(payload)
	var bound: PackedInt64Array = result[&"bound"]
	if not bound.is_empty():
		api._native_core.liveness_bind_routes_data(bound)
	var retired: PackedInt64Array = result[&"retired"]
	if not retired.is_empty():
		api._native_core.liveness_tombstone_routes_data(retired)


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
	var budget := api._native_core.datagram_budget()
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
	var api := _api()
	if api:
		api._native_core.carrier_clear()
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
## [NetwEntity] on every peer. The SPAWN frame is snapshotted at the first
## pump after tree entry, so the window between this call and
## [method Node.add_child] is where async hydration belongs, and so is every
## property written before that pump.
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
## tree location, and issues an [constant NetwSpawnBook.RECIPE_ADOPT] frame that
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
	if api and wrapper and api._native_core.liveness_core.entity_is_valid(wrapper.rid):
		return api.spawn_get_state(wrapper.rid)
	return _spawn_pipeline._collect_spawn_state(root)


## Returns [code]true[/code] when [param route] is tracked by the spawn
## ledger on this peer, as an issued authority spawn or a received
## materialization. [NetwMultiplayerCore] consults this to grant tracked
## roots the end-of-frame reparent grace.
func owns_spawned_route(route: int) -> bool:
	return _spawn_pipeline.owns_spawned_route(route)

#endregion
