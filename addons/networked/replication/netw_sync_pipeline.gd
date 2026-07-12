## The steady-state sync half of the replication core owned by
## [NetwReplicationInterface]: the per-tick pump that sends every derived
## [NetwSyncSetBinding], the on-demand [method Netw.sync_property] and
## [method Netw.emit_entity_signal] sends, and the receive-side apply for the
## [constant NetwFrameEnvelope.Channel.SYNC],
## [constant NetwFrameEnvelope.Channel.SYNC_DELTA],
## [constant NetwFrameEnvelope.Channel.PROPERTY_SYNC], and
## [constant NetwFrameEnvelope.Channel.SIGNAL] channels.
##
## The pipeline keys its routing and freshness state by route, never by node
## identity. A binding on the pump is resolved to its [NetwEntity] and route once
## per pass inside [method pump], an inbound payload arrives already resolved to
## its target node by [NetwReplicationInterface]'s dispatch, and the datagram
## freshness books key on route. A record therefore dies with its entity
## through [method clear_route] and never outlives the node it tracked.
## [codeblock]
## two cadences over one carrier, both leaving on the tick flush:
##   TICK       on_clock_tick -> pump -> each authored set's SYNC frame
##              (volatile row) and SYNC_DELTA frames (retained lane)
##   ON_DEMAND  sync_property / emit_entity_signal -> one PROPERTY_SYNC /
##              SIGNAL frame; the server broadcasts, a client asks the server
## [/codeblock]
## Carrier concerns stay on [NetwReplicationInterface]:
## [method NetwReplicationInterface.send_to], the per-peer aggregation buffers,
## and the route-to-node resolution through
## [method NetwReplicationInterface.resolve_comp_node]. This pipeline reaches
## them through the owning interface and owns only the sync traffic itself.
##
## [b]Freshness[/b]
## [br]Every unreliable carrier datagram is stamped with one [code]u16[/code]
## sequence by [method NetwMultiplayer.send_packet], and
## [method accept_unreliable] gates each arriving frame per sender, route, and
## channel stream: a frame applies only when its datagram is fresher than the
## last one accepted for that stream, compared across a half window so
## wraparound stays correct. A reordered datagram therefore loses only the
## streams a fresher datagram already superseded, and a reliable send is
## ordered by the transport and never gated. The books live per route, so
## [method clear_route] drops them the moment the [NetwEntity] despawns.
##
## [b]Authority[/b]
## [br]A received value or signal validates its sender on the receiver against
## the target's own script write policy, never against anything on the wire. The
## server is always trusted, a [constant NetwScriptModel.Policy.AUTHORITY]
## property accepts only the node's [method Node.get_multiplayer_authority], and
## a [constant NetwScriptModel.Policy.CONTROLLER] property accepts only
## [member NetwEntity.controller]. An unauthorized frame is dropped and the value
## never applies.
class_name NetwSyncPipeline
extends RefCounted

# The owning NetwMultiplayer. A weakref because the owner holds this pipeline
# strongly through NetwReplicationInterface and both are reference counted.
var _api_ref: WeakRef

# Derived-set bindings, one NetwSyncSetBinding per state or input set a
# configure_property node declares. Registered on both peers when the node enters
# the tree: the authority pumps its bindings, the receiver resolves an inbound
# frame's ordinal against its own copy. Keyed by node identity through the
# binding's weakref, never by route, so a reparent is followed for free.
var _derived_bindings: Array[NetwSyncSetBinding] = []

# Inbound datagram freshness books, route -> sender -> channel -> last accepted
# u16. Nesting by route first keeps clear_route a single erase, so a stream's
# freshness state dies with its entity.
var _stream_recv_seqs: Dictionary = { }

# route -> { ordinal: schema hash } decoded from a SPAWN frame's derived-set
# descriptor section, validated lazily against a binding the first frame it
# receives. Ordinals are the unified space, so a derived ordinal is offset above
# the route's consumed set count.
var _derived_pending_schema: Dictionary = { }

# Receiver: a SYNC or SYNC_DELTA ordinal above the consumed count named no
# derived binding on this peer.
var _drops_derived_no_set: int = 0
# Receiver: a derived frame's sender is not the set's authorized author.
var _drops_derived_bad_sender: int = 0
# Receiver: a derived set's schema hash disagreed with the spawn descriptor.
var _drops_derived_schema: int = 0
# Receiver: a derived volatile or delta frame applied.
var _derived_frames_in: int = 0

# Receiver: an unreliable frame arrived in a datagram staler than one already
# accepted for its stream. A healthy consequence of reordering, not an error.
var _sync_drops_stale: int = 0

# Sender: a verb targeted a node that belongs to no NetwEntity.
var _sends_dropped_unroutable: int = 0
# Sender: the target entity has no live route on this peer (spawning or dead).
var _sends_dropped_not_live: int = 0

var _property_signal_router: _PropertySignalRouter

# Sender: peer_id -> Array[{binding, row}] staged this pass by a masked-lane
# send (§5.6), pending the seq the tick's flush assigns. Committed into the
# binding's in-flight ring by commit_pending_masked once the flush reports a
# seq, so an overflow-triggered early flush mid-pump and the tick's final
# flush each commit only the rows staged since the last flush for that peer.
var _pending_masked: Dictionary = { }

# Sender: a masked-lane frame actually sent this pass (mask != 0). A caught-up
# peer that costs nothing this pass is not counted.
var _masked_frames_out: int = 0
# Sender: of the above, a frame where every field masked in -- the gain edge
# (an absent baseline) or a coincidental all-fields-changed pass. A high ratio
# against masked_frames_out means the lane is spending mostly on full-row
# heals, the signal that loss or churn is defeating the bandwidth win.
var _masked_frames_full: int = 0


func _init(api: NetwMultiplayer) -> void:
	_api_ref = weakref(api) if api else null
	_property_signal_router = _PropertySignalRouter.new(self)


func _api() -> NetwMultiplayer:
	return _api_ref.get_ref() as NetwMultiplayer if _api_ref else null


# The owning replication interface, which carries send_to and component address
# resolution. Nodes only ever resolve at that carrier edge.
func _repl() -> NetwReplicationInterface:
	var api := _api()
	return api.replication if api else null


## Registers the derived state and input bindings [param node]'s script declares
## through [method Netw.configure_property]. Idempotent per node, and a script
## that marks neither a state nor an input set registers nothing. A marked node
## calls this itself through its own [signal Node.tree_entered], wired by
## [method Netw.configure_property], so both peers register their own copy the
## moment it enters a live session and an inbound frame's ordinal resolves against
## the receiver's binding for the same node.
func register_derived(node: Node) -> void:
	if not is_instance_valid(node):
		return
	var script := node.get_script() as Script
	if not script:
		return
	for binding in _derived_bindings:
		if binding.node() == node:
			return
	var state_set := NetwSyncSet.from_script(script, NetwSyncSet.Record.RECORD_STATE)
	if state_set:
		_derived_bindings.append(NetwSyncSetBinding.new(state_set, node))
		_register_state_timeline(node)
	var input_set := NetwSyncSet.from_script(script, NetwSyncSet.Record.RECORD_INPUT)
	if input_set:
		_derived_bindings.append(NetwSyncSetBinding.new(input_set, node))
	# A broadcast set records into no timeline (the rewind boundary in code), so it
	# calls no timeline hook, unlike the state set above.
	var broadcast_set := NetwSyncSet.from_script(script, NetwSyncSet.Record.RECORD_BROADCAST)
	if broadcast_set:
		_derived_bindings.append(NetwSyncSetBinding.new(broadcast_set, node))


# State-set presence is the rewind trigger: on the server, register the entity so
# the lag compensation service records its authoritative history every tick, even
# without a prediction component. A client and a session with no lag compensation
# mounted register nothing.
func _register_state_timeline(node: Node) -> void:
	var api := _api()
	if not api or api.get_unique_id() != 1:
		return
	var sim := api.lag_compensation
	var entity := NetwEntity.of(node)
	if sim and entity:
		sim.register_timeline(entity)


# Drops the entity's rewind history when its state set unregisters, unless it is
# preserving history across a reparent (a lingering despawn keeps its window).
func _unregister_state_timeline(node: Node) -> void:
	var api := _api()
	if not api or api.get_unique_id() != 1:
		return
	var entity := NetwEntity.of(node)
	if not entity or (entity.reparenting and entity.reparenting.preserve_history):
		return
	var sim := api.lag_compensation
	if sim:
		sim.unregister_timeline(entity)


## Returns the derived [NetwSyncSetBinding] [param node] declares for
## [param record] ([constant NetwSyncSet.Record.RECORD_STATE],
## [constant NetwSyncSet.Record.RECORD_INPUT], or
## [constant NetwSyncSet.Record.RECORD_BROADCAST]), or [code]null[/code] when the
## node marks no set of that kind. This is the set handle a prediction engine
## gathers, applies, and stamps through, the registry replacement for the
## synchronizer node.
func derived_binding(node: Node, record: int) -> NetwSyncSetBinding:
	for binding in _derived_bindings:
		if binding.node() == node and binding.set.record == record:
			return binding
	return null


## Drops every derived binding declared on [param node], releasing its rewind
## history when a state set unregisters. Idempotent.
func unregister_derived(node: Node) -> void:
	var had_state := false
	for i in range(_derived_bindings.size() - 1, -1, -1):
		if _derived_bindings[i].node() == node:
			if _derived_bindings[i].set.record == NetwSyncSet.Record.RECORD_STATE:
				had_state = true
			_derived_bindings.remove_at(i)
	if had_state:
		_unregister_state_timeline(node)


# Drops derived bindings whose node has freed.
func _prune_derived() -> void:
	for i in range(_derived_bindings.size() - 1, -1, -1):
		if not is_instance_valid(_derived_bindings[i].node()):
			_derived_bindings.remove_at(i)


## Pumps every derived [constant NetwSyncSet.Cadence.TICK] binding for
## [param tick] onto the shared [constant NetwFrameEnvelope.Channel.SYNC] and
## [constant NetwFrameEnvelope.Channel.SYNC_DELTA] lanes. The per-peer flush
## that follows is a carrier concern owned by [NetwReplicationInterface].
func pump(tick: int) -> void:
	var api := _api()
	var liveness := api.liveness if api else null
	if not liveness:
		return
	var repl := _repl()
	if not repl:
		return
	_pump_derived(tick, liveness, repl)


# Pumps every active derived binding: each set's volatile row on the shared
# SYNC frame and its retained lane on the per-recipient SYNC_DELTA. A set's
# ordinal is K plus its index in the route's derived group, where K is the
# route's consumed set count, so a receiver splits an inbound ordinal at the
# same boundary. The author gate follows the set's record and policy (the server
# for a state set, the entity controller for an input set) and the recipients
# follow its audience (every admitted peer for a public set, the server alone
# for a server-only input set).
func _pump_derived(
		tick: int,
		liveness: NetwLivenessInterface,
		repl: NetwReplicationInterface,
) -> void:
	_prune_derived()
	for binding: NetwSyncSetBinding in _derived_bindings.duplicate():
		var node := binding.node()
		if not is_instance_valid(node) or not node.is_inside_tree():
			continue
		var entity := NetwEntity.of(node)
		if not entity:
			continue
		var route := liveness.route_of(entity)
		if route <= 0:
			continue
		if not _derived_author_ok(binding, entity, node):
			continue
		var recipients := _derived_recipients(binding, entity)
		if recipients.is_empty():
			continue

		var ordinal := repl._sync_compat.route_set_count(route) \
				+ derived_group(route, liveness).find(binding)

		# A prediction engine authors the frame's tick and reconciliation ack
		# through the binding. Absent an engine, the frame stamps the pump's tick
		# and the -1 no-input sentinel, the plain-state cadence.
		var frame_tick := binding.authored_tick if binding.authored_tick >= 0 else tick
		if binding.set.masked:
			# A masked set's mask differs by recipient (each peer's own confirmed
			# baseline), so the frame cannot be shared like the plain volatile row
			# below; it is computed and sent per recipient.
			for peer_id in recipients:
				var masked := binding.masked_delta(
					ordinal, peer_id, frame_tick, binding.reconcile_ack,
				)
				if masked.is_empty():
					break
				var masked_bytes: PackedByteArray = masked["bytes"]
				if masked_bytes.is_empty():
					continue
				repl.send_to(
					peer_id, route, NetwFrameEnvelope.Channel.SYNC,
					masked_bytes, false, 0, "", true,
				)
				_stage_pending_masked(peer_id, binding, masked["row"])
				_masked_frames_out += 1
				if masked["full"]:
					_masked_frames_full += 1
			# A confirmed baseline (or in-flight row) held against a peer no longer
			# a recipient must not survive to its next admission, so absence heals
			# the full masked row on gain, matching the retained lane's rule below.
			binding.retain_masked_baselines(recipients)
		else:
			var bytes := binding.encode_volatile(ordinal, frame_tick, binding.reconcile_ack)
			if not bytes.is_empty():
				for peer_id in recipients:
					repl.send_to(
						peer_id, route, NetwFrameEnvelope.Channel.SYNC,
						bytes, false, 0, "", true,
					)

		binding.poll_retained()
		for peer_id in recipients:
			var delta := binding.retained_delta(ordinal, peer_id)
			if not delta.is_empty():
				repl.send_to(
					peer_id, route, NetwFrameEnvelope.Channel.SYNC_DELTA,
					delta, true, 0, "", true,
				)
		# A baseline held against a peer no longer a recipient must not survive to
		# its next admission, so absence heals the full retained row.
		binding.retain_baselines(recipients)


# True when the local peer may author [param binding]'s stream this pass: the
# node authority for a set policed by the authority, the entity controller for a
# set policed by the controller, any peer for an open set. A RECORD_STATE set is
# always server-authored regardless of policy because it sits inside the rewind
# boundary: it feeds the server-authoritative state timeline, and entity control
# stamps the controller's authority onto the body, so following node authority
# would hand the state stream to the controlling client (the invariant the state
# synchronizer's forced authority 1 used to hold). A RECORD_BROADCAST set is
# outside that boundary, a stream trusted outright, so it falls to its policy.
func _derived_author_ok(
		binding: NetwSyncSetBinding,
		entity: NetwEntity,
		node: Node,
) -> bool:
	var api := _api()
	if binding.set.record == NetwSyncSet.Record.RECORD_STATE:
		return api != null and api.get_unique_id() == 1
	match binding.set.policy:
		NetwScriptModel.Policy.AUTHORITY:
			return node.is_multiplayer_authority()
		NetwScriptModel.Policy.CONTROLLER:
			return api != null and api.get_unique_id() == entity.controller
		NetwScriptModel.Policy.ANY_PEER:
			return true
	return false


# The recipients for [param binding] this pass. A server-only set reaches the
# server alone (nothing when the server itself authors it), a public set reaches
# every peer the entity is live for, both excluding the local author.
func _derived_recipients(
		binding: NetwSyncSetBinding,
		entity: NetwEntity,
) -> Array[int]:
	var api := _api()
	if not api:
		return []
	var local_id := api.get_unique_id()
	var out: Array[int] = []
	if binding.set.audience == NetwSyncSet.Audience.AUDIENCE_SERVER_ONLY:
		if local_id != 1:
			out.append(1)
		return out
	for peer_id in api.liveness.live_peers(entity):
		if peer_id != local_id:
			out.append(peer_id)
	return out


## A route's derived bindings in wire-ordinal order, sorted by the declaring
## node's path from the entity owner and then the record kind, so a node
## declaring both a state and an input set orders them stably. Structural rather
## than registration order, so both peers derive the same ordinals. The ordinal
## on the wire is [method Ne`twSyncCompat.route_set_count] plus a binding's index
## in this group.
func derived_group(
		route: int,
		liveness: NetwLivenessInterface,
) -> Array[NetwSyncSetBinding]:
	var keyed: Array = []
	for binding in _derived_bindings:
		var node := binding.node()
		if not is_instance_valid(node):
			continue
		var entity := NetwEntity.of(node)
		if not entity or liveness.route_of(entity) != route:
			continue
		if not is_instance_valid(entity.owner):
			continue
		keyed.append([
			String(entity.owner.get_path_to(node)),
			binding.set.record,
			binding,
		])
	keyed.sort_custom(func(a: Array, b: Array) -> bool:
		if a[0] != b[0]:
			return a[0] < b[0]
		return a[1] < b[1])
	var out: Array[NetwSyncSetBinding] = []
	for entry: Array in keyed:
		out.append(entry[2])
	return out


# Stages [param row] for [param binding]'s masked send to [param peer_id] this
# pass, pending the seq the tick's flush assigns (§5.6).
func _stage_pending_masked(peer_id: int, binding: NetwSyncSetBinding, row: Dictionary) -> void:
	var list: Array = _pending_masked.get_or_add(peer_id, [])
	list.append({"binding": binding, "row": row})


## Commits every masked row staged for [param peer_id] this pass into its
## binding's in-flight ring under [param seq], the datagram seq
## [method NetwReplicationInterface._flush_buffer] just assigned to the send
## that carries it. Called once per unreliable flush, so an overflow-triggered
## early flush mid-pump and the tick's final flush each commit only the rows
## staged since the last flush for that peer.
func commit_pending_masked(peer_id: int, seq: int) -> void:
	var list: Array = _pending_masked.get(peer_id, [])
	if list.is_empty():
		return
	for entry: Dictionary in list:
		(entry["binding"] as NetwSyncSetBinding).commit_masked_pending(
			peer_id, seq, entry["row"],
		)
	_pending_masked.erase(peer_id)


## Promotes [param peer_id]'s confirmed masked baseline on every derived binding
## when [method NetwMultiplayer.peer_state_ack] advances for it. A binding that
## never staged a masked send for this peer no-ops.
func note_peer_ack(peer_id: int, acked_seq: int) -> void:
	for binding in _derived_bindings:
		binding.advance_masked_ack(peer_id, acked_seq)


## Sends an on-demand property sync for [param property] on [param node].
##
## The server broadcasts to every live peer, a client requests the server. The
## server validates write authority before applying and rebroadcasting.
func send_property(node: Node, property: StringName) -> void:
	var frame := _resolve_send(node)
	if frame.is_empty():
		var entity := NetwEntity.of(node)
		if not entity and not node.has_meta(&"_netw_unroutable_warned"):
			node.set_meta(&"_netw_unroutable_warned", true)
			Netw.dbg.warn(
				"sync_property: Node '%s' is not part of any NetwEntity.",
				[node.name],
				func(m): push_warning(m)
			)
		return

	if not (property in node):
		Netw.dbg.warn(
			"sync_property: Property '%s' does not exist on '%s'.",
			[property, node.name],
			func(m): push_warning(m)
		)
		return

	var script := node.get_script() as Script
	var opt := (
			NetwScriptModel.get_property_config(script, property)
			if script
			else null
	)
	if not opt:
		# Zero-Setup Sender Authority Check: only authority can send.
		var api := _api()
		var local_id := api.get_unique_id() if api else 1
		if local_id != 1 and local_id != node.get_multiplayer_authority():
			Netw.dbg.warn(
				"sync_property: Non-authority peer cannot "
				+ "sync unregistered property '%s'.",
				[property],
				func(m): push_warning(m)
			)
			return

	var set := NetwSyncSet.from_property_config(property, opt)
	var reliable := set.reliable

	# Write to binary buffer using codec
	var w := NetwBitBuffer.Writer.new()

	# 1. Write property token (1-byte id when the table is intact, else name)
	NetwScriptModel.write_token(
		w,
		_encode_prop_val(frame["entity"], node, property),
	)

	# 2. Write value. Freshness for an unreliable send rides the carrier
	# datagram's sequence stamp, so the payload carries none of its own.
	var val = node.get(property)
	var quantizer: NetwQuantize = set.fields[0].quantizer
	var type := NetwScriptModel.get_node_property_type(node, property)
	NetwScriptModel.write_values(w, [val], [quantizer], [type])

	_send_entity_event(frame, set.channel, w.to_bytes(), reliable)


## Sends a networked emission of [param signal_name] on [param node] with
## [param args]. Same routing and authority rules as [method send_property].
func send_signal(
		node: Node,
		signal_name: StringName,
		args: Array,
) -> void:
	var frame := _resolve_send(node)
	if frame.is_empty():
		var entity := NetwEntity.of(node)
		if not entity and not node.has_meta(&"_netw_unroutable_warned"):
			node.set_meta(&"_netw_unroutable_warned", true)
			Netw.dbg.warn(
				"send_signal: Node '%s' is not part of any NetwEntity.",
				[node.name],
				func(m): push_warning(m)
			)
		return

	if not node.has_signal(signal_name):
		Netw.dbg.warn(
			"send_signal: Signal '%s' does not exist on '%s'.",
			[signal_name, node.name],
			func(m): push_warning(m)
		)
		return

	var script := node.get_script() as Script
	var opt := (
			NetwScriptModel.get_signal_config(script, signal_name)
			if script
			else null
	)
	var reliable := true
	var is_call_local := true
	if opt:
		reliable = opt.transfer_mode == NetwScriptModel.TransferMode.RELIABLE
		is_call_local = opt.is_call_local
	else:
		# Zero-Setup Sender Authority Check: only authority can send.
		var api := _api()
		var local_id := api.get_unique_id() if api else 1
		if local_id != 1 and local_id != node.get_multiplayer_authority():
			Netw.dbg.warn(
				"send_signal: Non-authority peer cannot "
				+ "emit unregistered signal '%s'.",
				[signal_name],
				func(m): push_warning(m)
			)
			return

	if is_call_local:
		var callable := Callable(node, &"emit_signal")
		callable.callv([signal_name] + args)

	# Write to binary buffer using codec
	var w := NetwBitBuffer.Writer.new()

	# 1. Write signal token (1-byte id when the table is intact, else name)
	NetwScriptModel.write_token(
		w,
		_encode_signal_val(frame["entity"], node, signal_name),
	)

	# 2. Write args
	var quantizers := opt.quantizers if opt else []
	var types := (
			NetwScriptModel.get_signal_arg_types(script, signal_name)
			if script
			else []
	)
	NetwScriptModel.write_values(w, args, quantizers, types)

	_send_entity_event(frame, NetwFrameEnvelope.Channel.SIGNAL, w.to_bytes(), reliable)


# Resolves the entity, route, and component addressing for an on-demand event
# send. Returns an empty dictionary when the node is unroutable.
func _resolve_send(node: Node) -> Dictionary:
	var entity := NetwEntity.of(node)
	if not entity:
		_sends_dropped_unroutable += 1
		return { }
	var api := _api()
	var liveness := api.liveness if api else null
	if not liveness:
		return { }
	var route := liveness.route_of(entity)
	if route <= 0:
		_sends_dropped_not_live += 1
		return { }
	var repl := _repl()
	var target := repl._resolve_comp(entity, node) if repl else { "comp": 0, "path": "" }
	return {
		"entity": entity,
		"route": route,
		"comp": target["comp"],
		"path": target["path"],
	}


func _send_entity_event(
		frame: Dictionary,
		channel: NetwFrameEnvelope.Channel,
		payload: PackedByteArray,
		reliable: bool,
) -> void:
	var api := _api()
	var repl := _repl()
	if not repl:
		return
	var mt := api.tree if api else null
	if mt and mt.is_host:
		var liveness := api.liveness
		for recipient in liveness.live_peers(frame["entity"]):
			repl.send_to(recipient, frame["route"], channel, payload, reliable, frame["comp"], frame["path"])
	else:
		var local_id := api.get_unique_id() if api else 0
		if local_id != 1:
			repl.send_to(
				1,
				frame["route"],
				channel,
				payload,
				reliable,
				frame["comp"],
				frame["path"],
			)


# Returns the wire property token: a 1-byte id when the table is intact,
# otherwise the property name. The id table divergence hash covers property
# names, so a poisoned table falls back to names on both peers.
func _encode_prop_val(
		entity: NetwEntity,
		node: Node,
		property: StringName,
) -> Variant:
	var script := node.get_script() as Script
	if script and entity and not entity.components.poisoned:
		var pid := NetwScriptModel.get_property_id(script, property)
		if pid > 0:
			return pid
	return property


# Returns the wire signal token: a 1-byte id when the table is intact, otherwise
# the signal name. Mirrors [method _encode_prop_val].
func _encode_signal_val(
		entity: NetwEntity,
		node: Node,
		signal_name: StringName,
) -> Variant:
	var script := node.get_script() as Script
	if script and entity and not entity.components.poisoned:
		var sid := NetwScriptModel.get_signal_id(script, signal_name)
		if sid > 0:
			return sid
	return signal_name


## Decides whether an unreliable frame stamped with datagram sequence
## [param seq] may apply for its (sender, route, channel) stream. Acceptance is
## freshest-wins per stream: the first frame a stream is ever seen with is
## accepted, afterward only a fresher stamp is, compared across a
## [code]u16[/code] half window so wraparound stays correct.
##
## [param verdicts] carries one datagram's per-stream verdicts, so every frame
## of a stream aggregated into the same datagram shares the first frame's
## outcome, while a duplicated datagram re-evaluates against the book and drops
## whole. [NetwReplicationInterface]'s dispatch owns that memo's lifetime.
func accept_unreliable(
		sender: int,
		route: int,
		channel: NetwFrameEnvelope.Channel,
		seq: int,
		verdicts: Dictionary,
) -> bool:
	var key := Vector3i(route, sender, channel)
	if verdicts.has(key):
		return verdicts[key]
	var by_sender: Dictionary = _stream_recv_seqs.get_or_add(route, { })
	var by_channel: Dictionary = by_sender.get_or_add(sender, { })
	var accepted := true
	if by_channel.has(channel):
		var last := int(by_channel[channel])
		accepted = seq != last and ((seq - last) & 0xFFFF) < 32768
	if accepted:
		by_channel[channel] = seq
	else:
		_sync_drops_stale += 1
	verdicts[key] = accepted
	return accepted


#region Derived-set frames

## Gathers [param node]'s [constant NetwSyncSet.Lane.VOLATILE] field values for
## [param set] and encodes the [constant NetwFrameEnvelope.Channel.SYNC] frame for
## [param ordinal], stamping [param tick] and [param ack] per the set's
## [member NetwSyncSet.stamp]. A state set frames [code]STAMPED | ACKED[/code], a
## plain tick set frames [code]STAMPED[/code], and a set with no stamp frames the
## bare positional row. Returns an empty array when a field is missing on the
## node, so a half row never crosses the wire.
static func encode_volatile_frame(
		node: Node,
		set: NetwSyncSet,
		ordinal: int,
		tick: int,
		ack: int,
) -> PackedByteArray:
	var gathered := gather_volatile(node, set)
	if not gathered[0]:
		return PackedByteArray()
	return NetwFrameEnvelope.encode_sync_frame({
		"ordinal": ordinal,
		"flags": volatile_flags(set),
		"values": gathered[1],
		"quantizers": gathered[2],
		"types": gathered[3],
		"tick": tick,
		"ack": ack,
	})


## Reads [param node]'s [constant NetwSyncSet.Lane.VOLATILE] field values for
## [param set], returning [code][ok, values, quantizers, types][/code] with
## [code]ok[/code] false when a field is missing on the node, so a caller drops
## the whole pass rather than send a half row. The volatile-frame encoder and the
## windowed input gather both walk the row through here.
static func gather_volatile(node: Node, set: NetwSyncSet) -> Array:
	var values: Array = []
	var quantizers: Array = []
	var types: Array = []
	for field in set.fields:
		if field.lane != NetwSyncSet.Lane.VOLATILE:
			continue
		if not (field.key in node):
			return [false, [], [], []]
		var value = node.get(field.key)
		values.append(value)
		quantizers.append(field.quantizer)
		types.append(typeof(value))
	return [true, values, quantizers, types]


## Returns the scalar-header SYNC flags for [param set]'s
## [member NetwSyncSet.stamp]: [code]0[/code] plain, [code]STAMPED[/code] for a
## tick set, [code]STAMPED | ACKED[/code] for a state set. The windowed input
## flag is added by the binding, which owns the sample ring.
static func volatile_flags(set: NetwSyncSet) -> int:
	match set.stamp:
		NetwSyncSet.Stamp.STAMP_TICK:
			return NetwFrameEnvelope.SYNC_FLAG_STAMPED
		NetwSyncSet.Stamp.STAMP_TICK_ACK:
			return NetwFrameEnvelope.SYNC_FLAG_STAMPED | NetwFrameEnvelope.SYNC_FLAG_ACKED
	return 0


## Decodes a [constant NetwFrameEnvelope.Channel.SYNC] frame's [param payload]
## against [param set] and, when [param write] is true, writes each decoded
## [constant NetwSyncSet.Lane.VOLATILE] value onto [param node]. Returns the
## decoded header [code]{ordinal, tick, ack, payload}[/code] where
## [code]payload[/code] is the [code]{key: value}[/code] row, so the caller can
## feed the timeline and the prediction stream, or an empty dictionary when the
## frame is malformed. A predicting client passes [param write] false so the
## authoritative row reconciles rather than snapping the predicted body.
## [br][br]
## A [constant NetwFrameEnvelope.SYNC_FLAG_MASKED] frame (§5.6) carries only the
## fields that differ from the recipient's confirmed baseline, so [param last_row]
## supplies the rest: the caller's own last-decoded row, merged with the
## frame's masked subset to rebuild a complete row. This must be the caller's
## remembered row rather than the live node, because a reconciling client
## ([param write] false) may hold a diverging prediction on the node. A key
## [param last_row] has never seen (only possible on a malformed stream, since
## the gain edge always sends a full mask first) falls back to the live node.
static func apply_volatile_frame(
		node: Node,
		set: NetwSyncSet,
		payload: PackedByteArray,
		write: bool = true,
		last_row: Dictionary = { },
) -> Dictionary:
	var keys: Array[StringName] = []
	var quantizers: Array = []
	var types: Array = []
	for field in set.fields:
		if field.lane != NetwSyncSet.Lane.VOLATILE:
			continue
		keys.append(field.key)
		quantizers.append(field.quantizer)
		types.append(NetwScriptModel.get_node_property_type(node, field.key))
	var frame := NetwFrameEnvelope.decode_sync_frame(payload, quantizers, types)
	if frame.is_empty():
		return { }
	var values: Array = frame.get("values", [])
	var row: Dictionary = { }
	if int(frame.get("flags", 0)) & NetwFrameEnvelope.SYNC_FLAG_MASKED:
		var indices: Array = frame.get("indices", [])
		if values.size() != indices.size():
			return { }
		row = last_row.duplicate()
		for i in keys.size():
			if not row.has(keys[i]):
				row[keys[i]] = node.get(keys[i]) if keys[i] in node else null
		for i in indices.size():
			row[keys[indices[i]]] = values[i]
	else:
		if values.size() != keys.size():
			return { }
		for i in keys.size():
			row[keys[i]] = values[i]
	if write:
		for k: StringName in row:
			node.set(k, row[k])
	return {
		"ordinal": frame.get("ordinal", 0),
		"tick": frame.get("tick", -1),
		"ack": frame.get("ack", -1),
		"payload": row,
	}


## Reads every field of [param set] off [param node] into a
## [code]{key: value}[/code] [Dictionary], the plain-payload counterpart of
## [method gather_volatile] with no wire encoding. This is the snapshot a
## prediction step records into a [NetwTimeline] and reconciles against, keyed by
## the set the script declared rather than a synchronizer's virtual map. A field
## missing on the node is skipped, so the payload only names fields the node
## actually carries.
static func gather_payload(node: Node, set: NetwSyncSet) -> Dictionary:
	var out: Dictionary = { }
	if not is_instance_valid(node) or not set:
		return out
	for field in set.fields:
		if field.key in node:
			out[field.key] = node.get(field.key)
	return out


## Writes a [param payload] produced by [method gather_payload] straight onto
## [param node], setting only the fields [param set] declares that the payload
## carries. A key the set does not name is ignored, mirroring the synchronizer
## restore, so a superset payload never writes a stray property. This is the
## reconciliation restore of a predicted body onto authoritative state.
static func apply_payload(node: Node, set: NetwSyncSet, payload: Dictionary) -> void:
	if not is_instance_valid(node) or not set:
		return
	for field in set.fields:
		if payload.has(field.key) and field.key in node:
			node.set(field.key, payload[field.key])

#endregion


#region Derived-set receive

## Applies one [constant NetwFrameEnvelope.Channel.SYNC] frame whose
## [param ordinal] addresses a derived set. The dispatch resolves the ordinal
## against the route's consumed count and delegates here when it names a derived
## binding, so a mixed route splits cleanly. The frame's sender must be the set's
## authorized author, and its schema must match the spawn descriptor.
func handle_derived_sync(
		entity: NetwEntity,
		ordinal: int,
		payload: PackedByteArray,
		sender: int,
) -> void:
	var api := _api()
	if not api:
		return
	var route := api.liveness.route_of(entity)
	var binding := _derived_binding_for(route, ordinal, api.liveness)
	if not binding:
		_drops_derived_no_set += 1
		return
	if not _derived_sender_ok(binding, entity, sender):
		_drops_derived_bad_sender += 1
		return
	if not _derived_schema_ok(binding, route, ordinal):
		_drops_derived_schema += 1
		return
	var header := binding.apply_volatile(payload)
	if header.is_empty():
		return
	_derived_frames_in += 1
	_feed_derived_interpolation(route, binding, header)


## Applies one reliable [constant NetwFrameEnvelope.Channel.SYNC_DELTA] frame whose
## [param ordinal] addresses a derived set's retained lane. Same ordinal split,
## sender, and schema rules as [method handle_derived_sync].
func handle_derived_delta(
		entity: NetwEntity,
		ordinal: int,
		payload: PackedByteArray,
		sender: int,
) -> void:
	var api := _api()
	if not api:
		return
	var route := api.liveness.route_of(entity)
	var binding := _derived_binding_for(route, ordinal, api.liveness)
	if not binding:
		_drops_derived_no_set += 1
		return
	if not _derived_sender_ok(binding, entity, sender):
		_drops_derived_bad_sender += 1
		return
	if not _derived_schema_ok(binding, route, ordinal):
		_drops_derived_schema += 1
		return
	if binding.apply_retained_delta(payload):
		_derived_frames_in += 1
		_feed_derived_interpolation(route, binding, { })


# Records a just-applied derived frame into display history, the derived mirror
# of NetwSyncCompat._feed_interpolation. The binding has already applied and
# fired its hook, so the interface reads a settled row.
func _feed_derived_interpolation(
		route: int,
		binding: NetwSyncSetBinding,
		header: Dictionary,
) -> void:
	var api := _api()
	var iface := api.interpolation if api else null
	if iface:
		iface.feed_derived_apply(route, binding, header)


# Resolves a unified ordinal to the derived binding it names on this peer, or
# null when the ordinal is below the consumed count or above the derived group.
func _derived_binding_for(
		route: int,
		ordinal: int,
		liveness: NetwLivenessInterface,
) -> NetwSyncSetBinding:
	var repl := _repl()
	if not repl:
		return null
	var base := repl._sync_compat.route_set_count(route)
	var index := ordinal - base
	if index < 0:
		return null
	var group := derived_group(route, liveness)
	if index >= group.size():
		return null
	return group[index]


# A received derived frame validates its sender against the set's policy the same
# way the property router does: the server is always trusted, an authority set
# accepts only the node authority, a controller set only the entity controller.
# A RECORD_STATE frame accepts the server alone, mirroring the send gate, so a
# controller-stamped node authority never smuggles client-authored state.
func _derived_sender_ok(
		binding: NetwSyncSetBinding,
		entity: NetwEntity,
		sender: int,
) -> bool:
	if sender == 1:
		return true
	if binding.set.record == NetwSyncSet.Record.RECORD_STATE:
		return false
	var node := binding.node()
	if not is_instance_valid(node):
		return false
	match binding.set.policy:
		NetwScriptModel.Policy.AUTHORITY:
			return sender == node.get_multiplayer_authority()
		NetwScriptModel.Policy.CONTROLLER:
			return sender == entity.controller
		NetwScriptModel.Policy.ANY_PEER:
			return true
	return false


# Validates a derived binding's schema hash against the spawn descriptor once, so
# two peers whose declarations disagree drop the stream instead of misreading it.
# Absent descriptors accept, matching the consumed lazy-validation stance.
func _derived_schema_ok(
		binding: NetwSyncSetBinding,
		route: int,
		ordinal: int,
) -> bool:
	var pending: Dictionary = _derived_pending_schema.get(route, { })
	if not pending.has(ordinal):
		return true
	return int(pending[ordinal]) == binding.set.schema_hash()


## Appends the [constant NetwFrameEnvelope.Channel.SPAWN] frame's derived-set
## descriptor section for [param route], the count of derived sets then one
## [code](ordinal varint, schema hash u16)[/code] per set, so the receiver
## validates its derived declarations against the sender's at spawn time. The
## ordinal is the unified value, offset above the route's consumed count.
func encode_derived_descriptors(w: NetwBitBuffer.Writer, route: int) -> void:
	var api := _api()
	var repl := _repl()
	if not api or not repl:
		NetwCodec.put_varint(w, 0)
		return
	var base := repl._sync_compat.route_set_count(route)
	var group := derived_group(route, api.liveness)
	NetwCodec.put_varint(w, group.size())
	for i in group.size():
		NetwCodec.put_varint(w, base + i)
		w.put_aligned_u16(group[i].set.schema_hash())


## Records the derived-set descriptor section decoded from a
## [constant NetwFrameEnvelope.Channel.SPAWN] frame, validated lazily against this
## peer's derived bindings as their frames arrive.
func note_derived_schema(route: int, descriptors: Dictionary) -> void:
	if descriptors.is_empty():
		_derived_pending_schema.erase(route)
	else:
		_derived_pending_schema[route] = descriptors

#endregion


## Drops all per-session sync state so no registered binding or property
## sequence record outlives its session.
func clear_session() -> void:
	_derived_bindings.clear()
	_stream_recv_seqs.clear()
	_derived_pending_schema.clear()
	_pending_masked.clear()


## Drops [param route]'s datagram freshness books when its [NetwEntity]
## despawns, so per-stream freshness state never outlives the entity it
## tracks.
func clear_route(route: int) -> void:
	_stream_recv_seqs.erase(route)
	_derived_pending_schema.erase(route)


## Drops the datagram freshness state held against [param peer_id], so a
## reconnecting peer's restarted sequence is accepted fresh on every stream.
## Also drops any masked-lane state held against [param peer_id]: its pending
## rows (never flushed before it left) and, on every derived binding, its
## confirmed baseline and in-flight ring, so a reconnecting peer's masked lane
## heals with a full row instead of diffing against a stale one.
func clear_peer(peer_id: int) -> void:
	for route in _stream_recv_seqs:
		_stream_recv_seqs[route].erase(peer_id)
	_pending_masked.erase(peer_id)
	for binding in _derived_bindings:
		binding.clear_peer(peer_id)


## Breaks the mutual strong reference with the internal property and signal
## router so both can be released. The pipeline is unusable afterward.
func dispose() -> void:
	_property_signal_router = null


## Returns this pipeline's contribution to [method NetwMultiplayer.monitor_snapshot].
func counters() -> Dictionary:
	return {
		&"sends_dropped_unroutable": _sends_dropped_unroutable,
		&"sends_dropped_not_live": _sends_dropped_not_live,
		&"sync_drops_stale": _sync_drops_stale,
		&"derived_sets_active": _derived_bindings.size(),
		&"derived_frames_in": _derived_frames_in,
		&"drops_derived_no_set": _drops_derived_no_set,
		&"drops_derived_bad_sender": _drops_derived_bad_sender,
		&"drops_derived_schema": _drops_derived_schema,
		&"masked_frames_out": _masked_frames_out,
		&"masked_frames_full": _masked_frames_full,
	}


class _PropertySignalRouter:
	extends RefCounted

	var _sync: NetwSyncPipeline


	func _init(sync: NetwSyncPipeline) -> void:
		_sync = sync


	func handle_property_sync(
			entity: NetwEntity,
			comp_node: Node,
			payload: PackedByteArray,
			sender: int,
	) -> void:
		var r := NetwBitBuffer.Reader.new(payload)
		var script := comp_node.get_script() as Script
		var prop_raw := NetwScriptModel.read_token(r)
		var prop: StringName
		if prop_raw is int:
			prop = (
					NetwScriptModel.get_property_name_by_id(script, prop_raw)
					if script
					else &""
			)
		else:
			prop = StringName(prop_raw)
		if prop.is_empty():
			return

		var opt := (
				NetwScriptModel.get_property_config(script, prop)
				if script
				else null
		)
		var set := NetwSyncSet.from_property_config(prop, opt)
		var reliable := set.reliable

		# Decode value using types and quantizers
		var quantizer: NetwQuantize = set.fields[0].quantizer
		var type := NetwScriptModel.get_node_property_type(comp_node, prop)
		var decoded_vals := NetwScriptModel.read_values(r, [quantizer], [type])
		if decoded_vals.is_empty():
			return
		var val = decoded_vals[0]

		# Server-Only Zero Setup Check:
		# Clients cannot sync unregistered properties, even if they have authority.
		if sender != 1 and not opt:
			return

		if not _is_write_allowed(comp_node, prop, sender):
			Netw.dbg.warn(
				"NetwReplicationInterface: unauthorized property sync "
				+ "for '%s' from peer %d",
				[prop, sender],
			)
			return

		comp_node.set(prop, val)

		var api := _sync._api()
		var interpolator: NetwInterpolate = (
				opt.interpolators[0]
				if (opt and not opt.interpolators.is_empty())
				else null
		)
		if interpolator:
			var iface := api.interpolation if api else null
			if iface:
				iface._record(
					comp_node,
					prop,
					val,
					api._receive_tick(),
					interpolator,
					false,
				)

		var mt := api.tree if api else null
		if mt and mt.is_host:
			var liveness := api.liveness
			var route := liveness.route_of(entity)
			var recipients := liveness.live_peers(entity)
			recipients.erase(sender)
			var repl := _sync._repl()
			for recipient in recipients:
				repl.send_to(
					recipient,
					route,
					NetwFrameEnvelope.Channel.PROPERTY_SYNC,
					payload,
					reliable,
				)


	func handle_signal(
			entity: NetwEntity,
			comp_node: Node,
			payload: PackedByteArray,
			sender: int,
	) -> void:
		var r := NetwBitBuffer.Reader.new(payload)
		var script := comp_node.get_script() as Script
		var signal_raw := NetwScriptModel.read_token(r)
		var signal_name: StringName
		if signal_raw is int:
			signal_name = (
					NetwScriptModel.get_signal_name_by_id(script, signal_raw)
					if script
					else &""
			)
		else:
			signal_name = StringName(signal_raw)
		if signal_name.is_empty():
			return

		var opt := (
				NetwScriptModel.get_signal_config(script, signal_name)
				if script
				else null
		)
		var reliable := true
		if opt:
			reliable = opt.transfer_mode == NetwScriptModel.TransferMode.RELIABLE

		var quantizers := opt.quantizers if opt else []
		var types := (
				NetwScriptModel.get_signal_arg_types(script, signal_name)
				if script
				else []
		)
		var args := NetwScriptModel.read_values(r, quantizers, types)

		# Server-Only Zero Setup Check:
		# Clients cannot emit unregistered signals, even if they have authority.
		if sender != 1 and not opt:
			return

		if not _is_signal_allowed(comp_node, signal_name, sender):
			Netw.dbg.warn(
				"NetwReplicationInterface: unauthorized signal emission '%s' "
				+ "from peer %d",
				[signal_name, sender],
			)
			return

		_record_interpolated_signal_args(comp_node, args, opt)

		var callable := Callable(comp_node, &"emit_signal")
		callable.callv([signal_name] + args)

		var api := _sync._api()
		var mt := api.tree if api else null
		if mt and mt.is_host:
			var liveness := api.liveness
			var route := liveness.route_of(entity)
			var recipients := liveness.live_peers(entity)
			recipients.erase(sender)
			var repl := _sync._repl()
			for recipient in recipients:
				repl.send_to(
					recipient,
					route,
					NetwFrameEnvelope.Channel.SIGNAL,
					payload,
					reliable,
				)


	func _record_interpolated_signal_args(
			comp_node: Node,
			args: Array,
			opt: NetwScriptModel.SyncConfig,
	) -> void:
		if not opt or opt.interpolators.is_empty():
			return
		var api := _sync._api()
		var iface := api.interpolation if api else null
		if not iface:
			return
		var tick := api._receive_tick()
		for i in opt.interpolators.size():
			var spec := opt.interpolators[i] as NetwInterpolate
			if not spec or spec.target.is_empty():
				continue
			if i >= args.size():
				continue
			iface._record(
				comp_node,
				spec.target,
				args[i],
				tick,
				spec,
				false,
			)


	func _is_write_allowed(
			node: Node,
			property: StringName,
			sender: int,
	) -> bool:
		if sender == 1:
			return true
		var script := node.get_script() as Script
		if not script:
			return false
		var configs: Dictionary = NetwScriptModel._property_configs.get(script, { })
		if not configs.has(property):
			return sender == node.get_multiplayer_authority()
		var opt: NetwScriptModel.SyncConfig = configs[property]
		match opt.write_policy:
			NetwScriptModel.Policy.AUTHORITY:
				return sender == node.get_multiplayer_authority()
			NetwScriptModel.Policy.CONTROLLER:
				var entity := NetwEntity.of(node)
				return entity != null and sender == entity.controller
			NetwScriptModel.Policy.ANY_PEER:
				return true
		return false


	func _is_signal_allowed(
			node: Node,
			signal_name: StringName,
			sender: int,
	) -> bool:
		if sender == 1:
			return true
		var script := node.get_script() as Script
		if not script:
			return false
		# Unregistered signals are allowed from the authority by default.
		var configs: Dictionary = NetwScriptModel._signal_configs.get(script, { })
		if not configs.has(signal_name):
			return sender == node.get_multiplayer_authority()
		var emit_policy = configs[signal_name].write_policy
		match emit_policy:
			NetwScriptModel.Policy.AUTHORITY:
				return sender == node.get_multiplayer_authority()
			NetwScriptModel.Policy.CONTROLLER:
				var entity := NetwEntity.of(node)
				return entity != null and sender == entity.controller
			NetwScriptModel.Policy.ANY_PEER:
				return true
		return false
