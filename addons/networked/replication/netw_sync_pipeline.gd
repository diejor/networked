## The steady-state sync half of the replication core owned by
## [ReplicationCore]: the per-tick pump that sends every derived
## [NetwPropertySetBinding], the on-demand [method Netw.sync_property] and
## [method Netw.emit_entity_signal] sends, and the receive-side apply for the
## [constant NetwFrameEnvelope.Channel.SYNC],
## [constant NetwFrameEnvelope.Channel.SYNC_ROW],
## [constant NetwFrameEnvelope.Channel.SYNC_ROW_DELTA],
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
##   TICK       on_clock_tick -> pump -> each authored set's SYNC_ROW frame
##              (volatile row) and SYNC_ROW_DELTA frames (retained lane)
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

const _CONTRACT_CHECK_KEY_PREFIX := "sync-prediction-contract?"

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

# Engine-owned datagram freshness books.
var _progress := NetwSyncProgress.new()

# Receiver: a frame's own row address named no derived binding on this peer, or
# a SYNC ordinal above the consumed count named none.
var _drops_derived_no_set: int = 0
# Receiver: a derived frame's sender is not the set's authorized author.
var _drops_derived_bad_sender: int = 0
# Receiver: a derived set's schema hash disagreed with the spawn descriptor.
var _drops_derived_schema: int = 0
# Receiver: a derived frame applied, on either lane.
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

var _row_sender: NetwReplicationSend = null

var _row_frames_out: int = 0
var _row_frames_full: int = 0
var _row_frames_refused_by_stage: int = 0
var _row_frames_ungathered: int = 0
var _retained_frames_out: int = 0
var _window_frames_out: int = 0
var _window_samples_out: int = 0

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


func _row_send() -> NetwReplicationSend:
	if _row_sender == null:
		_row_sender = NetwReplicationSend.new()
		_row_sender.declare_channel(
			NetwFrameEnvelope.Channel.SYNC_ROW,
			&"SYNC_ROW",
			true,
		)
		_row_sender.set_encode_stage(_stage_row_frame)
	return _row_sender


func _stage_row_frame(
		peer: int,
		frame_tick: int,
		_route: int,
		_comp: int,
		_mask: int,
		bytes: PackedByteArray,
) -> PackedByteArray:
	return _run_encode_stage(
		peer,
		frame_tick,
		func(_peer: int, _tick: int) -> PackedByteArray:
			return bytes,
	)


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
	var api := _api()
	if api:
		api._settle_schedule(
			_report_missing_prediction_component.bind(
				weakref(node),
				config_hash,
			),
			StringName("%s%d" % [_CONTRACT_CHECK_KEY_PREFIX, instance_id]),
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
		api.timeline_declare(api.entity_of(entity.owner))


# Drops the entity's rewind history when its state set unregisters, unless it is
# preserving history across a reparent (a lingering despawn keeps its window).
func _unregister_state_timeline(node: Node) -> void:
	var api := _api()
	if not api or api.get_unique_id() != 1:
		return
	var entity := NetwEntity.of(node)
	if not entity or (entity.reparenting and entity.reparenting.preserve_history):
		return
	api.timeline_undeclare(api.entity_of(entity.owner))


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
## [param tick] onto the [constant NetwFrameEnvelope.Channel.SYNC_ROW] and
## [constant NetwFrameEnvelope.Channel.SYNC_ROW_DELTA] lanes. The per-peer
## flush that follows is a carrier concern owned by [ReplicationCore].
func pump(tick: int) -> void:
	var api := _api()
	var native_core := api._native_core if api else null
	if not native_core:
		return
	var repl := _repl()
	if not repl:
		return
	_pump_derived(tick, native_core, repl)


# Pumps every active derived binding: both of a set's lanes are offered whole
# to NetwReplicationSend, which decides what each recipient is owed. The row a
# binding declared answers both gates, through
# [method NetwSyncModel.authors] and [method NetwSyncModel.recipients].
func _pump_derived(
		tick: int,
		native_core: NetwMultiplayerCore,
		repl: ReplicationCore,
) -> void:
	var api := _api()
	if not api:
		return
	var row_send := _row_send()
	var offers: Array = []
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
		var row := repl.sync_model.row_for(
			route,
			NetwSyncModel.Kind.KIND_DERIVED,
			binding.order_key,
			binding.set.record,
		)
		if row == null:
			_pump_skips_no_route += 1
			continue
		var ordinal := row.ordinal

		var local_id := api.get_unique_id()
		if not repl.sync_model.authors(
				route,
				ordinal,
				local_id,
				node.is_multiplayer_authority(),
				entity.controller,
		):
			_pump_skips_not_author += 1
			continue
		var recipients := repl.sync_model.recipients(
			route,
			ordinal,
			local_id,
			PackedInt32Array(api._replication.live_peers(entity)),
		)
		if recipients.is_empty():
			_pump_skips_no_recipients += 1
			continue

		# A prediction engine authors the frame's tick and reconciliation ack
		# through the binding. Absent an engine, the frame stamps the pump's tick
		# and the -1 no-input sentinel, the plain-state cadence.
		var frame_tick := binding.authored_tick if binding.authored_tick >= 0 else tick
		if CodeTap.armed():
			CodeTap.record(binding, route, frame_tick)
		if not binding.volatile_external:
			var values := binding.volatile_row()
			if not values.is_empty():
				var windowed := binding.is_windowed() and not binding.set.masked
				offers.append({
					"route": route,
					"comp": ordinal,
					"channel": NetwFrameEnvelope.Channel.SYNC_ROW_WINDOW \
					if windowed else NetwFrameEnvelope.Channel.SYNC_ROW,
					"schema": binding.set.volatile_schema,
					"values": values,
					"recipients": recipients,
					"tick": frame_tick,
					"ack": binding.reconcile_ack,
					"masked": binding.set.masked and not windowed,
					"windowed": windowed,
					"window": binding.set.window,
					"priority": 1.0,
				})

		var retained := binding.retained_row()
		if not retained.is_empty():
			offers.append({
				"route": route,
				"comp": ordinal,
				"channel": NetwFrameEnvelope.Channel.SYNC_ROW_DELTA,
				"schema": binding.set.retained_schema,
				"values": retained,
				"recipients": recipients,
				"tick": -1,
				"ack": -1,
				"reliable": true,
				"priority": 1.0,
			})
		# A baseline held against a peer no longer a recipient must not survive
		# to its next admission, so absence heals the full retained row.
		row_send.retain_row(route, ordinal, recipients)

	_flush_row_offers(offers, repl)


func _flush_row_offers(offers: Array, repl: ReplicationCore) -> void:
	if offers.is_empty():
		return
	var result: Dictionary = _row_send().run_deferred(offers)
	var sends: Array = result["sends"]
	var api := _api()
	if api:
		api.report_event(
			NetwMultiplayerCore.GATHER,
			0,
			{ offers = offers.size(), sends = sends.size() },
		)
	for at in sends.size():
		var send: Dictionary = sends[at]
		var reliable := bool(send.get("reliable", false))
		repl.send_to(
			int(send["peer"]),
			int(send["route"]),
			_lane_channel(send),
			send["bytes"],
			reliable,
			0,
			"",
			true,
		)
		# The frame is in the carrier now, so this row waits for the datagram
		# it is actually in. A run that overflowed during the hand-off above
		# already went out, and the rows it carried were bound to it there.
		_row_send().confirm(at)
		if reliable:
			_retained_frames_out += 1
			continue
		if bool(send.get("windowed", false)):
			_window_frames_out += 1
			_window_samples_out += int(send.get("samples", 0))
			continue
		_row_frames_out += 1
		if bool(send.get("whole", false)):
			_row_frames_full += 1
	_row_frames_refused_by_stage += int(result.get("staged_out", 0))
	_row_frames_ungathered += int(result.get("ungathered", 0))


# The channel one send rides, which is the lane shape the pass answered with.
func _lane_channel(send: Dictionary) -> NetwFrameEnvelope.Channel:
	if bool(send.get("reliable", false)):
		return NetwFrameEnvelope.Channel.SYNC_ROW_DELTA
	if bool(send.get("windowed", false)):
		return NetwFrameEnvelope.Channel.SYNC_ROW_WINDOW
	return NetwFrameEnvelope.Channel.SYNC_ROW


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
	api.report_event(
		NetwMultiplayerCore.SYNC_ENCODE,
		0,
		{ bytes = bytes.size() },
		peer,
	)
	return bytes


## A route's derived bindings in captured wire-ordinal order.
func derived_group(
		route: int,
		_native_core: NetwMultiplayerCore = null,
) -> Array[NetwPropertySetBinding]:
	var out: Array[NetwPropertySetBinding] = []
	var repl := _repl()
	if not repl:
		return out
	for binding in repl.sync_model.route_bindings(
			route,
			NetwSyncModel.Kind.KIND_DERIVED,
	):
		out.append(binding as NetwPropertySetBinding)
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
		NetwSyncModel.Kind.KIND_DERIVED,
		binding.order_key,
		binding.comp,
		binding.set.rid,
		binding.set.record,
		binding.set.wire_hash(),
		binding.set.policy,
		binding.set.audience,
	)
	repl.sync_model.attach(
		route,
		NetwSyncModel.Kind.KIND_DERIVED,
		binding.order_key,
		binding.set.record,
		binding,
	)


# Drops one binding's model row.
func _drop_declaration(binding: NetwPropertySetBinding) -> void:
	var repl := _repl()
	if repl and binding.route > 0 and not binding.order_key.is_empty():
		repl.sync_model.detach(
			binding.route,
			NetwSyncModel.Kind.KIND_DERIVED,
			binding.order_key,
			binding.set.record,
		)
		repl.sync_model.drop(
			binding.route,
			NetwSyncModel.Kind.KIND_DERIVED,
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


## Binds every row the pass staged for [param peer_id] to [param seq], the
## datagram seq [method ReplicationCore.flush_all_buffers] just assigned to the
## send that carries it. Called once per unreliable flush, so an
## overflow-triggered early flush mid-pump and the tick's final flush each bind
## only the rows sent since the last flush for that peer.
func commit_pending_masked(peer_id: int, seq: int) -> void:
	if _row_sender:
		_row_sender.commit(peer_id, seq)


## Advances every derived lane's confirmed baseline for [param peer_id] when the
## session's tracked ack for it advances. A lane that sent that peer nothing
## no-ops.
func note_peer_ack(peer_id: int, acked_seq: int) -> void:
	if _row_sender:
		_row_sender.acknowledge(peer_id, acked_seq)


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
	NetwCodec.write_values(w, [val], [quantizer], [type])

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
	NetwCodec.write_values(w, args, quantizers, types)

	_send_entity_event(frame, NetwFrameEnvelope.Channel.SIGNAL, w.to_bytes(), reliable)


# Resolves the entity, route, and component addressing for an on-demand event
# send. Returns an empty dictionary when the node is unroutable.
func _resolve_send(node: Node) -> Dictionary:
	var entity := NetwEntity.of(node)
	if not entity:
		_sends_dropped_unroutable += 1
		return { }
	var api := _api()
	var native_core := api._native_core if api else null
	if not native_core:
		return { }
	var route := native_core.liveness_route_of(entity)
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
	var recipients := NetwSyncModel.event_recipients(
		api != null and api.is_host,
		api.get_unique_id() if api else 0,
		0,
		PackedInt32Array(repl.live_peers(frame["entity"])),
	)
	for recipient in recipients:
		repl.send_to(
			recipient,
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

## Applies one [constant NetwFrameEnvelope.Channel.SYNC_ROW] frame to the
## derived set its own header addresses. The frame's sender must be the set's
## authorized author, and its schema must match the spawn descriptor.
##
## The address is read off the frame rather than off the envelope because the
## envelope's component byte names a node in the entity's component table, and a
## row address is not one: a route carries one row per declared set, and a set
## and its siblings share the node they were declared on.
func handle_derived_row(
		entity: NetwEntity,
		payload: PackedByteArray,
		sender: int,
) -> void:
	var api := _api()
	if not api:
		return
	var header: Dictionary = _row_send().peek(payload)
	if header.is_empty():
		_drops_derived_no_set += 1
		return
	var ordinal := int(header["comp"])
	var route := api._native_core.liveness_route_of(entity)
	var binding := _derived_binding_for(route, ordinal, api._native_core)
	if not binding:
		_drops_derived_no_set += 1
		return
	var node := binding.node()
	if not is_instance_valid(node):
		_drops_derived_no_set += 1
		return
	var model := api._replication.sync_model
	if not model.admits_sender(
			route,
			ordinal,
			sender,
			node.get_multiplayer_authority(),
			entity.controller,
	):
		_drops_derived_bad_sender += 1
		return
	if not model.admits_schema(route, ordinal):
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
			decoded[0] = binding.apply_row_frame(_row_send(), payload)
			return OK if not decoded[0].is_empty() else ERR_INVALID_DATA,
	)
	if verdict != OK or decoded[0].is_empty():
		return
	_derived_frames_in += 1
	_feed_derived_interpolation(binding, decoded[0])


## Applies one [constant NetwFrameEnvelope.Channel.SYNC_ROW_WINDOW] frame to
## the derived set its own header addresses, delivering every tick it repeats.
## Same self addressing, sender and schema rules as [method handle_derived_row].
func handle_window_row(
		entity: NetwEntity,
		payload: PackedByteArray,
		sender: int,
) -> void:
	var api := _api()
	if not api:
		return
	var header: Dictionary = _row_send().peek(payload)
	if header.is_empty():
		_drops_derived_no_set += 1
		return
	var ordinal := int(header["comp"])
	var route := api._native_core.liveness_route_of(entity)
	var binding := _derived_binding_for(route, ordinal, api._native_core)
	if not binding:
		_drops_derived_no_set += 1
		return
	var node := binding.node()
	if not is_instance_valid(node):
		_drops_derived_no_set += 1
		return
	var model := api._replication.sync_model
	if not model.admits_sender(
			route,
			ordinal,
			sender,
			node.get_multiplayer_authority(),
			entity.controller,
	):
		_drops_derived_bad_sender += 1
		return
	if not model.admits_schema(route, ordinal):
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
			decoded[0] = binding.apply_window_frame(_row_send(), payload)
			return OK if not decoded[0].is_empty() else ERR_INVALID_DATA,
	)
	if verdict != OK or decoded[0].is_empty():
		return
	_derived_frames_in += 1
	_feed_derived_interpolation(binding, decoded[0])


## Applies one [constant NetwFrameEnvelope.Channel.SYNC_ROW_DELTA] frame to the
## retained half of the derived set its own header addresses. Same self
## addressing, sender and schema rules as [method handle_derived_row].
func handle_retained_row(
		entity: NetwEntity,
		payload: PackedByteArray,
		sender: int,
) -> void:
	var api := _api()
	if not api:
		return
	var header: Dictionary = _row_send().peek(payload)
	if header.is_empty():
		_drops_derived_no_set += 1
		return
	var ordinal := int(header["comp"])
	var route := api._native_core.liveness_route_of(entity)
	var binding := _derived_binding_for(route, ordinal, api._native_core)
	if not binding:
		_drops_derived_no_set += 1
		return
	var node := binding.node()
	if not is_instance_valid(node):
		_drops_derived_no_set += 1
		return
	var model := api._replication.sync_model
	if not model.admits_sender(
			route,
			ordinal,
			sender,
			node.get_multiplayer_authority(),
			entity.controller,
	):
		_drops_derived_bad_sender += 1
		return
	if not model.admits_schema(route, ordinal):
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
			decoded[0] = binding.apply_retained_row(_row_send(), payload)
			return OK if not decoded[0].is_empty() else ERR_INVALID_DATA,
	)
	if verdict != OK or decoded[0].is_empty():
		return
	_derived_frames_in += 1
	_feed_derived_interpolation(binding, decoded[0])


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
	api.report_event(
		NetwMultiplayerCore.SYNC_DECODE,
		api._native_core.liveness_core.route_of(entity),
		{ comp = comp },
		0,
		&"",
		{ },
		verdict,
	)
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
		_native_core: NetwMultiplayerCore,
) -> NetwPropertySetBinding:
	var repl := _repl()
	if not repl:
		return null
	var row := repl.sync_model.row(route, ordinal)
	if row == null or row.kind != NetwSyncModel.Kind.KIND_DERIVED:
		return null
	return repl.sync_model.binding_of(route, ordinal) as NetwPropertySetBinding


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
	var rows: Array[NetwSyncSetRow] = []
	for row: NetwSyncSetRow in repl.sync_model.route_rows(route):
		if row.kind == NetwSyncModel.Kind.KIND_DERIVED:
			rows.append(row)
	NetwCodec.put_varint(w, rows.size())
	for row: NetwSyncSetRow in rows:
		NetwCodec.put_varint(w, row.ordinal)
		w.put_aligned_u16(row.schema_hash)


## Records the derived-set descriptor section decoded from a
## [constant NetwFrameEnvelope.Channel.SPAWN] frame, validated lazily against this
## peer's derived bindings as their frames arrive.
func note_derived_schema(route: int, descriptors: Dictionary) -> void:
	var repl := _repl()
	if repl:
		repl.sync_model.note_descriptors(route, descriptors)

#endregion

## Drops all per-session sync state so no registered binding or property
## sequence record outlives its session.
func clear_session() -> void:
	_derived_bindings.clear()
	_progress.clear()
	_row_sender = null


## Drops [param route]'s datagram freshness books when its [NetwEntity]
## despawns, so per-stream freshness state never outlives the entity it
## tracks.
func clear_route(route: int) -> void:
	_progress.clear_route(route)
	if _row_sender:
		_row_sender.close_route(route)
	for binding: NetwPropertySetBinding in _derived_bindings:
		if binding.route == route:
			binding.route = 0


## Drops the datagram freshness state held against [param peer_id], so a
## reconnecting peer's restarted sequence is accepted fresh on every stream.
## Also drops every derived lane's baseline for it, so a reconnecting peer heals
## with a whole row instead of diffing against a row the last peer at that id
## held.
func clear_peer(peer_id: int) -> void:
	_progress.clear_peer(peer_id)
	for binding in _derived_bindings:
		binding.clear_peer(peer_id)
	if _row_sender:
		_row_sender.forget_peer(peer_id)


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
		&"row_frames_out": _row_frames_out,
		&"row_frames_full": _row_frames_full,
		&"row_frames_stage_refused": _row_frames_refused_by_stage,
		&"row_frames_ungathered": _row_frames_ungathered,
		&"retained_frames_out": _retained_frames_out,
		&"window_frames_out": _window_frames_out,
		&"window_samples_out": _window_samples_out,
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
		var decoded_vals := NetwCodec.read_values(r, [quantizer], [type])
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
			var route := api._native_core.liveness_route_of(entity)
			var repl := _sync._repl()
			var recipients := NetwSyncModel.event_recipients(
				true,
				api.get_unique_id(),
				sender,
				PackedInt32Array(repl.live_peers(entity)),
			)
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
		var args := NetwCodec.read_values(r, quantizers, types)

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
			var route := api._native_core.liveness_route_of(entity)
			var repl := _sync._repl()
			var recipients := NetwSyncModel.event_recipients(
				true,
				api.get_unique_id(),
				sender,
				PackedInt32Array(repl.live_peers(entity)),
			)
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
