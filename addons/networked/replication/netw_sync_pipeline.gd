## The steady-state sync half of the replication core owned by
## [ReplicationCore]: the per-tick pump that sends every derived
## [NetwPropertySetBinding], the on-demand [method Netw.sync_property] and
## [method Netw.emit_entity_signal] sends, and the receive-side apply for the
## [constant NetwFrameEnvelope.Channel.SYNC],
## [constant NetwFrameEnvelope.Channel.SYNC_DELTA],
## [constant NetwFrameEnvelope.Channel.PROPERTY_SYNC], and
## [constant NetwFrameEnvelope.Channel.SIGNAL] channels.
##
## The pipeline keys its routing and freshness state by route, never by node
## identity. A binding on the pump is resolved to its [NetwEntity] and route once
## per pass inside [method pump], an inbound payload arrives already resolved to
## its target node by [ReplicationCore]'s dispatch, and the datagram
## freshness books key on route. A record therefore dies with its entity
## through [method clear_route] and never outlives the node it tracked.
## [codeblock]
## two cadences over one carrier, both leaving on the tick flush:
##   TICK       on_clock_tick -> pump -> each authored set's SYNC frame
##              (volatile row) and SYNC_DELTA frames (retained lane)
##   ON_DEMAND  sync_property / emit_entity_signal -> one PROPERTY_SYNC /
##              SIGNAL frame; the server broadcasts, a client asks the server
## [/codeblock]
## Carrier concerns stay on [ReplicationCore]:
## [method ReplicationCore.send_to], the per-peer aggregation buffers,
## and the route-to-node resolution through
## [method ReplicationCore.resolve_comp_node]. This pipeline reaches
## them through the owning interface and owns only the sync traffic itself.
##
## [b]Freshness[/b]
## [br]Every unreliable carrier datagram is stamped with one [code]u16[/code]
## sequence by the session's carrier, and
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

const CodeTap := preload("res://addons/networked/replication/code_tap.gd")

# The owning NetwMultiplayer. A weakref because the owner holds this pipeline
# strongly through ReplicationCore and both are reference counted.
var _api_ref: WeakRef

# Derived-set bindings, one NetwPropertySetBinding per state or input set a
# configure_property node declares. Registered on both peers when the node enters
# the tree: the authority pumps its bindings, the receiver resolves an inbound
# frame's ordinal against its own copy. Keyed by node identity through the
# binding's weakref, never by route, so a reparent is followed for free.
var _derived_bindings: Array[NetwPropertySetBinding] = []

# Node instance id -> state/input config hash already checked for the prediction
# component contract. Kept across rewires so one unchanged mistake warns once.
var _prediction_contract_hashes: Dictionary[int, int] = { }

# Engine-owned freshness and pending masked-send books.
var _progress := NetwSyncProgress.new()

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

# Sender: derived pump items skipped before encoding.
var _pump_skips_invalid_node: int = 0
var _pump_skips_no_entity: int = 0
var _pump_skips_no_route: int = 0
var _pump_skips_not_author: int = 0
var _pump_skips_no_recipients: int = 0

var _property_signal_router: _PropertySignalRouter

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
	if api:
		api._connect_once(api.entity_live, _on_entity_live)


func _api() -> NetwMultiplayer:
	return _api_ref.get_ref() as NetwMultiplayer if _api_ref else null


# The owning replication interface, which carries send_to and component address
# resolution. Nodes only ever resolve at that carrier edge.
func _repl() -> ReplicationCore:
	var api := _api()
	return api._replication if api else null


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
	var api := _api()
	var state_set := _compile_property_set(
		script,
		NetwPropertySet.Record.RECORD_STATE,
		api,
		node,
	)
	if state_set:
		register_property_set(node, state_set)
	var input_set := _compile_property_set(
		script,
		NetwPropertySet.Record.RECORD_INPUT,
		api,
		node,
	)
	if input_set:
		register_property_set(node, input_set)
	_queue_prediction_contract_check(node, state_set, input_set)
	# A broadcast set records into no timeline (the rewind boundary in code), so it
	# calls no timeline hook, unlike the state set above.
	var broadcast_set := _compile_property_set(
		script,
		NetwPropertySet.Record.RECORD_BROADCAST,
		api,
		node,
	)
	if broadcast_set:
		register_property_set(node, broadcast_set)


# Compiles through the flat RID store when an api owns this pipeline.
#
# The node is what the column types are reflected through, because half the
# properties a game replicates are engine properties a script's own property
# list cannot see.
func _compile_property_set(
		script: Script,
		record: int,
		api: NetwMultiplayer,
		node: Node,
) -> NetwPropertySet:
	return NetwPropertySet.from_script(script, record, api, node)


## Registers one compiled [param set] on [param node].
func register_property_set(node: Node, set: NetwPropertySet) -> Error:
	if not is_instance_valid(node) or set == null or not set.sealed:
		return ERR_INVALID_DATA
	for index in _derived_bindings.size():
		var binding: NetwPropertySetBinding = _derived_bindings[index]
		if binding.node() != node or binding.set.record != set.record:
			continue
		if binding.set == set:
			_bind_declaration(binding)
			return OK
		_drop_declaration(binding)
		var replacement := NetwPropertySetBinding.new(set, node)
		_capture_declaration_key(replacement)
		_derived_bindings[index] = replacement
		_bind_declaration(replacement)
		return OK
	var created := NetwPropertySetBinding.new(set, node)
	_capture_declaration_key(created)
	_derived_bindings.append(created)
	_bind_declaration(created)
	if set.record == NetwPropertySet.Record.RECORD_STATE:
		_register_state_timeline(node)
	return OK


# Defers until child components have entered the tree. A state plus command
# declaration is a prediction contract, but the pipeline exists before a
# PredictionComponent has necessarily registered its engine.
func _queue_prediction_contract_check(
		node: Node,
		state_set: NetwPropertySet,
		input_set: NetwPropertySet,
) -> void:
	if not state_set or not input_set \
			or state_set.columns.is_empty() or input_set.columns.is_empty():
		return
	var parts := PackedStringArray()
	for column: NetwPropertySet.Column in state_set.columns:
		parts.append("s:%s" % column.key)
	for column: NetwPropertySet.Column in input_set.columns:
		parts.append("i:%s" % column.key)
	var config_hash := hash("\n".join(parts))
	var instance_id := node.get_instance_id()
	if _prediction_contract_hashes.get(instance_id, 0) == config_hash:
		return
	_prediction_contract_hashes[instance_id] = config_hash
	_report_missing_prediction_component.call_deferred(
		weakref(node),
		config_hash,
	)


# Reports the contradiction only after code-first registration and scene child
# setup had a chance to satisfy it.
func _report_missing_prediction_component(
		node_ref: WeakRef,
		config_hash: int,
) -> void:
	var node := node_ref.get_ref() as Node if node_ref else null
	if not is_instance_valid(node):
		return
	if _prediction_contract_hashes.get(node.get_instance_id(), 0) != config_hash:
		return
	var entity := NetwEntity.of(node)
	if entity and entity.prediction.is_registered():
		return
	if not node.find_children("*", "PredictionComponent", true, false).is_empty():
		return
	var entity_name := String(entity.entity_id) if entity else node.name
	push_warning(_missing_prediction_component_message(entity_name))


# Builds the stable fire-once warning asserted by the validator law.
static func _missing_prediction_component_message(
		entity_name: String,
) -> String:
	return (
			"Prediction: %s declares state() and input() fields but carries no "
			% entity_name
			+ "PredictionComponent. Its controller will author commands without "
			+ "simulating the predicted state. Add the component or register "
			+ "prediction from code."
	)


# State-set presence is the rewind trigger: on the server, register the entity so
# the lag compensation service records its authoritative history every tick, even
# without a prediction component. A client and a session with no lag compensation
# mounted register nothing.
func _register_state_timeline(node: Node) -> void:
	var api := _api()
	if not api or api.get_unique_id() != 1:
		return
	var entity := NetwEntity.of(node)
	if entity:
		api.timeline_declare(api.rid_of(entity.owner))


# Drops the entity's rewind history when its state set unregisters, unless it is
# preserving history across a reparent (a lingering despawn keeps its window).
func _unregister_state_timeline(node: Node) -> void:
	var api := _api()
	if not api or api.get_unique_id() != 1:
		return
	var entity := NetwEntity.of(node)
	if not entity or (entity.reparenting and entity.reparenting.preserve_history):
		return
	api.timeline_undeclare(api.rid_of(entity.owner))


## Returns the derived [NetwPropertySetBinding] [param node] declares for
## [param record] ([constant NetwPropertySet.Record.RECORD_STATE],
## [constant NetwPropertySet.Record.RECORD_INPUT], or
## [constant NetwPropertySet.Record.RECORD_BROADCAST]), or [code]null[/code] when the
## node marks no set of that kind. This is the set handle a prediction engine
## gathers, applies, and stamps through, the registry replacement for the
## synchronizer node.
func derived_binding(node: Node, record: int) -> NetwPropertySetBinding:
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
			_drop_declaration(_derived_bindings[i])
			if _derived_bindings[i].set.record == NetwPropertySet.Record.RECORD_STATE:
				had_state = true
			_derived_bindings.remove_at(i)
	if had_state:
		_unregister_state_timeline(node)


# Drops derived bindings whose node has freed.
func _prune_derived() -> void:
	for i in range(_derived_bindings.size() - 1, -1, -1):
		if not is_instance_valid(_derived_bindings[i].node()):
			_drop_declaration(_derived_bindings[i])
			_derived_bindings.remove_at(i)


## Pumps every derived [constant NetwPropertySet.Cadence.TICK] binding for
## [param tick] onto the shared [constant NetwFrameEnvelope.Channel.SYNC] and
## [constant NetwFrameEnvelope.Channel.SYNC_DELTA] lanes. The per-peer flush
## that follows is a carrier concern owned by [ReplicationCore].
func pump(tick: int) -> void:
	var api := _api()
	var liveness := api._liveness if api else null
	if not liveness:
		return
	var repl := _repl()
	if not repl:
		return
	_pump_derived(tick, liveness, repl)


# Pumps every active derived binding: each set's volatile row on the shared
# SYNC frame and its retained lane on the per-recipient SYNC_DELTA. A set's
# ordinal comes from the registration-time declaration model, where consumed
# rows precede derived rows. The author gate follows the set's record and policy.
# The server authors state sets and controllers author input sets. Recipients
# follow its audience (every admitted peer for a public set, the server alone
# for a server-only input set).
func _pump_derived(
		tick: int,
		liveness: LivenessShell,
		repl: ReplicationCore,
) -> void:
	var api := _api()
	if not api:
		return
	_prune_derived()
	for binding: NetwPropertySetBinding in _derived_bindings.duplicate():
		var node := binding.node()
		if not is_instance_valid(node):
			_pump_skips_invalid_node += 1
			continue
		var entity := NetwEntity.of(node)
		if not entity:
			_pump_skips_no_entity += 1
			continue
		if binding.route <= 0:
			_bind_declaration(binding)
		var route := binding.route
		if route <= 0:
			_pump_skips_no_route += 1
			continue
		if not _derived_author_ok(binding, entity, node):
			_pump_skips_not_author += 1
			continue
		var recipients := _derived_recipients(binding, entity)
		if recipients.is_empty():
			_pump_skips_no_recipients += 1
			continue

		var row := repl.sync_model.row_for(
			route,
			NetwSyncModel.Kind.DERIVED,
			binding.order_key,
			binding.set.record,
		)
		if row == null:
			_pump_skips_no_route += 1
			continue
		var ordinal := row.ordinal

		# A prediction engine authors the frame's tick and reconciliation ack
		# through the binding. Absent an engine, the frame stamps the pump's tick
		# and the -1 no-input sentinel, the plain-state cadence.
		var frame_tick := binding.authored_tick if binding.authored_tick >= 0 else tick
		if CodeTap.armed():
			CodeTap.record(binding, route, frame_tick)
		if binding.set.masked:
			# A masked set's mask differs by recipient (each peer's own confirmed
			# baseline), so the frame cannot be shared like the plain volatile row
			# below; it is computed and sent per recipient. The row those masks
			# are diffed against does not differ, so the node is read once for the
			# whole loop, the way the retained lane below polls once.
			if not binding.volatile_external:
				binding.poll_masked()
				if binding.has_masked_row():
					for peer_id in recipients:
						var masked_bytes := _run_encode_stage(
							peer_id,
							frame_tick,
							func(_peer: int, _tick: int) -> PackedByteArray:
								var result := binding.masked_delta_for(
									ordinal,
									peer_id,
									frame_tick,
									binding.reconcile_ack,
								)
								api._sync_encode_meta = result
								return result.get(
									&"bytes",
									PackedByteArray(),
								),
						)
						if masked_bytes.is_empty():
							continue
						var masked := api._sync_encode_meta
						repl.send_to(
							peer_id,
							route,
							NetwFrameEnvelope.Channel.SYNC,
							masked_bytes,
							false,
							0,
							"",
							true,
						)
						_stage_pending_masked(peer_id, binding, masked["row"])
						_masked_frames_out += 1
						if masked["full"]:
							_masked_frames_full += 1
			# A confirmed baseline (or in-flight row) held against a peer no longer
			# a recipient must not survive to its next admission, so absence heals
			# the full masked row on gain, matching the retained lane's rule below.
			binding.retain_masked_baselines(recipients)
		elif not binding.volatile_external:
			for peer_id in recipients:
				var bytes := _run_encode_stage(
					peer_id,
					frame_tick,
					func(_peer: int, _tick: int) -> PackedByteArray:
						return binding.encode_volatile(
							ordinal,
							frame_tick,
							binding.reconcile_ack,
						),
				)
				if not bytes.is_empty():
					repl.send_to(
						peer_id,
						route,
						NetwFrameEnvelope.Channel.SYNC,
						bytes,
						false,
						0,
						"",
						true,
					)

		binding.poll_retained()
		for peer_id in recipients:
			var delta := _run_encode_stage(
				peer_id,
				frame_tick,
				func(_peer: int, _tick: int) -> PackedByteArray:
					return binding.retained_delta(ordinal, peer_id),
			)
			if not delta.is_empty():
				repl.send_to(
					peer_id,
					route,
					NetwFrameEnvelope.Channel.SYNC_DELTA,
					delta,
					true,
					0,
					"",
					true,
				)
		# A baseline held against a peer no longer a recipient must not survive to
		# its next admission, so absence heals the full retained row.
		binding.retain_baselines(recipients)


# Routes one encode through the installed independent virtual stage.
func _run_encode_stage(
		peer: int,
		tick: int,
		encoder: Callable,
) -> PackedByteArray:
	var api := _api()
	if not api:
		return encoder.call(peer, tick)
	api._sync_encoder = encoder
	api._sync_encode_meta = { }
	var bytes := api._sync_encode(peer, tick)
	api._sync_encoder = Callable()
	return bytes


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
		binding: NetwPropertySetBinding,
		entity: NetwEntity,
		node: Node,
) -> bool:
	var api := _api()
	if binding.set.record == NetwPropertySet.Record.RECORD_STATE:
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
		binding: NetwPropertySetBinding,
		entity: NetwEntity,
) -> Array[int]:
	var api := _api()
	if not api:
		return []
	var local_id := api.get_unique_id()
	var out: Array[int] = []
	if binding.set.audience == NetwPropertySet.Audience.AUDIENCE_SERVER_ONLY:
		if local_id != 1:
			out.append(1)
		return out
	for peer_id in api._replication.live_peers(entity):
		if peer_id != local_id:
			out.append(peer_id)
	return out


## A route's derived bindings in captured wire-ordinal order.
func derived_group(
		route: int,
		_liveness: LivenessShell = null,
) -> Array[NetwPropertySetBinding]:
	var out: Array[NetwPropertySetBinding] = []
	var repl := _repl()
	if not repl:
		return out
	for row: NetwSyncModel.SetRow in repl.sync_model.route_rows(route):
		if row.kind != NetwSyncModel.Kind.DERIVED:
			continue
		for binding: NetwPropertySetBinding in _derived_bindings:
			if binding.route == route \
					and binding.order_key == row.key \
					and binding.set.record == row.record:
				out.append(binding)
				break
	return out


# Captures the structural registration key and component address once.
func _capture_declaration_key(binding: NetwPropertySetBinding) -> void:
	var node := binding.node()
	var entity := NetwEntity.of(node) if is_instance_valid(node) else null
	if not entity or not is_instance_valid(entity.owner):
		return
	binding.order_key = StringName(entity.owner.get_path_to(node))
	var repl := _repl()
	var target := repl._resolve_comp(entity, node) if repl else { }
	binding.comp = int(target.get("comp", 0))


# Registers one binding into the value model once its route is live.
func _bind_declaration(binding: NetwPropertySetBinding) -> void:
	var node := binding.node()
	var entity := NetwEntity.of(node) if is_instance_valid(node) else null
	var api := _api()
	var repl := _repl()
	if not entity or not api or not repl:
		return
	if binding.order_key.is_empty():
		_capture_declaration_key(binding)
	var route := api.entity_get_route(entity.rid)
	if route <= 0 or binding.order_key.is_empty():
		return
	binding.route = route
	repl.sync_model.declare(
		route,
		NetwSyncModel.Kind.DERIVED,
		binding.order_key,
		binding.comp,
		binding.set.rid,
		binding.set.record,
		binding.set.wire_hash(),
	)


# Drops one binding's model row.
func _drop_declaration(binding: NetwPropertySetBinding) -> void:
	var repl := _repl()
	if repl and binding.route > 0 and not binding.order_key.is_empty():
		repl.sync_model.drop(
			binding.route,
			NetwSyncModel.Kind.DERIVED,
			binding.order_key,
			binding.set.record,
		)
	binding.route = 0


# Binds declarations that entered the tree before their route went live.
func _on_entity_live(route: int, entity: NetwEntity) -> void:
	for binding: NetwPropertySetBinding in _derived_bindings:
		var node := binding.node()
		if not is_instance_valid(node):
			continue
		if NetwEntity.of(node) == entity:
			binding.route = route
			_bind_declaration(binding)


# Stages [param row] for [param binding]'s masked send to [param peer_id] this
# pass, pending the seq the tick's flush assigns.
func _stage_pending_masked(peer_id: int, binding: NetwPropertySetBinding, row: Dictionary) -> void:
	_progress.stage_masked(
		peer_id,
		binding.route,
		binding.order_key,
		binding.set.record,
		row,
	)


## Commits every masked row staged for [param peer_id] this pass into its
## binding's in-flight ring under [param seq], the datagram seq
## [method ReplicationCore._flush_buffer] just assigned to the send
## that carries it. Called once per unreliable flush, so an overflow-triggered
## early flush mid-pump and the tick's final flush each commit only the rows
## staged since the last flush for that peer.
func commit_pending_masked(peer_id: int, seq: int) -> void:
	for entry: Dictionary in _progress.take_masked(peer_id):
		var binding := _binding_by_key(
			int(entry[&"route"]),
			entry[&"key"],
			int(entry[&"record"]),
		)
		if binding == null:
			continue
		binding.commit_masked_pending(
			peer_id,
			seq,
			entry[&"row"],
		)


# Resolves one value progress key at the shell edge.
func _binding_by_key(
		route: int,
		key: StringName,
		record: int,
) -> NetwPropertySetBinding:
	for binding: NetwPropertySetBinding in _derived_bindings:
		if binding.route == route \
				and binding.order_key == key \
				and binding.set.record == record:
			return binding
	return null


## Promotes [param peer_id]'s confirmed masked baseline on every derived binding
## when the session's tracked ack for it advances. A binding that
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

	var set := NetwPropertySet.from_property_config(property, opt)
	var reliable := set.reliable

	# Write to binary buffer using codec
	var w := NetwBitBufferWriter.new()

	# 1. Write property token (1-byte id when the table is intact, else name)
	NetwScriptModel.write_token(
		w,
		_encode_prop_val(frame["entity"], node, property),
	)

	# 2. Write value. Freshness for an unreliable send rides the carrier
	# datagram's sequence stamp, so the payload carries none of its own.
	var sync_api := _api()
	var gatherer := func() -> Array:
		return [node.get(property)]
	var values := sync_api._run_gather_set(
		(frame["entity"] as NetwEntity).rid,
		int(frame["comp"]),
		gatherer,
	) if sync_api else gatherer.call()
	if values.size() != 1:
		return
	var val: Variant = values[0]
	var quantizer: NetwQuantize = set.columns[0].quantizer
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
	var w := NetwBitBufferWriter.new()

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
	var liveness := api._liveness if api else null
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
	if api and api.is_host:
		var liveness := api._liveness
		for recipient in api._replication.live_peers(frame["entity"]):
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
## whole. [ReplicationCore]'s dispatch owns that memo's lifetime.
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
	var accepted := _progress.accept_unreliable(sender, route, channel, seq)
	if not accepted:
		_sync_drops_stale += 1
	verdicts[key] = accepted
	return accepted

#region Derived-set frames

## Gathers [param node]'s [constant NetwPropertySet.Lane.VOLATILE] field values for
## [param set] and encodes the [constant NetwFrameEnvelope.Channel.SYNC] frame for
## [param ordinal], stamping [param tick] and [param ack] per the set's
## [member NetwPropertySet.stamp]. A state set frames [code]STAMPED | ACKED[/code], a
## plain tick set frames [code]STAMPED[/code], and a set with no stamp frames the
## bare positional row. Returns an empty array when a field is missing on the
## node, so a half row never crosses the wire.
static func encode_volatile_frame(
		node: Node,
		set: NetwPropertySet,
		ordinal: int,
		tick: int,
		ack: int,
) -> PackedByteArray:
	var gathered := gather_volatile(node, set)
	if not gathered[0]:
		return PackedByteArray()
	return NetwSyncKernel.encode_volatile(
		ordinal,
		volatile_flags(set),
		gathered[1],
		gathered[2],
		gathered[3],
		tick,
		ack,
	)


## Reads [param node]'s [constant NetwPropertySet.Lane.VOLATILE] field values for
## [param set], returning [code][ok, values, quantizers, types][/code] with
## [code]ok[/code] false when a field is missing on the node, so a caller drops
## the whole pass rather than send a half row. The volatile-frame encoder and the
## windowed input gather both walk the row through here.
static func gather_volatile(node: Node, set: NetwPropertySet) -> Array:
	var values: Array = []
	var quantizers: Array = []
	var types: Array = []
	for field in set.columns:
		if field.lane != NetwPropertySet.Lane.VOLATILE:
			continue
		if not (field.key in node):
			return [false, [], [], []]
		var value = node.get(field.key)
		values.append(value)
		quantizers.append(field.quantizer)
		types.append(typeof(value))
	return [true, values, quantizers, types]


## Returns the scalar-header SYNC flags for [param set]'s
## [member NetwPropertySet.stamp]: [code]0[/code] plain, [code]STAMPED[/code] for a
## tick set, [code]STAMPED | ACKED[/code] for a state set. The windowed input
## flag is added by the binding, which owns the sample ring.
static func volatile_flags(set: NetwPropertySet) -> int:
	match set.stamp:
		NetwPropertySet.Stamp.STAMP_TICK:
			return NetwFrameEnvelope.SYNC_FLAG_STAMPED
		NetwPropertySet.Stamp.STAMP_TICK_ACK:
			return NetwFrameEnvelope.SYNC_FLAG_STAMPED | NetwFrameEnvelope.SYNC_FLAG_ACKED
	return 0


## Decodes a [constant NetwFrameEnvelope.Channel.SYNC] frame's [param payload]
## against [param set] and, when [param write] is true, writes each decoded
## [constant NetwPropertySet.Lane.VOLATILE] value onto [param node]. Returns the
## decoded header [code]{ordinal, tick, ack, payload}[/code] where
## [code]payload[/code] is the [code]{key: value}[/code] row, so the caller can
## feed the timeline and the prediction stream, or an empty dictionary when the
## frame is malformed. A predicting client passes [param write] false so the
## authoritative row reconciles rather than snapping the predicted body.
## [br][br]
## A [constant NetwFrameEnvelope.SYNC_FLAG_MASKED] frame carries only the
## fields that differ from the recipient's confirmed baseline, so [param last_row]
## supplies the rest: the caller's own last-decoded row, merged with the
## frame's masked subset to rebuild a complete row. This must be the caller's
## remembered row rather than the live node, because a reconciling client
## ([param write] false) may hold a diverging prediction on the node. A key
## [param last_row] has never seen (only possible on a malformed stream, since
## the gain edge always sends a full mask first) falls back to the live node.
## [br][br]
## This merge is what carries the masked lane's reconstruction invariant. After
## an accepted frame the merged row equals the sender's full row for that frame's
## tick, under arbitrary loss, duplication, and reorder, save for the rows before
## the gain edge. Loss and duplication are absorbed because the sender diffs
## against a confirmed baseline and so re-carries every field that changed since
## it. Reorder is absorbed because a stale frame is dropped at
## [method accept_unreliable] and never reaches this merge. The one hole a naive
## diff leaves, a value that changes away from the baseline and back before its
## send is acked, is closed on the sender by
## [method NetwPropertySetBinding.masked_delta_for] keeping the field sticky until
## the ack.
static func apply_volatile_frame(
		node: Node,
		set: NetwPropertySet,
		payload: PackedByteArray,
		write: bool = true,
		last_row: Dictionary = { },
) -> Dictionary:
	var keys: Array[StringName] = []
	var quantizers: Array = []
	var types: Array = []
	for field in set.columns:
		if field.lane != NetwPropertySet.Lane.VOLATILE:
			continue
		keys.append(field.key)
		quantizers.append(field.quantizer)
		types.append(NetwScriptModel.get_node_property_type(node, field.key))
	var fallback: Dictionary = { }
	for key: StringName in keys:
		fallback[key] = node.get(key) if key in node else null
	var staged := NetwSyncKernel.decode_volatile(
		payload,
		keys,
		quantizers,
		types,
		last_row,
		fallback,
	)
	if staged == null:
		return { }
	if write:
		for key: StringName in staged.row:
			node.set(key, staged.row[key])
	return staged.header()


## Reads every field of [param set] off [param node] into a
## [code]{key: value}[/code] [Dictionary], the plain-payload counterpart of
## [method gather_volatile] with no wire encoding. This is the snapshot a
## prediction step records into a [NetwTimeline] and reconciles against, keyed by
## the set the script declared rather than a synchronizer's virtual map. A field
## missing on the node is skipped, so the payload only names fields the node
## actually carries.
static func gather_payload(node: Node, set: NetwPropertySet) -> Dictionary:
	var out: Dictionary = { }
	if not is_instance_valid(node) or not set:
		return out
	for field in set.columns:
		if field.key in node:
			out[field.key] = node.get(field.key)
	return out


## Writes a [param payload] produced by [method gather_payload] straight onto
## [param node], setting only the fields [param set] declares that the payload
## carries. A key the set does not name is ignored, mirroring the synchronizer
## restore, so a superset payload never writes a stray property. This is the
## reconciliation restore of a predicted body onto authoritative state.
static func apply_payload(node: Node, set: NetwPropertySet, payload: Dictionary) -> void:
	if not is_instance_valid(node) or not set:
		return
	for field in set.columns:
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
	var route := api._liveness.route_of(entity)
	var binding := _derived_binding_for(route, ordinal, api._liveness)
	if not binding:
		_drops_derived_no_set += 1
		return
	if not _derived_sender_ok(binding, entity, sender):
		_drops_derived_bad_sender += 1
		return
	if not _derived_schema_ok(binding, route, ordinal):
		_drops_derived_schema += 1
		return
	var decoded: Array[Dictionary] = [{ }]
	var verdict := _run_decode_stage(
		entity.rid,
		binding.comp,
		0,
		-1,
		payload,
		func() -> Error:
			decoded[0] = binding.apply_volatile(payload)
			return OK if not decoded[0].is_empty() else ERR_INVALID_DATA,
	)
	if verdict != OK:
		return
	var header := decoded[0]
	if header.is_empty():
		return
	_derived_frames_in += 1
	_feed_derived_interpolation(binding, header)


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
	var route := api._liveness.route_of(entity)
	var binding := _derived_binding_for(route, ordinal, api._liveness)
	if not binding:
		_drops_derived_no_set += 1
		return
	if not _derived_sender_ok(binding, entity, sender):
		_drops_derived_bad_sender += 1
		return
	if not _derived_schema_ok(binding, route, ordinal):
		_drops_derived_schema += 1
		return
	var applied := [false]
	var verdict := _run_decode_stage(
		entity.rid,
		binding.comp,
		0,
		-1,
		payload,
		func() -> Error:
			applied[0] = binding.apply_retained_delta(payload)
			return OK if applied[0] else ERR_INVALID_DATA,
	)
	if verdict == OK and applied[0]:
		_derived_frames_in += 1
		_feed_derived_interpolation(binding, { })


# Routes one decode through the installed independent virtual stage.
func _run_decode_stage(
		entity: RID,
		comp: int,
		flags: int,
		tick: int,
		payload: PackedByteArray,
		decoder: Callable,
) -> Error:
	var api := _api()
	if not api:
		return decoder.call()
	api._sync_decoder = decoder
	var verdict := api._sync_decode(entity, comp, flags, tick, payload)
	api._sync_decoder = Callable()
	return verdict


# Records a just-applied derived frame into display history, the derived mirror
# of NetwSyncCompat._feed_interpolation. The binding has already applied and
# fired its hook, so the feeder reads a settled row and hands the engine plain
# data through its record door. A stamped set records each field at the header's
# authoring tick, the same tick domain the stamped payload funnel fed; a header
# with no payload row (a retained delta) reads the just-written values back off
# the node at the receive tick. Only a public set feeds display, so a server-only
# input stream never writes a display buffer.
func _feed_derived_interpolation(
		binding: NetwPropertySetBinding,
		header: Dictionary,
) -> void:
	var api := _api()
	var iface := api._display if api else null
	if not iface:
		return
	if binding.set.audience != NetwPropertySet.Audience.AUDIENCE_PUBLIC:
		return
	var node := binding.node()
	if not is_instance_valid(node):
		return
	# A derived field reads and writes the declaring node under its own key, so
	# the mapping is a live per-field interpolator lookup. Resolving it each frame
	# rather than caching lets a spec registered after the first frame take effect.
	var tick := int(header.get("tick", -1))
	var authoring := binding.set.stamp != NetwPropertySet.Stamp.STAMP_NONE and tick >= 0
	if not authoring:
		tick = api.clock.tick if api.clock.is_configured() else 0
	var payload: Dictionary = header.get("payload", { })
	if payload.is_empty():
		# Only the retained lane applies without a payload row, and it never shares
		# a field with the volatile lane, so the read-back stays in the receive-tick
		# domain without mixing a stamped field's history.
		for field in binding.set.columns:
			if field.lane != NetwPropertySet.Lane.RETAINED:
				continue
			var spec := NetwScriptModel.get_node_property_interpolator(node, field.key)
			if spec:
				iface._record(node, field.key, node.get(field.key), tick, spec, false)
		return
	for key: StringName in payload:
		var spec := NetwScriptModel.get_node_property_interpolator(node, key)
		if spec:
			iface._record(node, key, payload[key], tick, spec, authoring)


# Resolves a unified ordinal to the derived binding it names on this peer, or
# null when the ordinal is below the consumed count or above the derived group.
func _derived_binding_for(
		route: int,
		ordinal: int,
		_liveness: LivenessShell,
) -> NetwPropertySetBinding:
	var repl := _repl()
	if not repl:
		return null
	var row := repl.sync_model.row(route, ordinal)
	if row == null or row.kind != NetwSyncModel.Kind.DERIVED:
		return null
	for binding: NetwPropertySetBinding in _derived_bindings:
		if binding.route == route \
				and binding.order_key == row.key \
				and binding.set.record == row.record:
			return binding
	return null


# A received derived frame validates its sender against the set's policy the same
# way the property router does: the server is always trusted, an authority set
# accepts only the node authority, a controller set only the entity controller.
# A RECORD_STATE frame accepts the server alone, mirroring the send gate, so a
# controller-stamped node authority never smuggles client-authored state.
func _derived_sender_ok(
		binding: NetwPropertySetBinding,
		entity: NetwEntity,
		sender: int,
) -> bool:
	if sender == 1:
		return true
	if binding.set.record == NetwPropertySet.Record.RECORD_STATE:
		return false
	var node := binding.node()
	if not is_instance_valid(node):
		return false
	return _repl().policy_admits(binding.set.policy, sender, node, entity)


# Validates a derived binding's schema hash against the spawn descriptor once, so
# two peers whose declarations disagree drop the stream instead of misreading it.
# Absent descriptors accept, matching the consumed lazy-validation stance.
func _derived_schema_ok(
		binding: NetwPropertySetBinding,
		route: int,
		ordinal: int,
) -> bool:
	var pending: Dictionary = _derived_pending_schema.get(route, { })
	if not pending.has(ordinal):
		return true
	return int(pending[ordinal]) == binding.set.wire_hash()


## Appends the [constant NetwFrameEnvelope.Channel.SPAWN] frame's derived-set
## descriptor section for [param route], the count of derived sets then one
## [code](ordinal varint, schema hash u16)[/code] per set, so the receiver
## validates its derived declarations against the sender's at spawn time. The
## ordinal is the unified value, offset above the route's consumed count.
func encode_derived_descriptors(w: NetwBitBufferWriter, route: int) -> void:
	var repl := _repl()
	if not repl:
		NetwCodec.put_varint(w, 0)
		return
	var rows: Array[NetwSyncModel.SetRow] = []
	for row: NetwSyncModel.SetRow in repl.sync_model.route_rows(route):
		if row.kind == NetwSyncModel.Kind.DERIVED:
			rows.append(row)
	NetwCodec.put_varint(w, rows.size())
	for row: NetwSyncModel.SetRow in rows:
		NetwCodec.put_varint(w, row.ordinal)
		w.put_aligned_u16(row.schema_hash)


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
	_progress.clear()
	_derived_pending_schema.clear()


## Drops [param route]'s datagram freshness books when its [NetwEntity]
## despawns, so per-stream freshness state never outlives the entity it
## tracks.
func clear_route(route: int) -> void:
	_progress.clear_route(route)
	_derived_pending_schema.erase(route)
	for binding: NetwPropertySetBinding in _derived_bindings:
		if binding.route == route:
			binding.route = 0


## Drops the datagram freshness state held against [param peer_id], so a
## reconnecting peer's restarted sequence is accepted fresh on every stream.
## Also drops any masked-lane state held against [param peer_id]: its pending
## rows (never flushed before it left) and, on every derived binding, its
## confirmed baseline and in-flight ring, so a reconnecting peer's masked lane
## heals with a full row instead of diffing against a stale one.
func clear_peer(peer_id: int) -> void:
	_progress.clear_peer(peer_id)
	for binding in _derived_bindings:
		binding.clear_peer(peer_id)


## Breaks the mutual strong reference with the internal property and signal
## router so both can be released. The pipeline is unusable afterward.
func dispose() -> void:
	var api := _api()
	if api and api.entity_live.is_connected(_on_entity_live):
		api.entity_live.disconnect(_on_entity_live)
	_property_signal_router = null
	CodeTap.close()


## Returns this pipeline's contribution to [method NetwMultiplayer.stats_snapshot].
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
		&"sync_pump_skips_invalid_node": _pump_skips_invalid_node,
		&"sync_pump_skips_no_entity": _pump_skips_no_entity,
		&"sync_pump_skips_no_route": _pump_skips_no_route,
		&"sync_pump_skips_not_author": _pump_skips_not_author,
		&"sync_pump_skips_no_recipients": _pump_skips_no_recipients,
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
			comp: int = -1,
	) -> void:
		var r := NetwBitBufferReader.create(payload)
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
		var set := NetwPropertySet.from_property_config(prop, opt)
		var reliable := set.reliable

		# Decode value using types and quantizers
		var quantizer: NetwQuantize = set.columns[0].quantizer
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
				"ReplicationCore: unauthorized property sync "
				+ "for '%s' from peer %d",
				[prop, sender],
			)
			return

		var api := _sync._api()
		if comp < 0:
			var repl := _sync._repl()
			var target := repl._resolve_comp(entity, comp_node) \
			if repl else { &"comp": 0 }
			comp = int(target.get(&"comp", 0))
		var applier := func(values: Array) -> Error:
			if values.size() != 1:
				return ERR_INVALID_DATA
			comp_node.set(prop, values[0])
			return OK
		var verdict := api._run_apply_set(
			entity.rid,
			comp,
			[val],
			applier,
		) if api else applier.call([val])
		if verdict != OK:
			return
		var interpolator: NetwInterpolate = (
				opt.interpolators[0]
				if (opt and not opt.interpolators.is_empty())
				else null
		)
		if interpolator:
			var iface := api._display if api else null
			if iface:
				iface._record(
					comp_node,
					prop,
					val,
					api._receive_tick(),
					interpolator,
					false,
				)

		if api and api.is_host:
			var liveness := api._liveness
			var route := liveness.route_of(entity)
			var repl := _sync._repl()
			var recipients := repl.live_peers(entity)
			recipients.erase(sender)
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
		var r := NetwBitBufferReader.create(payload)
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
				"ReplicationCore: unauthorized signal emission '%s' "
				+ "from peer %d",
				[signal_name, sender],
			)
			return

		_record_interpolated_signal_args(comp_node, args, opt)

		var callable := Callable(comp_node, &"emit_signal")
		callable.callv([signal_name] + args)

		var api := _sync._api()
		if api and api.is_host:
			var liveness := api._liveness
			var route := liveness.route_of(entity)
			var repl := _sync._repl()
			var recipients := repl.live_peers(entity)
			recipients.erase(sender)
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
		var iface := api._display if api else null
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
		return _sync._repl().policy_admits(opt.write_policy, sender, node)


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
		return _sync._repl().policy_admits(emit_policy, sender, node)
