## Consumes [MultiplayerSynchronizer] registrations so an unmodified stock
## project synchronizes through the Networked sync pump instead of the native
## [code]SceneReplicationInterface[/code], gaining route identity, datagram
## freshness gating, liveness-scoped recipients, and interest admission without
## knowing Networked exists.
##
## The native replicator is sealed (constructed inside [SceneMultiplayer],
## never exposed), so there is nothing to subclass. This adapter intercepts the
## [method MultiplayerAPI.object_configuration_add] a synchronizer already
## emits and translates its [SceneReplicationConfig] into a consumed sync set:
## the always-replicated properties become the per-tick
## [constant NetwFrameEnvelope.Channel.SYNC] row, the on-change properties
## become the reliable [constant NetwFrameEnvelope.Channel.SYNC_DELTA] row, and
## the spawn properties keep riding the
## [constant NetwFrameEnvelope.Channel.SPAWN] frame's spawn-state section. A
## registration is consumed, never forwarded to [member NetwMultiplayer.inner],
## so the native replicator never tracks the node and its sync loop iterates
## empty sets.
## [codeblock]
## authority, each pump pass          receiver
##   gather sync row (throttled)
##     SYNC (unreliable) ───────────▶ freshest-wins apply, synchronized fires
##   poll watchers, per-peer mask
##     SYNC_DELTA (reliable) ───────▶ masked apply, delta_synchronized fires
## [/codeblock]
## A consumed set is addressed on the wire as (route, ordinal): the route is
## the [NetwEntity] identity of the synchronizer's resolved root, and the
## ordinal orders the route's consumed synchronizers by their path from the
## entity root, which both peers derive from the same scene. The
## [constant NetwFrameEnvelope.Channel.SPAWN] frame carries one
## [code](ordinal, schema hash)[/code] descriptor per consumed set, so a
## receiver whose translated set disagrees with the sender's poisons that
## binding loudly at spawn time instead of misreading bytes every tick.
##
## [br][br][b]Conformance and extension[/b]
## [br]Consumption is conformance-first. Sync properties reach visible peers
## on the sender's [member MultiplayerSynchronizer.replication_interval]
## throttle, watch properties dirty-poll on
## [member MultiplayerSynchronizer.delta_interval] and heal a peer that gains
## visibility with a full watched row (the native zero-baseline heal, kept as
## an explicit per-recipient baseline), sub-node property paths resolve exactly
## as [method MultiplayerSynchronizer.get_state] resolves them, only the
## synchronizer's authority may author its stream, and
## [signal MultiplayerSynchronizer.synchronized] and
## [signal MultiplayerSynchronizer.delta_synchronized] fire on the receiving
## node at apply. The pipeline then exceeds the native contract in one place:
## recipients come from the [NetwSpawnBook] book clamped by
## [InterestCore] admission, so a peer is never sent state for a node
## it was never sent, and a stream stops the moment its route stops being
## [constant NetwMultiplayer.EntityState.LIVE] instead of trailing the despawn.
## Watch lists are capped at 64 properties, asserted loudly at consumption
## where native silently corrupts its bitmask.
class_name NetwSyncCompat
extends RefCounted


## Watched properties per consumed synchronizer ride a 64-bit delta mask, the
## same ceiling the native replicator has, enforced loudly at consumption.
const WATCH_LIMIT := 64

# The owning NetwMultiplayer. A weakref because the owner holds this adapter
# strongly through ReplicationCore and both are reference counted.
var _api_ref: WeakRef

# Consumed bindings in registration order. Wire ordinals come from the cached
# declaration model rather than positions in this list.
var _consumed: Array[_Consumed] = []

# The shared dirty-poll engine for every binding's watched fields, keyed by the
# binding. The compat gathers a watched row and hands it in, the book answers
# per-recipient masks and holds the per-peer baselines.
var _watch_book := NetwWatchBook.new()

# route -> { ordinal: schema hash } decoded from a SPAWN frame's descriptor
# section, held until the route's bindings register and validate against it.
var _pending_schema: Dictionary = { }

var _sync_frames_out: int = 0
var _sync_frames_in: int = 0
var _delta_frames_out: int = 0
var _delta_frames_in: int = 0
# Receiver: a SYNC or SYNC_DELTA frame named an ordinal with no binding on
# this peer. Healthy while a registration races the first frames.
var _drops_sync_no_set: int = 0
# Receiver: frame from a sender that is not the target synchronizer's authority.
var _drops_sync_bad_sender: int = 0
# Receiver: frame for a binding whose schema hash disagreed with the SPAWN
# frame's descriptor. The binding is poisoned and every frame drops loudly.
var _drops_sync_poisoned: int = 0
# Receiver: SYNC frame whose reserved byte was written. The sender speaks a
# grammar this version does not have, so the frame drops loudly rather than
# reading its values against the wrong layout.
var _drops_sync_unknown_flag: int = 0


func _init(api: NetwMultiplayer) -> void:
	_api_ref = weakref(api) if api else null
	if api:
		api._connect_once(api.entity_live, _on_entity_live)


func _api() -> NetwMultiplayer:
	return _api_ref.get_ref() as NetwMultiplayer if _api_ref else null


# One consumed synchronizer's live state: the translated field lists, the
# watcher copies driving the delta stream, and the per-recipient baselines.
class _Consumed:
	extends RefCounted

	var sync_ref: WeakRef
	var root_ref: WeakRef
	var config_ref: WeakRef
	var config_changed: Callable
	var config_dirty := true
	var entity: RID
	var route: int
	var comp: int
	var order_key: StringName
	# Config property paths in config order: the SYNC row and the SYNC_DELTA
	# watch order. The config's changed signal invalidates this registration
	# snapshot so a later pump rebuilds it before gathering values.
	var sync_paths: Array[NodePath] = []
	var watch_paths: Array[NodePath] = []
	var intent_by_peer: Dictionary[int, bool] = { }
	var schema_hash: int = 0
	var poisoned: bool = false
	var schema_checked: bool = false
	# One implicit-adopt attempt per binding, so an unadoptable root does not
	# retry every pass.
	var adopt_attempted: bool = false
	var last_sync_usec: int = -1
	var last_watch_usec: int = -1
	# Cached display feed paths [[node, property], ...] resolved from the live
	# config, the expensive half of the feed mapping. The per-property
	# interpolator is looked up live at record time so a spec registered after
	# the first frame still takes effect.
	var display_feed_paths: Array = []
	var display_feed_built: bool = false


	func sync() -> MultiplayerSynchronizer:
		return sync_ref.get_ref() as MultiplayerSynchronizer if sync_ref else null


	func root() -> Node:
		return root_ref.get_ref() as Node if root_ref else null


## Consumes a synchronizer registration for [param root]. Returns
## [constant OK] so [method NetwMultiplayer._object_configuration_add] treats
## it as handled and never forwards it to [member NetwMultiplayer.inner].
##
## A configuration with no always-replicated and no on-change properties (a
## spawn-only config, or none at all) registers nothing on the pump: its spawn
## properties already ride the [constant NetwFrameEnvelope.Channel.SPAWN]
## frame's spawn-state section and its visibility still votes on spawn fate
## through [method synchronizer_verdict].
func consume(root: Node, sync: MultiplayerSynchronizer) -> Error:
	if not is_instance_valid(root) or not is_instance_valid(sync):
		return ERR_INVALID_PARAMETER
	# Visibility changes drive per-peer spawn and stream fate through the
	# sweep, the consumed counterpart of native _update_sync_visibility.
	var visibility_handler := _on_sync_visibility_changed.bind(root)
	if not sync.visibility_changed.is_connected(visibility_handler):
		sync.visibility_changed.connect(visibility_handler)
	if _binding_of(sync):
		return OK

	# The config is bound live, never snapshotted: native registration happens
	# once at tree entry, possibly before the config even exists, and every
	# later assignment or in-place mutation is honored by re-deriving the field
	# lists at each use.
	var binding := _Consumed.new()
	binding.sync_ref = weakref(sync)
	binding.root_ref = weakref(root)
	_refresh_binding(binding)
	_consumed.append(binding)
	_capture_declaration(binding)
	_refresh_binding_intent(binding)
	_refresh_interest_intent(root)
	return OK


## Releases the consumed binding for [param sync] and its per-recipient
## baselines. The counterpart of [method consume], reached through
## [method NetwMultiplayer._object_configuration_remove] when the synchronizer
## exits the tree.
func consume_remove(root: Node, sync: MultiplayerSynchronizer) -> Error:
	var visibility_handler := _on_sync_visibility_changed.bind(root)
	if is_instance_valid(sync) \
			and sync.visibility_changed.is_connected(visibility_handler):
		sync.visibility_changed.disconnect(visibility_handler)
	for i in range(_consumed.size() - 1, -1, -1):
		if _consumed[i].sync() == sync:
			_drop_declaration(_consumed[i])
			_disconnect_config(_consumed[i])
			_watch_book.reset(_consumed[i].get_instance_id())
			_consumed.remove_at(i)
	_refresh_interest_intent(root)
	return OK


func _on_sync_visibility_changed(_for_peer: int, root: Node) -> void:
	var api := _api()
	if api:
		_refresh_binding_intents_for_root(root)
		_refresh_interest_intent(root)
		api._replication._spawn_pipeline.schedule_visibility_sweep()


## Refreshes every event-fed [NetwInterestEngine] synchronizer intent row.
func refresh_interest_intents() -> void:
	var roots: Dictionary[Node, bool] = { }
	for binding in _consumed:
		var root := binding.root()
		if is_instance_valid(root):
			roots[root] = true
			_refresh_binding_intent(binding)
	for root in roots:
		_refresh_interest_intent(root)


func _refresh_interest_intent(root: Node) -> void:
	var api := _api()
	if not api or not api.is_server() or not is_instance_valid(root):
		return
	var entity := NetwEntity.of(root)
	if not entity:
		return
	var entity_root := entity.owner
	if not is_instance_valid(entity_root):
		return
	var admitted: Array[int] = []
	for peer_id in api._interest._known_peer_ids():
		if synchronizer_verdict(peer_id, entity_root):
			admitted.append(peer_id)
	api._interest._set_entity_intent(entity, admitted)


func _refresh_binding_intents_for_root(root: Node) -> void:
	for binding in _consumed:
		if binding.root() == root:
			_refresh_binding_intent(binding)


func _refresh_binding_intent(binding: _Consumed) -> void:
	var api := _api()
	var root := binding.root()
	if not api or not is_instance_valid(root):
		return
	var peer_ids: Array[int] = []
	if api.is_server():
		peer_ids = api._interest._known_peer_ids()
	elif api.has_multiplayer_peer():
		peer_ids.assign(api.get_peers())
	binding.intent_by_peer.clear()
	for peer_id in peer_ids:
		binding.intent_by_peer[peer_id] = synchronizer_verdict(peer_id, root)


func _binding_of(sync: MultiplayerSynchronizer) -> _Consumed:
	for binding in _consumed:
		if binding.sync() == sync:
			return binding
	return null


# Snapshots the binding field lists after registration or a config change.
func _refresh_binding(binding: _Consumed) -> void:
	var sync := binding.sync()
	var cfg := sync.replication_config if is_instance_valid(sync) else null
	var previous := binding.config_ref.get_ref() as SceneReplicationConfig \
	if binding.config_ref else null
	if cfg == previous and not binding.config_dirty:
		return
	_disconnect_config(binding)
	if cfg:
		binding.config_ref = weakref(cfg)
		binding.config_changed = _on_config_changed.bind(binding)
		if not cfg.changed.is_connected(binding.config_changed):
			cfg.changed.connect(binding.config_changed)
	binding.config_dirty = false
	binding.sync_paths.clear()
	var watch: Array[NodePath] = []
	if cfg:
		for path: NodePath in cfg.get_properties():
			if path.get_subname_count() == 0:
				continue
			match cfg.property_get_replication_mode(path):
				SceneReplicationConfig.REPLICATION_MODE_ALWAYS:
					binding.sync_paths.append(path)
				SceneReplicationConfig.REPLICATION_MODE_ON_CHANGE:
					watch.append(path)
	if watch.size() > WATCH_LIMIT:
		if not binding.poisoned:
			_poison(
				binding,
				0,
				"watches %d properties, above the 64-bit delta mask limit"
				% watch.size(),
			)
		watch.resize(WATCH_LIMIT)
	if watch != binding.watch_paths:
		binding.watch_paths = watch
		_watch_book.reset(binding.get_instance_id())
	binding.schema_hash = _schema_hash(binding)
	_declare_model_row(binding)


# Marks one config snapshot dirty and rebuilds it between pump passes.
func _on_config_changed(binding: _Consumed) -> void:
	binding.config_dirty = true


# Disconnects one SceneReplicationConfig change hook.
func _disconnect_config(binding: _Consumed) -> void:
	var cfg := binding.config_ref.get_ref() as SceneReplicationConfig \
	if binding.config_ref else null
	if cfg and binding.config_changed.is_valid() \
			and cfg.changed.is_connected(binding.config_changed):
		cfg.changed.disconnect(binding.config_changed)
	binding.config_ref = null
	binding.config_changed = Callable()


# The 16-bit schema fingerprint of a translated set: field paths in wire
# order, sync row and watch row kept distinct, so two peers whose configs
# disagree on membership or order poison instead of misreading bytes.
static func _schema_hash(binding: _Consumed) -> int:
	var parts: Array[String] = []
	for path in binding.sync_paths:
		parts.append("s" + String(path))
	for path in binding.watch_paths:
		parts.append("w" + String(path))
	return "|".join(parts).hash() & 0xFFFF

#region Pump

## Pumps every consumed binding once: the throttled
## [constant NetwFrameEnvelope.Channel.SYNC] row to the binding's current
## recipients and the per-recipient masked
## [constant NetwFrameEnvelope.Channel.SYNC_DELTA]. Driven by
## [method ReplicationCore.on_clock_tick], or once per
## [method MultiplayerAPI.poll] when no [MultiplayerClock] is configured, the
## native per-network-process cadence.
func pump() -> void:
	var api := _api()
	if not api or not api.inner.multiplayer_peer:
		return
	var native_core := api._native_core
	var repl := api._replication
	var now := Time.get_ticks_usec()

	_prune()

	# A clock can tick before the session assigns a role, and with no role there
	# are no live routes to gather for, so the pump has nothing to do and must
	# not read the role-dependent host flag yet.
	if api.role == SessionCore.Role.NONE:
		return
	var host := api.is_host

	for binding: _Consumed in _consumed.duplicate():
		var sync := binding.sync()
		var root := binding.root()
		if not is_instance_valid(sync) or not is_instance_valid(root):
			continue
		if not sync.is_inside_tree():
			continue
		_refresh_binding(binding)
		if binding.poisoned:
			continue
		var entity := NetwEntity.of(root)
		if not entity and host and not binding.adopt_attempted:
			# A synced root without identity gets one: synced implies routed. This
			# runs before the empty-field skip so a proxy-family synchronizer that
			# sends through the relay pump (its consumed config is all suppressed)
			# still earns the route the relay addresses it by.
			binding.adopt_attempted = true
			api._replication.adopt_in_place(root)
			continue
		if not entity:
			continue
		# A set with no per-tick fields (a spawn-only config, or a proxy family
		# member that relays its whole payload) is routed but never pumped here.
		if binding.sync_paths.is_empty() and binding.watch_paths.is_empty():
			continue
		if not sync.is_multiplayer_authority():
			continue
		if binding.route <= 0:
			_capture_declaration(binding)
		var route := binding.route
		if route <= 0:
			continue
		var recipients := _recipients_for(binding, entity, route, root)
		if recipients.is_empty():
			continue

		if not binding.sync_paths.is_empty() \
				and _throttle_elapsed(binding.last_sync_usec, sync.replication_interval, now):
			var stock_payload := _encode_sync_frame(binding, route, native_core)
			if not stock_payload.is_empty():
				binding.last_sync_usec = now
				for peer_id in recipients:
					var payload := _run_encode_stage(peer_id, stock_payload)
					if payload.is_empty():
						continue
					repl.send_to(
						peer_id,
						route,
						NetwFrameEnvelope.Channel.SYNC,
						payload,
						false,
						0,
						"",
						true,
					)
					_sync_frames_out += 1

		if not binding.watch_paths.is_empty() \
				and _throttle_elapsed(binding.last_watch_usec, sync.delta_interval, now):
			binding.last_watch_usec = now
			_poll_watchers(binding)
			_send_deltas(binding, route, recipients, native_core, repl)

		# A baseline held against a peer that lost the route must not survive
		# to its next admission: absence is what triggers the full-row heal.
		_watch_book.retain_baselines(
			binding.get_instance_id(),
			PackedInt32Array(recipients),
		)


func _prune() -> void:
	for i in range(_consumed.size() - 1, -1, -1):
		if not is_instance_valid(_consumed[i].sync()):
			_drop_declaration(_consumed[i])
			_disconnect_config(_consumed[i])
			_watch_book.reset(_consumed[i].get_instance_id())
			_consumed.remove_at(i)


# Native throttle semantics: interval 0 sends every pass, otherwise the
# interval in seconds must have elapsed since the last send in usec.
static func _throttle_elapsed(last_usec: int, interval: float, now: int) -> bool:
	if interval <= 0.0 or last_usec < 0:
		return true
	return now - last_usec >= int(interval * 1_000_000.0)


# The authority's recipient set for one binding this pass. The server clamps
# the spawn book's per-record recipients by interest admission and the
# synchronizer visibility verdict, so no peer is sent state for a node it was
# never sent. A client authority mirrors the native rule: every peer it knows,
# gated by its own local visibility verdict.
func _recipients_for(
		binding: _Consumed,
		entity: NetwEntity,
		route: int,
		root: Node,
) -> Array[int]:
	var api := _api()
	var out: Array[int] = []
	if not api:
		return out
	var local_id := api.get_unique_id()
	if api.is_host:
		var pipeline := api._replication._spawn_pipeline
		if pipeline._spawn_book.has_armed(route):
			# The SPAWN has not flushed yet, so no peer can have the node.
			return out
		var base: Array[int] = []
		var record := pipeline._spawn_book.spawned_of(route)
		if record:
			if api.entity_get_state(
					api.entity_from_route(route)) != NetwMultiplayer.EntityState.LIVE:
				return out
			base.assign(record.recipients())
		else:
			base = api._replication.live_peers(entity)
		# The filter verdict is entity-wide, so resolve it once outside the loop.
		var filtered := api.interest_is_filtered(api.entity_of(entity.owner))
		for peer_id in base:
			if peer_id == local_id:
				continue
			if filtered and not api._interest.wire_admits(peer_id, entity):
				continue
			if not binding.intent_by_peer.get(peer_id, false):
				continue
			out.append(peer_id)
	else:
		for peer_id in api.inner.get_peers():
			if peer_id == local_id:
				continue
			if not synchronizer_verdict(peer_id, root):
				continue
			out.append(peer_id)
	return out


## Mirrors the native spawn and sync visibility OR-composition over
## [param node]'s subtree: authority-held synchronizers whose resolved root is
## [param node] vote with [member MultiplayerSynchronizer.public_visibility]
## and their per-peer overrides, and a subtree with no such synchronizer is
## visible. [NetwSpawnPipeline] consults this for spawn fate and the pump
## consults it for stream fate, so both edges always agree. Filter callables
## are not script-readable, so interest-installed filters are composed by the
## callers through [InterestCore] directly.
func synchronizer_verdict(peer_id: int, node: Node) -> bool:
	var api := _api()
	var local_id := api.get_unique_id() if api and api.inner.multiplayer_peer else 1
	var found := false
	var stack: Array[Node] = [node]
	while not stack.is_empty():
		var n: Node = stack.pop_back()
		var sync := n as MultiplayerSynchronizer
		if sync and sync.get_multiplayer_authority() == local_id \
				and sync.get_node_or_null(sync.root_path) == node:
			found = true
			if sync.public_visibility or sync.get_visibility_for(peer_id):
				return true
		for child in n.get_children():
			stack.append(child)
	return not found


# Gathers the binding's sync row and encodes it through the shared kernel. An
# unresolved field target skips the whole pass so a half-row never crosses the
# wire.
func _encode_sync_frame(
		binding: _Consumed,
		route: int,
		native_core: NetwMultiplayerCore,
) -> PackedByteArray:
	var values := _gather_paths(binding, binding.sync_paths)
	if values.size() != binding.sync_paths.size():
		return PackedByteArray()
	return NetwSyncKernel.encode_volatile(
		_ordinal_of(binding, route, native_core),
		values,
	)


# Reads every watch field off the node and hands the row to the shared watch
# book, which stamps the changed fields. The book owns the compare and stamp,
# the compat owns the node addressing.
func _poll_watchers(binding: _Consumed) -> void:
	var readable: Array = []
	var values := _gather_paths(
		binding,
		binding.watch_paths,
		true,
		readable,
	)
	_watch_book.poll(binding.get_instance_id(), values, readable)


# Sends one masked SYNC_DELTA per recipient whose baseline is older than a
# watched field's last change. An absent baseline is the gain edge and heals
# with the full watched row, exactly the native zero-baseline mechanism.
func _send_deltas(
		binding: _Consumed,
		route: int,
		recipients: Array[int],
		native_core: NetwMultiplayerCore,
		repl: ReplicationCore,
) -> void:
	if not _watch_book.is_inited(binding.get_instance_id()):
		return
	var ordinal := _ordinal_of(binding, route, native_core)
	for peer_id in recipients:
		var selected := _watch_book.mask_for(binding.get_instance_id(), peer_id)
		var mask: int = selected[0]
		var values: Array = selected[1]
		_watch_book.commit(binding.get_instance_id(), peer_id)
		if mask == 0:
			continue
		var stock_bytes := NetwSyncKernel.encode_retained(ordinal, mask, values)
		var bytes := _run_encode_stage(peer_id, stock_bytes)
		if bytes.is_empty():
			continue
		repl.send_to(
			peer_id,
			route,
			NetwFrameEnvelope.Channel.SYNC_DELTA,
			bytes,
			true,
			0,
			"",
			true,
		)
		_delta_frames_out += 1


# Routes consumed bytes through the installed independent encode stage.
func _run_encode_stage(peer: int, stock: PackedByteArray) -> PackedByteArray:
	var api := _api()
	if not api:
		return stock
	api._sync_encoder = func(_peer: int, _tick: int) -> PackedByteArray:
		return stock
	api._sync_encode_meta = { }
	var tick := api.clock.tick if api.clock.is_configured() else 0
	var bytes := api._sync_encode(peer, tick)
	api._sync_encoder = Callable()
	api.report_event(
		NetwMultiplayerCore.SYNC_ENCODE,
		0,
		{ bytes = bytes.size() },
		peer,
	)
	return bytes

#endregion

#region Receive

## Applies one [constant NetwFrameEnvelope.Channel.SYNC] payload to the
## consumed binding it addresses, then emits
## [signal MultiplayerSynchronizer.synchronized] on the stock node and feeds
## the applied values to [DisplayCore]. The sender must be the
## target synchronizer's authority. Called from the receive dispatch after
## datagram freshness gating.
func handle_sync(entity: NetwEntity, payload: PackedByteArray, sender: int) -> void:
	var api := _api()
	if not api:
		return
	var route := api._native_core.liveness_route_of(entity)
	var r := NetwBitBufferReader.create(payload)
	var ordinal := NetwCodec.get_safe_varint(r)
	var binding := _binding_by_ordinal(route, ordinal)
	if not binding:
		_drops_sync_no_set += 1
		return
	_refresh_binding(binding)
	_validate_schema(binding, route, api._native_core)
	if binding.poisoned:
		_drops_sync_poisoned += 1
		return
	var sync := binding.sync()
	if sender != sync.get_multiplayer_authority():
		_drops_sync_bad_sender += 1
		return
	var flags := r.get_aligned_u8()
	if flags != NetwSyncKernel.RESERVED:
		# A sender that wrote the reserved byte speaks a newer grammar. Drop
		# loudly instead of misreading the framed payload.
		_drops_sync_unknown_flag += 1
		push_warning(
			"NetwSyncCompat: consumed synchronizer '%s' SYNC flags %d unimplemented, "
			% [sync.name, flags]
			+ "frame dropped. Peers must run the same Networked version.",
		)
		return
	var keys: Array[StringName] = []
	for path: NodePath in binding.sync_paths:
		keys.append(StringName(path))
	var decoded: Array[NetwStagedWrites] = [null]
	var verdict := _run_decode_stage(
		entity.rid,
		binding.comp,
		flags,
		-1,
		payload,
		func() -> Error:
			decoded[0] = NetwSyncKernel.decode_volatile(payload, keys)
			return OK if decoded[0] else ERR_INVALID_DATA,
	)
	var staged := decoded[0]
	if verdict != OK or staged == null \
			or staged.values.size() != binding.sync_paths.size():
		_drops_sync_poisoned += 1
		_poison(binding, route, "SYNC row size mismatch")
		return
	if _apply_paths(binding, binding.sync_paths, staged.values) != OK:
		return
	_sync_frames_in += 1
	sync.synchronized.emit()
	_feed_interpolation(binding)


## Applies one [constant NetwFrameEnvelope.Channel.SYNC_DELTA] payload: the
## mask selects watch fields in the set's watch order and the values apply
## positionally, then [signal MultiplayerSynchronizer.delta_synchronized]
## fires on the stock node. Same authority rule as [method handle_sync].
func handle_sync_delta(entity: NetwEntity, payload: PackedByteArray, sender: int) -> void:
	var api := _api()
	if not api:
		return
	var route := api._native_core.liveness_route_of(entity)
	var r := NetwBitBufferReader.create(payload)
	var ordinal := NetwCodec.get_safe_varint(r)
	var binding := _binding_by_ordinal(route, ordinal)
	if not binding:
		_drops_sync_no_set += 1
		return
	_refresh_binding(binding)
	_validate_schema(binding, route, api._native_core)
	if binding.poisoned:
		_drops_sync_poisoned += 1
		return
	var sync := binding.sync()
	if sender != sync.get_multiplayer_authority():
		_drops_sync_bad_sender += 1
		return
	var mask := NetwCodec.get_safe_varint(r)
	var indexes: Array[int] = []
	for i in binding.watch_paths.size():
		if mask & (1 << i):
			indexes.append(i)
	var keys: Array[StringName] = []
	for path: NodePath in binding.watch_paths:
		keys.append(StringName(path))
	var decoded: Array[NetwStagedWrites] = [null]
	var verdict := _run_decode_stage(
		entity.rid,
		binding.comp,
		0,
		-1,
		payload,
		func() -> Error:
			decoded[0] = NetwSyncKernel.decode_retained(payload, keys)
			return OK if decoded[0] else ERR_INVALID_DATA,
	)
	var staged := decoded[0]
	if verdict != OK or staged == null \
			or staged.values.size() != indexes.size():
		_drops_sync_poisoned += 1
		_poison(binding, route, "SYNC_DELTA mask and value count disagree")
		return
	var paths: Array[NodePath] = []
	for index: int in indexes:
		paths.append(binding.watch_paths[index])
	if _apply_paths(binding, paths, staged.values) != OK:
		return
	_delta_frames_in += 1
	sync.delta_synchronized.emit()
	_feed_interpolation(binding)


# Routes consumed decode through the installed independent virtual stage.
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


# Feeds the interpolation engine a consumed native sync's just-applied values.
# The feeder owns the mapping from replicated paths to display channels: it
# resolves [key -> [node, property, spec]] once per binding, then reads each
# value back off the node at the receive tick and hands the engine plain data
# through its record door. Only a public synchronizer feeds display, so a private
# stream never writes a display buffer.
func _feed_interpolation(binding: _Consumed) -> void:
	var api := _api()
	var iface := api._display if api else null
	if not iface:
		return
	var sync := binding.sync()
	if not is_instance_valid(sync) \
			or not sync.replication_config \
			or not sync.public_visibility:
		return
	var paths := _display_paths_for(binding, sync)
	if paths.is_empty():
		return
	var tick := api.clock.tick if api.clock.is_configured() else 0
	for entry: Array in paths:
		var node := entry[0] as Node
		if not is_instance_valid(node):
			binding.display_feed_built = false
			continue
		var prop := entry[1] as StringName
		var spec := NetwScriptModel.get_node_property_interpolator(node, prop)
		if spec:
			iface._record(node, prop, node.get(prop), tick, spec, false)


# Resolves and caches the consumed sync's replicated [node, property] paths on the
# binding, the expensive structural half of the feed mapping. The interpolator
# spec is looked up live per record so late spec registration still takes effect.
func _display_paths_for(
		binding: _Consumed,
		sync: MultiplayerSynchronizer,
) -> Array:
	if binding.display_feed_built:
		return binding.display_feed_paths
	var out: Array = []
	var root := binding.root()
	if is_instance_valid(root):
		for db: Array in NetwSynchronizers.display_bindings(sync, root):
			out.append([db[1] as Node, db[2] as StringName])
	binding.display_feed_paths = out
	binding.display_feed_built = true
	return out

#endregion

#region Schema descriptors

## Appends the [constant NetwFrameEnvelope.Channel.SPAWN] frame's consumed
## sync-set descriptor section for [param route]: a count, then one
## [code][ordinal varint | schema hash u16][/code] per consumed set, so the
## receiver validates its translated sets against the sender's at spawn time.
func encode_descriptors(w: NetwBitBufferWriter, route: int) -> void:
	var api := _api()
	var rows: Array[NetwSyncSetRow] = []
	if api:
		for row: NetwSyncSetRow in api._replication.sync_model.route_rows(
			route,
		):
			if row.kind == NetwSyncModel.Kind.KIND_CONSUMED:
				rows.append(row)
	NetwCodec.put_varint(w, rows.size())
	for row: NetwSyncSetRow in rows:
		NetwCodec.put_varint(w, row.ordinal)
		w.put_aligned_u16(row.schema_hash)


## Records the descriptor section decoded from a
## [constant NetwFrameEnvelope.Channel.SPAWN] frame, validated lazily against
## this peer's bindings as they register and resolve.
func note_schema(route: int, descriptors: Dictionary) -> void:
	if descriptors.is_empty():
		_pending_schema.erase(route)
	else:
		_pending_schema[route] = descriptors


func _validate_schema(
		binding: _Consumed,
		route: int,
		native_core: NetwMultiplayerCore,
) -> void:
	if binding.schema_checked:
		return
	var pending: Dictionary = _pending_schema.get(route, { })
	if pending.is_empty():
		return
	binding.schema_checked = true
	var ordinal := _ordinal_of(binding, route, native_core)
	if not pending.has(ordinal):
		return
	if int(pending[ordinal]) != binding.schema_hash:
		_poison(
			binding,
			route,
			"schema hash %04x disagrees with the spawn descriptor %04x"
			% [binding.schema_hash, int(pending[ordinal])],
		)


func _poison(binding: _Consumed, route: int, reason: String) -> void:
	binding.poisoned = true
	var sync := binding.sync()
	push_error(
		"NetwSyncCompat: consumed synchronizer '%s' (route %d) poisoned: %s. "
		% [sync.name if sync else "<freed>", route, reason]
		+ "Its replication config must be identical on every peer.",
	)

#endregion

#region Ordinals

# A route's consumed bindings in captured wire-ordinal order.
func _route_group(route: int, _native_core: NetwMultiplayerCore = null) \
-> Array[_Consumed]:
	var out: Array[_Consumed] = []
	var api := _api()
	if not api:
		return out
	for row: NetwSyncSetRow in api._replication.sync_model.route_rows(route):
		if row.kind != NetwSyncModel.Kind.KIND_CONSUMED:
			continue
		for binding: _Consumed in _consumed:
			if binding.route == route and binding.order_key == row.key:
				out.append(binding)
				break
	return out


func _ordinal_of(binding: _Consumed, route: int, native_core: NetwMultiplayerCore) -> int:
	var api := _api()
	var row := api._replication.sync_model.row_for(
		route,
		NetwSyncModel.Kind.KIND_CONSUMED,
		binding.order_key,
	) if api else null
	return row.ordinal if row else -1


func _binding_by_ordinal(route: int, ordinal: int) -> _Consumed:
	var api := _api()
	if not api or ordinal < 0:
		return null
	var row := api._replication.sync_model.row(route, ordinal)
	if row == null or row.kind != NetwSyncModel.Kind.KIND_CONSUMED:
		return null
	for binding: _Consumed in _consumed:
		if binding.route == route and binding.order_key == row.key:
			return binding
	return null


# Captures one synchronizer's route, component, and order key once.
func _capture_declaration(binding: _Consumed) -> void:
	var root := binding.root()
	var sync := binding.sync()
	var entity := NetwEntity.of(root) if is_instance_valid(root) else null
	var api := _api()
	if not entity or not api or not is_instance_valid(sync) \
			or not is_instance_valid(entity.owner):
		return
	binding.route = api.entity_get_route(entity.rid)
	binding.entity = entity.rid
	binding.order_key = StringName(entity.owner.get_path_to(sync))
	var target := api._replication._resolve_comp(entity, root)
	binding.comp = int(target.get("comp", 0))
	_declare_model_row(binding)


# Writes a captured consumed declaration into the value model.
func _declare_model_row(binding: _Consumed) -> void:
	var api := _api()
	if not api or binding.route <= 0 or binding.order_key.is_empty():
		return
	api._replication.sync_model.declare(
		binding.route,
		NetwSyncModel.Kind.KIND_CONSUMED,
		binding.order_key,
		binding.comp,
		RID(),
		0,
		binding.schema_hash,
	)


# Removes one consumed declaration row.
func _drop_declaration(binding: _Consumed) -> void:
	var api := _api()
	if api and binding.route > 0 and not binding.order_key.is_empty():
		api._replication.sync_model.drop(
			binding.route,
			NetwSyncModel.Kind.KIND_CONSUMED,
			binding.order_key,
		)
	binding.route = 0


# Binds synchronizers that registered before their entity route went live.
func _on_entity_live(route: int, entity: NetwEntity) -> void:
	for binding: _Consumed in _consumed:
		var root := binding.root()
		if is_instance_valid(root) and NetwEntity.of(root) == entity:
			binding.route = route
			_capture_declaration(binding)

#endregion

#region Field access

# Gathers path values through the installed value-only reader stage.
func _gather_paths(
		binding: _Consumed,
		paths: Array[NodePath],
		allow_missing: bool = false,
		readable: Array = [],
) -> Array:
	var root := binding.root()
	var gatherer := func() -> Array:
		var values: Array = []
		for path: NodePath in paths:
			var read := _read_path(root, path)
			readable.append(bool(read[0]))
			if not read[0] and not allow_missing:
				return []
			values.append(read[1] if read[0] else null)
		return values
	var api := _api()
	var values: Array = api._run_gather_set(
		binding.entity,
		binding.comp,
		gatherer,
	) if api else gatherer.call()
	if readable.size() != paths.size() and values.size() == paths.size():
		readable.clear()
		for _path in paths:
			readable.append(true)
	return values


# Applies path values through the installed value-only writer stage.
func _apply_paths(
		binding: _Consumed,
		paths: Array[NodePath],
		values: Array,
) -> Error:
	var root := binding.root()
	var applier := func(staged: Array) -> Error:
		if staged.size() != paths.size():
			return ERR_INVALID_DATA
		for index: int in paths.size():
			_write_path(root, paths[index], staged[index])
		return OK
	var api := _api()
	return api._run_apply_set(
		binding.entity,
		binding.comp,
		values,
		applier,
	) if api else applier.call(values)


# Resolves a config property path against [param root] the way native
# get_state does: names select the sub-node, subnames the property chain.
# Returns [resolved, value] so a null value stays distinguishable.
static func _read_path(root: Node, path: NodePath) -> Array:
	var target := root
	var names := NodePath(path.get_concatenated_names())
	if not names.is_empty():
		target = root.get_node_or_null(names)
	if not is_instance_valid(target):
		return [false, null]
	return [true, target.get_indexed(NodePath(":" + path.get_concatenated_subnames()))]


static func _write_path(root: Node, path: NodePath, value: Variant) -> void:
	var target := root
	var names := NodePath(path.get_concatenated_names())
	if not names.is_empty():
		target = root.get_node_or_null(names)
	if not is_instance_valid(target):
		return
	target.set_indexed(NodePath(":" + path.get_concatenated_subnames()), value)

#endregion

## Drops all per-session consumption state. Bindings survive because they are
## declarations of nodes still in the tree, which re-derive their routes and
## ordinals against the next session.
func clear_session() -> void:
	_pending_schema.clear()
	for binding in _consumed:
		_watch_book.clear_baselines(binding.get_instance_id())
		binding.schema_checked = false
		binding.poisoned = false
		binding.last_sync_usec = -1
		binding.last_watch_usec = -1
		binding.route = 0


## Drops [param route]'s pending schema descriptors when its [NetwEntity]
## despawns.
func clear_route(route: int) -> void:
	_pending_schema.erase(route)
	for binding: _Consumed in _consumed:
		if binding.route == route:
			binding.route = 0


## Drops the delta baselines held against [param peer_id], so a reconnecting
## peer heals with the full watched row.
func clear_peer(peer_id: int) -> void:
	_watch_book.clear_peer(peer_id)


## Releases declaration and config signal bindings.
func dispose() -> void:
	var api := _api()
	if api and api.entity_live.is_connected(_on_entity_live):
		api.entity_live.disconnect(_on_entity_live)
	for binding: _Consumed in _consumed:
		_disconnect_config(binding)


## Returns this adapter's contribution to
## [method ReplicationCore.counters].
func counters() -> Dictionary:
	return {
		&"sync_frames_out": _sync_frames_out,
		&"sync_frames_in": _sync_frames_in,
		&"delta_frames_out": _delta_frames_out,
		&"delta_frames_in": _delta_frames_in,
		&"sync_sets_active": _consumed.size(),
		&"drops_sync_no_set": _drops_sync_no_set,
		&"drops_sync_bad_sender": _drops_sync_bad_sender,
		&"drops_sync_poisoned": _drops_sync_poisoned,
		&"drops_sync_unknown_flag": _drops_sync_unknown_flag,
	}
