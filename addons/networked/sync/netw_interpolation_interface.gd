## Smooths a networked entity's displayed motion between the sparse snapshots
## that arrive over the wire.
##
## Snapshots land every few ticks and jittery, so the display never shows the
## newest one raw. It plays the visual back behind the live state by a buffer
## delay, so there is always a later snapshot to interpolate toward and motion
## stays smooth across a gap. The trade is a fixed display latency of roughly
## that buffer depth. This is the display half of a networked entity.
## [PredictionComponent] owns the body, this owns only what the body looks like.
## One always-mounted service drives every entity in a single pump. Per value
## config is a [NetwInterpolate] spec, authored in code through
## [method Netw.configure_property] or in the inspector through
## [MultiplayerInterpolator]. Each spec defines one channel, an independently
## smoothed value stream with its own history. Entity display settings live on
## [NetwInterpolationInterface.Handle].
## [codeblock]
## Netw.configure_property(self, &"position").interpolate(
##     NetwInterpolate.new().lerp().smooth(0.05)
## )
## entity.interpolation.visual_root = NodePath("sprite")
## [/codeblock]
##
## [b]The playhead[/b]
## [br]Each tracked value keeps a [NetwRingBuffer] of recorded snapshots keyed by
## tick, reachable through
## [method NetwInterpolationInterface.Handle.get_buffer]. A playhead reads that
## buffer behind the newest entry, lands between two recorded ticks, and the
## displayed value is the interpolation between them.
## [member NetwInterpolationInterface.Handle.display_lag] is how far behind the
## newest entry the playhead sits.
## [codeblock]
## history (one per value), keyed by tick:
##    9       12       15       18       21        newest received = 21
##                     |---- playhead ---|
##                     prev = 15   dt = 16.4   next = 18
##
## displayed = lerp(state[15], state[18], 0.4)
## [/codeblock]
##
## [b]Display roles[/b]
## [br]The same entity is displayed by one of four roles, resolved from
## [NetwEntity] control, [member Node.multiplayer_authority], and the authored
## sync channels unless
## [member NetwInterpolationInterface.Handle.display_role] overrides it. The
## question each rule answers is where the displayed values come from. A
## predicted entity is simulated live by this peer. A locally controlled body
## is truth and keeps raw input feel. An entity whose sync channels are all
## authored here has no receive stream, so its display samples the local
## simulation each tick. Everything else is somebody's stream to play back.
## [codeblock]
## # resolved per entity
## if handle.display_role != DisplayRole.AUTO:
##     return handle.display_role
## if has_prediction and entity.is_controlled_locally:
##     return DisplayRole.PREDICTED   # we simulate it live; chase the body
## if owner.is_multiplayer_authority() and entity.is_controlled_locally:
##     return DisplayRole.DISABLED    # our own body is truth; keep input feel
## if every feeding sync channel is authored here:
##     return DisplayRole.AUTHORITY   # we author the stream; sample our sim
## if owner.is_multiplayer_authority():
##     return DisplayRole.DISABLED    # nothing streams this; the body is truth
## return DisplayRole.REMOTE          # someone else's stream; play it back
## [/codeblock]
##
## [b]Playing the playhead[/b]
## [br]Once per frame the playhead is placed in the history's tick space from the
## [MultiplayerClock] display clock, then every value lerps across the two ticks
## that bracket it. [member NetwClockInterface.display_tick] already trails the
## simulation by [member NetwClockInterface.display_offset], and
## [member NetwInterpolationInterface.Handle.display_lag] subtracts the extra
## jitter buffer on top. An empty history is never written, so the pump defers to
## whatever last wrote the property instead of clobbering it with a stale value.
## [codeblock]
## time := clock.display_tick + clock.tick_factor - display_lag
## dt := floori(time)              # the playhead tick
## factor := time - dt             # progress from dt toward dt + 1
## every value: write lerp(history[prev], history[next], factor)
## [/codeblock]
##
## [b]Smart dilation[/b]
## [br]The buffer depth adapts.
## [member NetwInterpolationInterface.Handle.display_lag] eases toward a floor
## set by the expected snapshot interval and the clock display offset. When
## snapshots starve, no recorded tick sits after the playhead, so the lag grows
## to rebuild the buffer and settles back once packets resume.
## [member NetwInterpolationInterface.Handle.enable_smart_dilation] turns the
## loop off for a fixed zero lag, and the tuning members on
## [NetwInterpolationInterface.Handle] shape the rest.
##
## [br][br][b]Naming the displayed tick[/b]
## [br]When a stamped derived state set drives the entity, history is keyed by
## the frame's authoring tick instead of the receive tick. That lets
## [method NetwInterpolationInterface.Handle.displayed_authoring_tick] name the
## exact server tick under the playhead, which a firing client sends to the
## server for lag-compensated rewind through [NetwLagCompensationInterface]. The displayed
## tick is half a round trip behind the live server state, which is exactly the
## world the shooter saw.
## [codeblock]
## packet { __tick = 15, position = ... }   recorded at history key 15
## playhead under tick 15   ->   displayed_authoring_tick() == 15
## the shooter sends 15; the server rewinds every target to tick 15
## [/codeblock]
##
## [b]Predicted display[/b]
## [br]A locally predicted entity has no jitter buffer to drain, because its body
## is simulated live every frame and races ahead of the network.
## [member NetwInterpolationInterface.Handle.predicted_mode] picks the filter.
## [constant PredictedMode.CHASE] eases the visual toward the live body with an
## exponential time from
## [member NetwInterpolationInterface.Handle.predicted_smooth_time], the one role
## that reads the body instead of history.
## [constant PredictedMode.BRACKETED] records the predicted body each tick and
## interpolates the previous and current samples exactly like a remote entity, so
## it is the remote pipeline fed by a local sampler.
##
## [br][br][b]Writing the visual[/b]
## [br]Smoothing must not fight the physics engine.
## [member NetwInterpolationInterface.Handle.visual_root] points at a child
## that carries the smooth output while the body, [member NetwEntity.owner],
## keeps the raw pose that the derived state set and native replication write.
## There are two write-out modes, chosen by the visual's
## [member CanvasItem.top_level] ([member Node3D.top_level] in 3D).
## A parented visual takes global-space writes for the
## [constant GLOBAL_SPACE_CHANNELS], so a replicated write to the body never
## moves them through transform inheritance, while every channel without a
## [NetwInterpolate] spec still inherits from the body for free, a facing
## flip, an animation bob, or a riding attachment among them. Any other
## spatial channel has no per-channel global setter, so the service warns
## when it builds that channel's display state. A top-level visual is a
## detached ghost. The parent never composes, so every written channel is
## absolute, nothing inherits, and any channel can be smoothed,
## [member Node2D.transform] included.
## [codeblock]
## # Parented visual: smooth position, inherit the rest from the body.
## entity.interpolation.visual_root = NodePath("sprite")
##
## # Detached ghost: smooth the whole pose, inherit nothing.
## $Visual.top_level = true
## entity.interpolation.visual_root = NodePath("Visual")
## [/codeblock]
## Without a visual root, and for non-spatial values, the write is plain.
## [method NetwInterpolationInterface.Handle.snap_property] and
## [method NetwInterpolationInterface.Handle.reset] bypass smoothing for teleports.
##
## [br][br][b]RPC and signal arguments[/b]
## [br]Discrete events carry continuous values too. A [NetwInterpolate] per
## argument on [method Netw.configure_rpc] or [method Netw.configure_signal]
## smooths that argument into the property named by [member NetwInterpolate.target]
## while the handler still runs, so a periodic aim or nudge glides between events.
## Unlike a property, an argument has no implicit source, so
## [member NetwInterpolate.target] must be set.
class_name NetwInterpolationInterface
extends NetwService

## Selects the display role when automatic role resolution is not desired.
enum DisplayRole {
	## Resolve from [NetwEntity] control and [member Node.multiplayer_authority].
	AUTO = 0,
	## Use the remote snapshot interpolation path.
	REMOTE = 1,
	## Use the local predicted display path.
	PREDICTED = 2,
	## Disable display smoothing.
	DISABLED = 3,
	## Sample the locally authored simulation each tick and play it back.
	AUTHORITY = 4,
}

## Selects how predicted entities drive their visual output.
enum PredictedMode {
	## Ease the visual toward the live predicted body every frame.
	CHASE = 0,
	## Interpolate between the previous and current tick samples.
	BRACKETED = 1,
}

## Channels a parented visual accepts as global-space writes.
##
## These are the channels with a per-channel global setter, so the smoothed
## write escapes transform inheritance from the body. Position qualifies as
## [member Node2D.position] and [member Node3D.position], rotation only as
## [member Node2D.rotation]. Any other spatial channel on a parented visual
## warns when its display state is built.
const GLOBAL_SPACE_CHANNELS: Array[StringName] = [&"position", &"rotation"]

const _MAX_CLOCK_BIND_ATTEMPTS := 60

# Internal pump modes resolved from the display role and predicted mode.
const _PUMP_UNRESOLVED := -1
const _PUMP_DISABLED := 0
const _PUMP_REMOTE := 1
const _PUMP_BRACKETED := 2
const _PUMP_CHASE := 3

var _runtimes: Dictionary[int, _Runtime] = { }
var _action_gates: Dictionary[int, _ActionGate] = { }
var _clock: NetwClockInterface
var _liveness: NetwLivenessInterface
var _clock_connected := false
var _clock_bind_attempts := 0
var _liveness_connected := false
var _last_update_frame := -1
var _dbg: NetwHandle = Netw.dbg.handle(self)


## Resolves the service for the [MultiplayerTree] enclosing [param node].
static func for_node(node: Node) -> NetwInterpolationInterface:
	var mt := MultiplayerTree.resolve(node)
	if not mt:
		return null
	var service := mt.get_service(NetwInterpolationInterface) \
			as NetwInterpolationInterface
	if service:
		return service
	return mt.find_service_node(NetwInterpolationInterface) \
			as NetwInterpolationInterface


#region Service

## Returns the service script type for registration.
func service_type() -> Script:
	return NetwInterpolationInterface


## Connects this service to the session clock and liveness registry.
func service_entered(mt: MultiplayerTree) -> void:
	process_priority = 100
	if not mt.session_entered.is_connected(_on_session_entered):
		mt.session_entered.connect(_on_session_entered)
	if not mt.session_ended.is_connected(_on_session_ended):
		mt.session_ended.connect(_on_session_ended)
	_ensure_clock_connection()
	_ensure_liveness_connection()


## Disconnects this service from the session clock and liveness registry.
func service_exiting(mt: MultiplayerTree) -> void:
	if mt.session_entered.is_connected(_on_session_entered):
		mt.session_entered.disconnect(_on_session_entered)
	if mt.session_ended.is_connected(_on_session_ended):
		mt.session_ended.disconnect(_on_session_ended)
	_disconnect_clock()
	_disconnect_liveness()
	_clear_runtimes()


func _on_session_entered() -> void:
	_clock_bind_attempts = 0
	_ensure_clock_connection()
	_ensure_liveness_connection()


func _on_session_ended() -> void:
	_clear_runtimes.call_deferred()


# Resolves the tick engine, or null while no configurator has registered, so
# callers keep today's no-clock fallback semantics against the inert interface.
func _resolve_clock() -> NetwClockInterface:
	var api := NetwMultiplayer.of(self)
	if api and api.clock.is_configured():
		return api.clock
	return null


func _ensure_clock_connection() -> void:
	if _clock_connected:
		return
	var clock := _resolve_clock()
	if clock:
		if not clock.after_tick.is_connected(_on_clock_tick):
			clock.after_tick.connect(_on_clock_tick)
		_clock = clock
		_clock_connected = true
		return
	_clock_bind_attempts += 1
	if _clock_bind_attempts > _MAX_CLOCK_BIND_ATTEMPTS:
		return
	if not is_inside_tree():
		return
	if get_tree().process_frame.is_connected(_ensure_clock_connection):
		return
	get_tree().process_frame.connect(
		_ensure_clock_connection,
		CONNECT_ONE_SHOT,
	)


func _disconnect_clock() -> void:
	if not _clock_connected:
		return
	var clock := _resolve_clock()
	if clock and clock.after_tick.is_connected(_on_clock_tick):
		clock.after_tick.disconnect(_on_clock_tick)
	_clock = null
	_clock_connected = false


# Cached session clock, resolved and memoized on first use.
func _get_clock() -> NetwClockInterface:
	if is_instance_valid(_clock):
		return _clock
	_clock = _resolve_clock()
	if _clock and not _clock_connected:
		_ensure_clock_connection()
	return _clock


func _ensure_liveness_connection() -> void:
	if _liveness_connected:
		return
	var lv := NetwLivenessInterface.for_node(self)
	if not lv:
		return
	if not lv.entity_live.is_connected(_on_entity_live):
		lv.entity_live.connect(_on_entity_live)
	if not lv.entity_dead.is_connected(_on_entity_dead):
		lv.entity_dead.connect(_on_entity_dead)
	_liveness = lv
	_liveness_connected = true


func _disconnect_liveness() -> void:
	if not _liveness_connected:
		return
	var lv := NetwLivenessInterface.for_node(self)
	if lv:
		if lv.entity_live.is_connected(_on_entity_live):
			lv.entity_live.disconnect(_on_entity_live)
		if lv.entity_dead.is_connected(_on_entity_dead):
			lv.entity_dead.disconnect(_on_entity_dead)
	_liveness = null
	_liveness_connected = false

#endregion

#region Record seam

## Records [param value] for [param target_property] on [param node].
##
## [param tick] is the key in that value's history. The interpolation spec is
## resolved from the configuration registry. An unconfigured target is a no-op.
## [param tick] is the local receive tick from a property sync feeder. An
## entity displaying under [constant DisplayRole.PREDICTED] ignores recorded
## values and follows its locally simulated state instead.
func record(
		node: Node,
		target_property: StringName,
		value: Variant,
		tick: int,
) -> void:
	if not is_instance_valid(node):
		return
	var spec := NetwScriptModel.get_node_property_interpolator(
		node,
		target_property,
	)
	if not spec:
		_dbg.warn(
			"record: no interpolator configured for '%s' on '%s'",
			[target_property, node.name],
		)
		return
	_record(node, target_property, value, tick, spec, false)


# Records with the spec already decoded by a receive path. STATE feeders pass
# authoring_tick so the ring buffer carries the authoring tick domain.
func _record(
		node: Node,
		target_property: StringName,
		value: Variant,
		tick: int,
		spec: NetwInterpolate,
		authoring_tick: bool,
) -> void:
	if not is_instance_valid(node):
		return
	if not spec or target_property.is_empty():
		return
	var entity := NetwEntity.of(node)
	if not entity:
		return
	var route := _route_of(entity)
	if route <= 0:
		return
	var runtime := _runtime_for(route, entity)
	if not runtime:
		return
	# A predicted runtime displays the locally simulated body, so network
	# feeds must not write into the same histories the local sampler fills.
	# Prediction corrections rewrite the body itself, and the display picks
	# them up from the local sampling pass.
	_resolve_role(runtime)
	if _pump_is_predicted(runtime.pump_mode):
		return
	var state := runtime.states_by_key.get(
		_state_key_for(node, target_property),
	) as _PropertyState
	if not state:
		# A key naming a real property samples it as its source. Anything
		# else is an event argument stream that only writes its target.
		var is_property := false
		for prop in node.get_property_list():
			if prop["name"] == target_property:
				is_property = true
				break
		var target_prop := target_property
		if is_property and not spec.target.is_empty():
			target_prop = spec.target
		state = _ensure_state(
			runtime,
			node,
			target_property if is_property else &"",
			target_prop,
			spec,
			authoring_tick,
		)
	if not state:
		return
	if authoring_tick:
		state.authoring_ticks = true
	state.history.record(tick, value, authoring_tick)


# Feeds the bracketed pipelines. Records the live source value of every state
# at the current tick so the same resample path the remote role uses can drive
# a locally simulated visual.
func _on_clock_tick(_delta: float, tick: int) -> void:
	for runtime in _runtimes.values():
		if runtime.disabled:
			continue
		if runtime.pump_mode != _PUMP_BRACKETED:
			continue
		for state in runtime.states:
			if not is_instance_valid(state.source_obj):
				continue
			var value: Variant = state.source_obj.get(state.source_prop)
			state.history.record(tick, value, false)

#endregion

#region Runtimes

func _on_entity_live(route: int, entity: NetwEntity) -> void:
	# The action-spawn display gate runs for every routed entity, even one that
	# never builds an interpolation runtime, so it precedes the runtime filter.
	_apply_action_gate(route, entity)
	if not _entity_wants_runtime(entity):
		return
	var runtime := _runtime_for(route, entity)
	if not runtime:
		return
	if runtime.entity_hooks.is_empty():
		_bind_hook(
			runtime.entity_hooks,
			entity,
			entity.control_changed,
			_on_entity_control_changed.bind(runtime.route),
		)
		_bind_hook(
			runtime.entity_hooks,
			entity,
			entity.reparented,
			_on_entity_reparented.bind(runtime.route),
		)
	_rebuild_runtime(runtime)


# True when the entity has any interpolation config, a display role override,
# or a physics body needing remote freeze. Entities with none never build a
# runtime, though a configured record() call still creates one lazily.
func _entity_wants_runtime(entity: NetwEntity) -> bool:
	var handle := entity.interpolation
	if handle and handle.display_role != DisplayRole.AUTO:
		return true
	var owner := entity.owner
	if not is_instance_valid(owner):
		return false
	if owner is RigidBody2D or owner is RigidBody3D:
		return true
	for node in _entity_nodes(owner):
		var configs := NetwScriptModel.get_node_property_configs(node)
		for property: StringName in configs:
			var opt: NetwScriptModel.SyncConfig = configs[property]
			if not opt.interpolators.is_empty():
				return true
		var script := node.get_script() as Script
		if not script:
			continue
		for method in NetwScriptModel.get_rpc_configs(script):
			if not NetwScriptModel.get_rpc_configs(script)[method] \
					.interpolators.is_empty():
				return true
		for signal_name in NetwScriptModel.get_signal_configs(script):
			if not NetwScriptModel.get_signal_configs(script)[signal_name] \
					.interpolators.is_empty():
				return true
	return false


func _on_entity_dead(route: int) -> void:
	var runtime := _runtimes.get(route) as _Runtime
	if runtime:
		_disconnect_hooks(runtime.entity_hooks)
		_clear_sync_feeds(runtime)
		_apply_body_freeze(runtime, DisplayRole.DISABLED)
	_runtimes.erase(route)
	# The node is freeing, so drop its gate without revealing it.
	_action_gates.erase(route)
	_disconnect_action_reveal_if_idle()
	NetwScriptModel.sweep_dead_overlays()


func _clear_runtimes() -> void:
	for runtime in _runtimes.values():
		_disconnect_hooks(runtime.entity_hooks)
		_clear_sync_feeds(runtime)
		_apply_body_freeze(runtime, DisplayRole.DISABLED)
	_runtimes.clear()
	_action_gates.clear()
	_disconnect_action_reveal_if_idle()


# Hides a remote NetwAction result until the local display playhead reaches the
# action tick, so a spawned effect appears in step with the displayed world
# rather than the moment its frame arrived. The requester keeps its immediate
# predicted presentation, and an entity that never came from an action, or whose
# display already passed the tick, is never touched.
func _apply_action_gate(route: int, entity: NetwEntity) -> void:
	if _action_gates.has(route):
		return
	if entity.action_spawn_tick < 0 or _is_local_action_requester(entity):
		return
	var clock := _get_clock()
	if not clock or clock.display_tick >= entity.action_spawn_tick:
		return
	var gate := _ActionGate.new()
	gate.owner_ref = weakref(entity.owner)
	gate.action_tick = entity.action_spawn_tick
	if not _set_gate_visible(gate, false):
		return
	gate.hidden = true
	_action_gates[route] = gate
	if not clock.on_tick.is_connected(_on_action_reveal_tick):
		clock.on_tick.connect(_on_action_reveal_tick)


func _on_action_reveal_tick(_delta: float, tick: int) -> void:
	var clock := _get_clock()
	for gate_route in _action_gates.keys():
		var gate := _action_gates[gate_route] as _ActionGate
		var reached := true
		if clock:
			reached = maxi(0, tick - clock.display_offset) >= gate.action_tick
		if reached:
			_reveal_gate(gate_route)
	_disconnect_action_reveal_if_idle()


func _reveal_gate(route: int) -> void:
	var gate := _action_gates.get(route) as _ActionGate
	if not gate:
		return
	if gate.hidden:
		_set_gate_visible(gate, gate.original_visible)
		gate.hidden = false
	_action_gates.erase(route)


func _disconnect_action_reveal_if_idle() -> void:
	if not _action_gates.is_empty():
		return
	var clock := _get_clock()
	if clock and clock.on_tick.is_connected(_on_action_reveal_tick):
		clock.on_tick.disconnect(_on_action_reveal_tick)


func _is_local_action_requester(entity: NetwEntity) -> bool:
	if entity.action_requester == 0:
		return false
	var api := NetwMultiplayer.of(self)
	if not api or api.multiplayer_peer == null:
		return false
	return entity.action_requester == api.get_unique_id()


# Sets the gated owner's visibility, capturing its original state on the first
# hide so the reveal restores exactly what the scene declared. Returns
# [code]false[/code] for a non-visual owner (a bare logic node), which the gate
# then leaves alone.
func _set_gate_visible(gate: _ActionGate, value: bool) -> bool:
	var owner := gate.owner_ref.get_ref() as Node
	if owner is CanvasItem:
		var item := owner as CanvasItem
		if not gate.hidden:
			gate.original_visible = item.visible
		item.visible = value
		return true
	if owner is Node3D:
		var spatial := owner as Node3D
		if not gate.hidden:
			gate.original_visible = spatial.visible
		spatial.visible = value
		return true
	return false


func _route_of(entity: NetwEntity) -> int:
	if not is_instance_valid(_liveness):
		_liveness = NetwLivenessInterface.for_node(self)
	if _liveness:
		var route := _liveness.route_of(entity)
		if route > 0:
			return route
	return entity.route


func _runtime_for(route: int, entity: NetwEntity) -> _Runtime:
	if route <= 0 or entity == null:
		return null
	var runtime := _runtimes.get(route) as _Runtime
	if runtime:
		return runtime
	runtime = _Runtime.new()
	runtime.route = route
	runtime.entity_ref = weakref(entity)
	runtime.owner_ref = weakref(entity.owner)
	runtime.handle = entity.interpolation
	runtime.playhead = _Playhead.new()
	_runtimes[route] = runtime
	return runtime


func _runtime_for_handle(handle: Handle) -> _Runtime:
	var entity := handle.entity()
	if not entity:
		return null
	var route := _route_of(entity)
	return _runtimes.get(route) as _Runtime


func _mark_runtime_dirty(handle: Handle) -> void:
	var runtime := _runtime_for_handle(handle)
	if not runtime:
		# The entity can go live before its configuration lands (the shell
		# pushes specs in _ready, after entity registration), so the liveness
		# pass skipped it. A server-authority entity receives no network
		# records to lazily create the runtime either, so adopt it here.
		var entity := handle.entity()
		if entity:
			_on_entity_live(_route_of(entity), entity)
		return
	if runtime.rebuild_queued:
		return
	runtime.rebuild_queued = true
	_rebuild_runtime.call_deferred(runtime)


# Connects [param cb] to [param sig] and stores the triple so one helper can
# disconnect it later even after [param source] is freed.
func _bind_hook(
		store: Array,
		source: Object,
		sig: Signal,
		cb: Callable,
) -> void:
	if not sig.is_connected(cb):
		sig.connect(cb)
		store.append([weakref(source), sig, cb])


func _disconnect_hooks(store: Array) -> void:
	for entry in store:
		var source: Object = (entry[0] as WeakRef).get_ref()
		if not is_instance_valid(source):
			continue
		var sig := entry[1] as Signal
		var cb := entry[2] as Callable
		if sig.is_connected(cb):
			sig.disconnect(cb)
	store.clear()


func _on_entity_control_changed(
		_previous_peer: int,
		_peer: int,
		route: int,
) -> void:
	var runtime := _runtimes.get(route) as _Runtime
	if not runtime:
		return
	runtime.pump_mode = _PUMP_UNRESOLVED
	_resolve_role(runtime)
	_reset_runtime(runtime)


func _on_entity_reparented(
		_reparent: NetwEntity.ReparentOpts,
		route: int,
) -> void:
	var runtime := _runtimes.get(route) as _Runtime
	if not runtime:
		return
	var entity := runtime.entity()
	if not entity:
		return
	runtime.owner_ref = weakref(entity.owner)
	_rebuild_runtime(runtime)


func _rebuild_runtime(runtime: _Runtime) -> void:
	if not runtime:
		return
	runtime.rebuild_queued = false
	var entity := runtime.entity()
	var owner := runtime.owner()
	if not entity or not is_instance_valid(owner):
		return
	_clear_sync_feeds(runtime)
	runtime.states.clear()
	runtime.states_by_key.clear()
	runtime.states_by_target.clear()
	runtime.ambiguous_targets.clear()
	runtime.playhead.display_tick = -1
	_build_property_states(runtime)
	_build_arg_target_states(runtime)
	_compute_sync_intervals(runtime)
	runtime.pump_mode = _PUMP_UNRESOLVED
	_resolve_role(runtime)
	_reset_runtime(runtime)


func _build_property_states(runtime: _Runtime) -> void:
	var owner := runtime.owner()
	if not owner:
		return
	for node in _entity_nodes(owner):
		var configs := NetwScriptModel.get_node_property_configs(node)
		for property: StringName in configs:
			var opt: NetwScriptModel.SyncConfig = configs[property]
			if opt.interpolators.is_empty():
				continue
			var spec: NetwInterpolate = opt.interpolators[0]
			var target := spec.target if not spec.target.is_empty() else property
			_ensure_state(runtime, node, property, target, spec, false)


func _build_arg_target_states(runtime: _Runtime) -> void:
	var owner := runtime.owner()
	if not owner:
		return
	for node in _entity_nodes(owner):
		var script := node.get_script() as Script
		if not script:
			continue
		for method in NetwScriptModel.get_rpc_configs(script):
			var opt: NetwScriptModel.SyncConfig = (
					NetwScriptModel.get_rpc_configs(script)[method]
			)
			_build_arg_states(runtime, node, opt.interpolators)
		for signal_name in NetwScriptModel.get_signal_configs(script):
			var sig_opt: NetwScriptModel.SyncConfig = (
					NetwScriptModel.get_signal_configs(script)[signal_name]
			)
			_build_arg_states(runtime, node, sig_opt.interpolators)


func _build_arg_states(runtime: _Runtime, node: Node, specs: Array) -> void:
	for raw_spec in specs:
		var spec := raw_spec as NetwInterpolate
		if not spec or spec.mode == NetwInterpolate.Mode.NONE:
			continue
		if spec.target.is_empty():
			continue
		_ensure_state(runtime, node, &"", spec.target, spec, false)


func _entity_nodes(owner: Node) -> Array[Node]:
	var out: Array[Node] = [owner]
	for child in owner.find_children("*", "", true, false):
		out.append(child)
	return out


# Composite state identity: (source node instance, property). Property and
# STATE feeders for the same source value share one history.
func _state_key_for(node: Node, property: StringName) -> StringName:
	return StringName("%d/%s" % [node.get_instance_id(), property])


# One constructor and registrar for every state shape. A property state
# samples [param source_prop] on [param node] and aims at the visual root when
# one is configured. An event argument state passes an empty
# [param source_prop], has no source, and writes [param target_prop] in place.
func _ensure_state(
		runtime: _Runtime,
		node: Node,
		source_prop: StringName,
		target_prop: StringName,
		spec: NetwInterpolate,
		authoring_tick: bool,
) -> _PropertyState:
	if not spec or spec.mode == NetwInterpolate.Mode.NONE:
		return null
	var has_source := not source_prop.is_empty()

	var state := _PropertyState.new()
	state.name = target_prop
	state.state_key = _state_key_for(
		node,
		source_prop if has_source else target_prop,
	)
	state.spec = spec
	state.source_obj = node if has_source else null
	state.source_prop = source_prop
	state.target_prop = target_prop
	state.authoring_ticks = authoring_tick

	# An arg state never redirects to a visual root. A property state aims at
	# the visual root when one is configured, otherwise writes in place.
	var owner := runtime.owner()
	var handle := runtime.handle
	var visual: Node = null
	if owner and handle and not handle.visual_root.is_empty():
		visual = owner.get_node_or_null(handle.visual_root)
	var target: Object = visual if has_source and visual else node
	state.target_obj = target

	var history := _History.new()
	history.mode = spec.mode
	history.snap_distance = spec.snap_distance
	state.history = history

	var output := _Output.new()
	output.owner_ref = runtime.owner_ref
	output.target_obj = target
	output.target_prop = target_prop
	output.source_prop = source_prop
	output.global_space = (
			visual != null
			and target == visual
			and target_prop in GLOBAL_SPACE_CHANNELS
	)
	state.output = output

	# A parented visual only escapes body drag on channels written in global
	# space. Any other spatial channel is written locally and composes with
	# the body, so a body snap drags it. Say so instead of degrading silently.
	if visual != null and target == visual \
			and not output.global_space \
			and not visual.get(&"top_level") \
			and target_prop in [
				&"rotation", &"scale", &"transform",
				&"quaternion", &"basis", &"skew",
			]:
		_dbg.warn(
			"interpolation: channel '%s' on parented visual '%s' is written "
			+ "in local space and will be dragged by body writes. Set "
			+ "top_level on the visual, or interpolate position/rotation "
			+ "channels instead.",
			[target_prop, visual.name],
		)

	state.last_written = _current_source_value(state)

	var existing := runtime.states_by_key.get(state.state_key) as _PropertyState
	if existing:
		existing.copy_shape_from(state)
		return existing
	runtime.states_by_key[state.state_key] = state
	runtime.states.append(state)
	_index_target(runtime, state)
	return state


# Maintains the name-keyed secondary index used by Handle lookups. Two states
# that write the same target object and property are a real double-write. Two
# states that share only the property name make name lookups ambiguous.
func _index_target(runtime: _Runtime, state: _PropertyState) -> void:
	var existing := runtime.states_by_target.get(state.name) as _PropertyState
	if not existing:
		runtime.states_by_target[state.name] = state
		return
	runtime.ambiguous_targets[state.name] = true
	if existing.target_obj == state.target_obj \
			and existing.target_prop == state.target_prop:
		_dbg.warn(
			"interpolation: two sources write '%s' on the same target",
			[state.name],
		)

#endregion

#region Roles

# Recomputes the pump on every call and applies the transition when it
# changes. Always recomputing keeps the pump current without dirty tracking.
# The inputs are cheap reads, and the authored-streams scan is bounded by the
# entity's few cached synchronizers.
func _resolve_role(runtime: _Runtime) -> void:
	var role := _resolve_display_role(runtime)
	var pump := _pump_for(runtime, role)
	if pump == runtime.pump_mode:
		return

	# Crossing the predicted boundary switches feeders between local sampling
	# and network receive, which record in different tick domains. Stale
	# records from the previous feeder cannot mix with the new one.
	if _pump_is_predicted(pump) or _pump_is_predicted(runtime.pump_mode):
		for state in runtime.states:
			state.history.clear()

	runtime.pump_mode = pump
	_apply_body_freeze(runtime, role)
	if pump == _PUMP_REMOTE:
		_compute_sync_intervals(runtime)


func _pump_is_predicted(pump: int) -> bool:
	return pump == _PUMP_CHASE or pump == _PUMP_BRACKETED


func _pump_for(runtime: _Runtime, role: DisplayRole) -> int:
	match role:
		DisplayRole.PREDICTED:
			var handle := runtime.handle
			if handle and handle.predicted_mode == PredictedMode.BRACKETED:
				return _PUMP_BRACKETED
			return _PUMP_CHASE
		DisplayRole.AUTHORITY:
			return _PUMP_BRACKETED
		DisplayRole.REMOTE:
			return _PUMP_REMOTE
		_:
			return _PUMP_DISABLED


func _resolve_display_role(runtime: _Runtime) -> DisplayRole:
	var handle := runtime.handle
	if handle and handle.display_role != DisplayRole.AUTO:
		return handle.display_role
	var owner := runtime.owner()
	var entity := runtime.entity()
	if _has_prediction_component(runtime) and entity.is_controlled_locally:
		return DisplayRole.PREDICTED
	if owner and owner.is_multiplayer_authority() \
			and entity.is_controlled_locally:
		return DisplayRole.DISABLED
	if _authors_display_streams(runtime):
		return DisplayRole.AUTHORITY
	if owner and owner.is_multiplayer_authority():
		return DisplayRole.DISABLED
	return DisplayRole.REMOTE


# True when every sync channel feeding this entity's tracked values is authored
# by this peer, so no receive stream exists to consume and the display must
# sample the local simulation instead. At least one channel must exist. Derived
# sets ride the same rule: a public set whose fields feed tracked values counts
# as a stream, and authorship follows the set's policy, the same predicate the
# send-side pump gates on.
func _authors_display_streams(runtime: _Runtime) -> bool:
	var entity := runtime.entity()
	if not entity:
		return false
	var found := false
	for sync in entity.synchronizers():
		if not sync.is_inside_tree():
			continue
		if not _sync_feeds_consumed(sync):
			continue
		if not _sync_replicates_tracked_property(runtime, sync):
			continue
		if not sync.is_multiplayer_authority():
			return false
		found = true
	var api := NetwMultiplayer.of(self)
	if api:
		for binding in api.replication.derived_group(runtime.route):
			var node := binding.node()
			if not is_instance_valid(node) or not node.is_inside_tree():
				continue
			if binding.set.audience != NetwSyncSet.Audience.AUDIENCE_PUBLIC:
				continue
			if not _set_replicates_tracked_property(runtime, binding.set):
				continue
			if not _authors_derived_stream(binding, entity):
				return false
			found = true
	return found


# The send-side author predicate for a derived stream, mirrored from the pump's
# gate: the server for a state set, the node authority for an authority-policed
# set, the local controller for a controller-policed set, any peer for an open
# set.
func _authors_derived_stream(
		binding: NetwSyncSetBinding,
		entity: NetwEntity,
) -> bool:
	var node := binding.node()
	if not is_instance_valid(node):
		return false
	if binding.set.record == NetwSyncSet.Record.RECORD_STATE:
		var api := NetwMultiplayer.of(self)
		return api != null and api.get_unique_id() == 1
	match binding.set.policy:
		NetwScriptModel.Policy.AUTHORITY:
			return node.is_multiplayer_authority()
		NetwScriptModel.Policy.CONTROLLER:
			return entity.is_controlled_locally
		NetwScriptModel.Policy.ANY_PEER:
			return true
	return false


# The derived counterpart of _sync_replicates_tracked_property: a set feeds the
# runtime when any field key names a tracked value's source or name.
func _set_replicates_tracked_property(
		runtime: _Runtime,
		set: NetwSyncSet,
) -> bool:
	for field in set.fields:
		for state in runtime.states:
			if state.source_prop == field.key or state.name == field.key:
				return true
	return false


func _has_prediction_component(runtime: _Runtime) -> bool:
	var entity := runtime.entity()
	var owner := runtime.owner()
	if entity and entity.prediction.is_registered():
		return true
	if owner:
		return owner.get_node_or_null("%PredictionComponent") != null
	return false


func _apply_body_freeze(runtime: _Runtime, role: DisplayRole) -> void:
	var owner := runtime.owner()
	if not (owner is RigidBody2D or owner is RigidBody3D):
		return
	if role == DisplayRole.REMOTE:
		if runtime.saved_freeze.is_empty():
			runtime.saved_freeze = {
				&"freeze": owner.get(&"freeze"),
				&"mode": owner.get(&"freeze_mode"),
			}
		owner.set(&"freeze_mode", RigidBody2D.FREEZE_MODE_KINEMATIC)
		owner.set(&"freeze", true)
	elif not runtime.saved_freeze.is_empty():
		owner.set(&"freeze", runtime.saved_freeze[&"freeze"])
		owner.set(&"freeze_mode", runtime.saved_freeze[&"mode"])
		runtime.saved_freeze = { }

#endregion

#region Pump

func _process(delta: float) -> void:
	var frame := Engine.get_process_frames()
	if delta > 0.0 and frame == _last_update_frame:
		return
	_last_update_frame = frame

	var clock := _get_clock()
	if not clock:
		_ensure_clock_connection()
		return

	var global_dt := clock.display_tick
	var global_factor := clock.tick_factor
	var frame_ticks := delta * clock.tickrate

	for runtime in _runtimes.values():
		if runtime.disabled:
			continue
		_update_runtime(runtime, global_dt, global_factor, frame_ticks, delta)


func _update_runtime(
		runtime: _Runtime,
		global_dt: int,
		global_factor: float,
		frame_ticks: float,
		delta: float,
) -> void:
	if not runtime.owner():
		return
	_resolve_role(runtime)
	match runtime.pump_mode:
		_PUMP_DISABLED:
			return
		_PUMP_CHASE:
			_pump_chase(runtime, delta)
		_:
			_pump_history(runtime, global_dt, global_factor, frame_ticks, delta)


# Drives the remote and bracketed roles. Both sample the same history through
# the playhead. Remote dilates the playhead, bracketed follows the raw tick.
func _pump_history(
		runtime: _Runtime,
		global_dt: int,
		global_factor: float,
		frame_ticks: float,
		delta: float,
) -> void:
	var playhead := runtime.playhead
	var trace := _should_trace(runtime)
	var dt: int
	var factor: float
	if runtime.pump_mode == _PUMP_REMOTE:
		var handle := runtime.handle
		if handle and handle.enable_smart_dilation:
			_dilate_playhead(runtime, global_dt, frame_ticks, trace)
		else:
			playhead.display_lag = 0.0
		var time := (float(global_dt) + global_factor) - playhead.display_lag
		dt = int(floor(time))
		factor = time - float(dt)
	else:
		dt = maxi(0, global_dt - 1)
		factor = global_factor
		playhead.display_lag = 0.0
	playhead.display_tick = dt

	var eit := playhead.expected_interval_ticks
	for state in runtime.states:
		# An empty history has no stream to show. Defer to whatever wrote the
		# property instead of clobbering it with a stale value.
		if state.history.is_empty() or state.history.is_sleeping:
			continue
		var result: Variant = state.history.sample(
			dt,
			factor,
			state.last_written,
			eit,
		)
		var weight := 1.0
		if state.spec and state.spec.smoothing > 0.0:
			weight = 1.0 - exp(-delta / state.spec.smoothing)
		result = state.history.smooth_toward(state.last_written, result, weight)
		if trace:
			_dbg.trace(
				"Interp %s dt=%d lag=%.2f val=%s",
				[state.name, dt, playhead.display_lag, result],
			)
		state.output.write(result)
		state.last_written = result


# Eases each visual toward its live predicted body every frame.
func _pump_chase(runtime: _Runtime, delta: float) -> void:
	var weight := 1.0 - exp(-delta / _predicted_effective_smooth_time(runtime))
	var trace := _should_trace(runtime)
	for state in runtime.states:
		if not is_instance_valid(state.source_obj):
			continue
		var value: Variant = state.source_obj.get(state.source_prop)
		var result: Variant = state.history.smooth_toward(
			state.last_written,
			value,
			weight,
		)
		if trace:
			_dbg.trace(
				"PredictDisplay %s target=%s val=%s",
				[state.name, value, result],
			)
		state.output.write(result)
		state.last_written = result


func _dilate_playhead(
		runtime: _Runtime,
		global_dt: int,
		frame_ticks: float,
		trace: bool,
) -> void:
	var playhead := runtime.playhead
	var handle := runtime.handle
	var raw_floor := _calculate_min_lag(runtime)
	playhead.smoothed_floor += (raw_floor - playhead.smoothed_floor) \
			* handle.floor_smoothing

	var effective_dt := int(floor(float(global_dt) - playhead.display_lag))
	var is_starving := false
	var newest_tick := -1

	for state in runtime.states:
		if state.history.is_empty():
			continue
		if not state.history.has_tick_after(effective_dt):
			is_starving = true
			newest_tick = state.history.newest_tick()
			break

	if is_starving:
		playhead.starvation_ticks += 1
		for state in runtime.states:
			state.history.is_sleeping = false
	else:
		playhead.starvation_ticks = 0

	if playhead.starvation_ticks >= handle.starvation_grace_frames:
		playhead.display_lag = minf(
			playhead.display_lag + frame_ticks * handle.starvation_growth,
			playhead.smoothed_floor + handle.max_extra_dilation,
		)
	else:
		playhead.display_lag += (playhead.smoothed_floor - playhead.display_lag) \
				* handle.lag_adapt_rate

	if trace:
		_dbg.trace(
			"Interpolation dilation dt=%d newest=%d starving=%s lag=%.2f",
			[effective_dt, newest_tick, str(is_starving), playhead.display_lag],
		)


func _calculate_min_lag(runtime: _Runtime) -> float:
	var clock := _get_clock()
	if not clock:
		return 0.0
	var needed := float(runtime.playhead.expected_interval_ticks + 1)
	var network_padding := float(
		maxi(
			0,
			clock.recommended_display_offset - clock.display_offset,
		),
	)
	return maxf(
		0.0,
		needed - float(clock.display_offset) + network_padding,
	)


func _reset_runtime(runtime: _Runtime) -> void:
	var playhead := runtime.playhead
	var target_lag := _calculate_min_lag(runtime)
	playhead.smoothed_floor = target_lag
	playhead.display_lag = target_lag
	playhead.starvation_ticks = 0
	for state in runtime.states:
		state.history.clear()
		state.last_written = _current_source_value(state)
		state.output.write(state.last_written)


func _current_source_value(state: _PropertyState) -> Variant:
	if is_instance_valid(state.source_obj):
		return state.source_obj.get(state.source_prop)
	if is_instance_valid(state.target_obj):
		return state.target_obj.get(state.target_prop)
	return null


func _snap_state(
		runtime: _Runtime,
		property: StringName,
		value: Variant,
) -> void:
	var state := _named_state(runtime, property)
	if not state:
		return
	state.output.write(value)
	state.history.clear()
	state.last_written = value


func _displayed_authoring_tick(runtime: _Runtime) -> int:
	if not runtime.authoring_binding or runtime.playhead.display_tick < 0:
		return -1
	for state in runtime.states:
		if not state.authoring_ticks:
			continue
		var prev := state.history.bracketing_ticks(
			runtime.playhead.display_tick,
		).x
		if prev >= 0:
			return prev
	return -1


func _get_buffer(runtime: _Runtime, property: StringName) -> NetwRingBuffer:
	if runtime.pump_mode == _PUMP_CHASE:
		return null
	var state := _named_state(runtime, property)
	return state.history.buffer if state else null


# Returns the state writing [param property], warning if the name is ambiguous.
func _named_state(runtime: _Runtime, property: StringName) -> _PropertyState:
	if runtime.ambiguous_targets.has(property):
		_dbg.warn(
			"interpolation: ambiguous name lookup for '%s'",
			[property],
		)
	return runtime.states_by_target.get(property) as _PropertyState


func _should_trace(runtime: _Runtime) -> bool:
	var handle := runtime.handle
	if not handle or handle.trace_interval <= 0:
		return false
	runtime.trace_frame = (runtime.trace_frame + 1) % handle.trace_interval
	return runtime.trace_frame == 0


# The chase smoothing time, from the handle override or a clock-derived
# default of most of one tick.
func _predicted_effective_smooth_time(runtime: _Runtime) -> float:
	var handle := runtime.handle
	if handle and handle.predicted_smooth_time > 0.0:
		return handle.predicted_smooth_time
	var clock := _get_clock()
	if clock:
		return maxf(clock.ticktime * 0.85, 0.001)
	return 1.0 / 60.0

#endregion

#region Sync feeds (absorbed by NetwMultiplayer when the replication core lands)

func _compute_sync_intervals(runtime: _Runtime) -> void:
	var max_interval := 0.0
	var entity := runtime.entity()
	if not entity:
		return
	runtime.authoring_binding = null
	for sync in entity.synchronizers():
		if not sync.public_visibility:
			continue
		if not _sync_replicates_tracked_property(runtime, sync):
			continue
		max_interval = maxf(
			max_interval,
			maxf(sync.replication_interval, sync.delta_interval),
		)
	# The derived state set is the authoring source: its frames carry the
	# tick-ack stamp, the single authoring stream the displayed tick reads.
	var state_binding := entity.state_binding
	if state_binding and _set_replicates_tracked_property(runtime, state_binding.set):
		runtime.authoring_binding = state_binding
	if max_interval <= 0.0:
		return
	var clock := _get_clock()
	if clock:
		runtime.playhead.expected_interval_ticks = maxi(
			1,
			ceili(max_interval * clock.tickrate),
		)


func _sync_replicates_tracked_property(
		runtime: _Runtime,
		sync: MultiplayerSynchronizer,
) -> bool:
	if not sync.replication_config:
		return false
	for path in sync.replication_config.get_properties():
		if path.get_subname_count() == 0:
			continue
		var clean_name := path.get_subname(path.get_subname_count() - 1)
		for state in runtime.states:
			if state.source_prop == clean_name or state.name == clean_name:
				return true
	return false


## Records a consumed plain synchronizer's just-applied values into history.
## Called by [NetwSyncCompat] after it writes an incoming
## [constant NetwFrameEnvelope.Channel.SYNC] or
## [constant NetwFrameEnvelope.Channel.SYNC_DELTA] row onto the live nodes,
## the receive-side feed the [signal MultiplayerSynchronizer.synchronized]
## emission used to provide. Derived sets feed through
## [method feed_derived_apply] instead.
func feed_consumed_apply(route: int, sync: MultiplayerSynchronizer) -> void:
	if not _sync_feeds_consumed(sync):
		return
	_on_native_sync(route, sync)


## Records a derived set's just-applied values into history. Called by
## [NetwSyncPipeline] after [method NetwSyncSetBinding.apply_volatile] returns
## the decoded header, the derived counterpart of [method feed_consumed_apply].
## A stamped set records each field at the header's authoring tick, the same
## tick domain the stamped payload funnel fed, and a header with no payload row
## (a retained delta decodes no tick) reads the just-written values back off the
## node at the receive tick. Only a [constant NetwSyncSet.Audience.AUDIENCE_PUBLIC]
## set feeds display history, so a server-only input stream never writes into a
## display buffer, and the public third-peer input row rides this same feed.
func feed_derived_apply(
		route: int,
		binding: NetwSyncSetBinding,
		header: Dictionary,
) -> void:
	if binding.set.audience != NetwSyncSet.Audience.AUDIENCE_PUBLIC:
		return
	var runtime := _runtimes.get(route) as _Runtime
	if not runtime:
		return
	var map := _derived_map_for(runtime, binding)
	if map.is_empty():
		return
	var tick := int(header.get("tick", -1))
	var authoring := binding.set.stamp != NetwSyncSet.Stamp.STAMP_NONE and tick >= 0
	if not authoring:
		var clock := _get_clock()
		tick = clock.tick if clock else 0
	var payload: Dictionary = header.get("payload", { })
	if payload.is_empty():
		# Only the retained lane applies without a payload row, and it never
		# shares a field with the volatile lane, so the read-back stays in the
		# receive-tick domain without mixing a stamped field's history.
		for field in binding.set.fields:
			if field.lane != NetwSyncSet.Lane.RETAINED or not map.has(field.key):
				continue
			var b: Array = map[field.key]
			var node := b[0] as Node
			if is_instance_valid(node):
				_record(node, field.key, node.get(field.key), tick, b[2] as NetwInterpolate, false)
		return
	for key: StringName in payload:
		if not map.has(key):
			continue
		var b: Array = map[key]
		_record(b[0], b[1], payload[key], tick, b[2] as NetwInterpolate, authoring)


# Resolves and caches the derived binding's [key -> [node, property, spec]] map,
# dropping any field without an interpolator. Shares the sync source cache (keyed
# by instance id, which never collides across object kinds) so a runtime rebuild
# clears both. A derived field always reads and writes the declaring node under
# its own key, so node and property are the binding's node and the field key.
func _derived_map_for(runtime: _Runtime, binding: NetwSyncSetBinding) -> Dictionary:
	var bid := binding.get_instance_id()
	if runtime.sync_source_cache.has(bid):
		return runtime.sync_source_cache[bid]
	var map: Dictionary = { }
	var node := binding.node()
	if is_instance_valid(node):
		for field in binding.set.fields:
			var spec := NetwScriptModel.get_node_property_interpolator(node, field.key)
			if spec:
				map[field.key] = [node, field.key, spec]
	runtime.sync_source_cache[bid] = map
	return map


# True for a plain display synchronizer whose receive path is the consumed
# apply feed.
func _sync_feeds_consumed(sync: MultiplayerSynchronizer) -> bool:
	if not sync.replication_config:
		return false
	if not sync.public_visibility:
		return false
	return true


# Clears the cached receive-side binding maps so a rebuilt runtime re-resolves
# its sources against the live tree.
func _clear_sync_feeds(runtime: _Runtime) -> void:
	runtime.sync_source_cache.clear()


# Native receive adapter. A plain synchronizer carries no payload, so the value
# native replication just wrote onto each node is read back at the receive tick.
func _on_native_sync(route: int, sync: MultiplayerSynchronizer) -> void:
	var runtime := _runtimes.get(route) as _Runtime
	if not runtime:
		return
	var bindings := _binding_map_for(runtime, sync)
	if bindings.is_empty():
		return
	var clock := _get_clock()
	var tick := clock.tick if clock else 0
	for key: StringName in bindings:
		var b: Array = bindings[key]
		var node := b[0] as Node
		if not is_instance_valid(node):
			continue
		var prop := b[1] as StringName
		_record(node, prop, node.get(prop), tick, b[2] as NetwInterpolate, false)


# Resolves and caches the sync's configured [key -> [node, property, spec]]
# bindings once, dropping any key without an interpolator. Both receive adapters
# read this same map. Cleared on runtime rebuild.
func _binding_map_for(runtime: _Runtime, sync: MultiplayerSynchronizer) -> Dictionary:
	var sid := sync.get_instance_id()
	if runtime.sync_source_cache.has(sid):
		return runtime.sync_source_cache[sid]
	var map: Dictionary = { }
	var root := sync.get_node_or_null(sync.root_path)
	if root:
		for binding: Array in SynchronizersCache.display_bindings(sync, root):
			var node := binding[1] as Node
			var prop := binding[2] as StringName
			var spec := NetwScriptModel.get_node_property_interpolator(node, prop)
			if spec:
				map[binding[0]] = [node, prop, spec]
	runtime.sync_source_cache[sid] = map
	return map

#endregion

## Per entity interpolation handle.
##
## [NetwEntity.interpolation] owns this handle. It holds display settings that
## apply to every [NetwInterpolate] value on the entity.
## [codeblock]
## var handle := NetwEntity.of(self).interpolation
## handle.visual_root = NodePath("Visual")
## handle.display_role = NetwInterpolationInterface.DisplayRole.REMOTE
## [/codeblock]
class Handle:
	extends RefCounted

	## Visual child that receives smoothed output. A parented visual takes
	## global-space writes for the [constant GLOBAL_SPACE_CHANNELS] while
	## every other channel inherits from the body. A visual with
	## [member CanvasItem.top_level] set takes absolute writes on any channel.
	var visual_root: NodePath = NodePath(""):
		set(value):
			visual_root = value
			_mark_dirty()

	## Display strategy override for this entity.
	var display_role: DisplayRole = DisplayRole.AUTO:
		set(value):
			display_role = value
			_mark_dirty()

	## Predicted display filter used for local prediction.
	var predicted_mode: PredictedMode = PredictedMode.CHASE:
		set(value):
			predicted_mode = value
			_mark_dirty()

	## Exponential smoothing time for [constant PredictedMode.CHASE].
	var predicted_smooth_time: float = 0.0:
		set(value):
			predicted_smooth_time = value
			_mark_dirty()

	## Enables display lag adaptation for remote interpolation.
	var enable_smart_dilation: bool = true

	## Maximum extra ticks that display lag can grow while starving.
	var max_extra_dilation: float = 0.0

	## Per frame fraction used to track the measured lag floor.
	var lag_adapt_rate: float = 0.05

	## Ticks per frame added after starvation is sustained.
	var starvation_growth: float = 0.95

	## Per frame fraction used to low pass the lag floor.
	var floor_smoothing: float = 0.05

	## Starving frames tolerated before lag starts growing.
	var starvation_grace_frames: int = 3

	## Frames between interpolation trace logs. [code]0[/code] disables logs.
	var trace_interval: int = 0

	## Read-only extra display delay in ticks measured by smart dilation.
	var display_lag: float:
		get:
			var runtime := _runtime()
			return runtime.playhead.display_lag if runtime else 0.0

	## Read-only count of consecutive frames starved for fresh snapshots.
	var starvation_ticks: int:
		get:
			var runtime := _runtime()
			return runtime.playhead.starvation_ticks if runtime else 0

	var _entity_ref: WeakRef


	## Snaps [param property] to [param value] and clears its history.
	func snap_property(property: StringName, value: Variant) -> void:
		var iface := _interface()
		var runtime := _runtime()
		if iface and runtime:
			iface._snap_state(runtime, property, value)


	## Clears history and applies the live source values to the display target.
	func reset() -> void:
		var iface := _interface()
		var runtime := _runtime()
		if iface and runtime:
			iface._reset_runtime(runtime)


	## Returns the displayed STATE authoring tick, or [code]-1[/code].
	func displayed_authoring_tick() -> int:
		var iface := _interface()
		var runtime := _runtime()
		if not iface or not runtime:
			return -1
		return iface._displayed_authoring_tick(runtime)


	## Returns the [NetwRingBuffer] for [param property], or [code]null[/code].
	func get_buffer(property: StringName) -> NetwRingBuffer:
		var iface := _interface()
		var runtime := _runtime()
		if not iface or not runtime:
			return null
		return iface._get_buffer(runtime, property)


	## Temporarily disables interpolation for [param duration] seconds.
	func disable_for(duration: float) -> SceneTreeTimer:
		var runtime := _runtime()
		var ent := entity()
		if not runtime or not ent or not ent.owner:
			return null
		runtime.disabled = true
		reset()
		var timer := ent.owner.get_tree().create_timer(duration)
		var self_ref := weakref(self)
		timer.timeout.connect(
			func() -> void:
				var handle := self_ref.get_ref() as Handle
				if not handle:
					return
				var rt := handle._runtime()
				if rt:
					rt.disabled = false
		)
		return timer


	func _bind(bound_entity: NetwEntity) -> void:
		_entity_ref = weakref(bound_entity)


	func entity() -> NetwEntity:
		return _entity_ref.get_ref() as NetwEntity if _entity_ref else null


	func _interface() -> NetwInterpolationInterface:
		var ent := entity()
		if not ent or not ent.owner:
			return null
		return NetwInterpolationInterface.for_node(ent.owner)


	func _runtime() -> _Runtime:
		var iface := _interface()
		return iface._runtime_for_handle(self) if iface else null


	func _mark_dirty() -> void:
		var iface := _interface()
		if iface:
			iface._mark_runtime_dirty(self)


#region Runtime data

# One suspended action-spawn owner, hidden until its display tick arrives. Held
# by route in _action_gates. Weakref-backed so a freed owner clears itself.
class _ActionGate:
	extends RefCounted

	var owner_ref: WeakRef
	var hidden := false
	var original_visible := true
	var action_tick := -1


class _Runtime:
	extends RefCounted

	var route: int
	var entity_ref: WeakRef
	var owner_ref: WeakRef
	var handle: Handle
	var playhead: _Playhead
	var states: Array[_PropertyState] = []
	var states_by_key: Dictionary[StringName, _PropertyState] = { }
	var states_by_target: Dictionary[StringName, _PropertyState] = { }
	var ambiguous_targets: Dictionary[StringName, bool] = { }
	var sync_source_cache: Dictionary = { }
	var pump_mode: int = _PUMP_UNRESOLVED
	var rebuild_queued := false
	var authoring_binding: NetwSyncSetBinding
	var trace_frame := 0
	var saved_freeze: Dictionary = { }
	var entity_hooks: Array = []
	var disabled := false


	func entity() -> NetwEntity:
		return entity_ref.get_ref() as NetwEntity if entity_ref else null


	func owner() -> Node:
		return owner_ref.get_ref() as Node if owner_ref else null


class _PropertyState:
	var name: StringName
	var state_key: StringName
	var spec: NetwInterpolate
	var history: _History
	var output: _Output
	var source_obj: Object
	var source_prop: StringName
	var target_obj: Object
	var target_prop: StringName
	var authoring_ticks := false
	var last_written: Variant


	func copy_shape_from(other: _PropertyState) -> void:
		spec = other.spec
		source_obj = other.source_obj
		source_prop = other.source_prop
		target_obj = other.target_obj
		target_prop = other.target_prop
		authoring_ticks = authoring_ticks or other.authoring_ticks

#endregion

#region Display engine

# Per runtime display cursor. Carries signed display lag so a future forecast
# policy can lead the newest tick the same way dilation trails it.
class _Playhead:
	extends RefCounted

	var display_lag := 0.0
	var starvation_ticks := 0
	var smoothed_floor := 0.0
	var expected_interval_ticks := 3
	var display_tick := -1


# Per value ring buffer with a total resample. Sampling past the newest tick
# holds the last value (the HOLD tail policy). The tick domain is fixed by the
# first record and asserts on mixing authoring and receive ticks.
class _History:
	extends RefCounted

	var buffer := NetwRingBuffer.new(16)
	var mode: NetwInterpolate.Mode = NetwInterpolate.Mode.LERP
	var snap_distance := 0.0
	var is_sleeping := false
	var _tick_domain := -1
	var _last_recorded: Variant
	var _has_recorded := false
	var _cached_prev := -1
	var _search := PackedInt32Array([-1, -1])


	func record(tick: int, value: Variant, authoring_tick: bool) -> void:
		var domain := 1 if authoring_tick else 0
		if _tick_domain == -1:
			_tick_domain = domain
		else:
			assert(
				_tick_domain == domain,
				"interpolation: mixing authoring and receive ticks",
			)
		if _has_recorded and value == _last_recorded:
			return
		buffer.record(tick, value)
		_last_recorded = value
		_has_recorded = true
		is_sleeping = false


	func clear() -> void:
		buffer.clear()
		_tick_domain = -1
		_has_recorded = false
		_cached_prev = -1
		is_sleeping = false


	func is_empty() -> bool:
		return buffer.is_empty()


	func newest_tick() -> int:
		return buffer.newest_tick()


	func has_tick_after(tick: int) -> bool:
		return buffer.has_tick_after(tick)


	func bracketing_ticks(tick: int) -> Vector2i:
		return buffer.bracketing_ticks(tick)


	# Total resample at [param dt]+[param factor]. Before the first tick holds
	# [param last_written], past the newest tick holds the newest value and may
	# mark the history asleep.
	func sample(
			dt: int,
			factor: float,
			last_written: Variant,
			expected_interval_ticks: int,
	) -> Variant:
		buffer.find_bracketing_ticks(dt, _cached_prev, _search)
		var prev_tick := _search[0]
		var next_tick := _search[1]
		_cached_prev = prev_tick
		if prev_tick == -1:
			return last_written
		if next_tick == -1:
			var result: Variant = buffer.get_at(prev_tick)
			if not buffer.has_tick_after(prev_tick) \
					and _is_close(last_written, result):
				is_sleeping = true
			return result
		var p_val: Variant = buffer.get_at(prev_tick)
		var n_val: Variant = buffer.get_at(next_tick)
		if snap_distance > 0.0 and _snap(p_val, n_val, snap_distance):
			return n_val
		return _lerp_bracketed(
			p_val,
			n_val,
			prev_tick,
			next_tick,
			dt,
			factor,
			expected_interval_ticks,
		)


	# Exponential blend toward [param result] with a snap-distance escape.
	func smooth_toward(
			last_written: Variant,
			result: Variant,
			weight: float,
	) -> Variant:
		if weight >= 1.0:
			return result
		if snap_distance > 0.0 and _snap(last_written, result, snap_distance):
			return result
		return _interpolate(last_written, result, weight)


	func _lerp_bracketed(
			p_val: Variant,
			n_val: Variant,
			p_tick: int,
			n_tick: int,
			dt: int,
			factor: float,
			expected_interval_ticks: int,
	) -> Variant:
		var gap := n_tick - p_tick
		var threshold := expected_interval_ticks * 2
		if gap > threshold:
			var start_lerp_tick := n_tick - expected_interval_ticks
			if dt < start_lerp_tick:
				return p_val
			var t := clampf(
				(float(dt - start_lerp_tick) + factor)
				/ float(expected_interval_ticks),
				0.0,
				1.0,
			)
			return _interpolate(p_val, n_val, t)
		var t := clampf((float(dt - p_tick) + factor) / float(gap), 0.0, 1.0)
		return _interpolate(p_val, n_val, t)


	func _interpolate(a: Variant, b: Variant, t: float) -> Variant:
		if mode == NetwInterpolate.Mode.ANGLE:
			return lerp_angle(a, b, t)
		if mode == NetwInterpolate.Mode.SLERP:
			return (a as Quaternion).slerp(b, t)
		return lerp(a, b, t)


	func _snap(v1: Variant, v2: Variant, dist: float) -> bool:
		if typeof(v1) != typeof(v2):
			return true
		match typeof(v1):
			TYPE_VECTOR2, TYPE_VECTOR2I, TYPE_VECTOR3, TYPE_VECTOR3I:
				return v1.distance_to(v2) > dist
			TYPE_QUATERNION:
				return absf(v1.angle_to(v2)) > dist
			TYPE_FLOAT, TYPE_INT:
				var diff := abs(angle_difference(v1, v2)) \
				if mode == NetwInterpolate.Mode.ANGLE \
				else abs(v1 - v2)
				return diff > dist
		return false


	func _is_close(v1: Variant, v2: Variant) -> bool:
		if typeof(v1) != typeof(v2):
			return false
		match typeof(v1):
			TYPE_VECTOR2, TYPE_VECTOR2I, TYPE_VECTOR3, TYPE_VECTOR3I:
				return v1.is_equal_approx(v2)
			TYPE_QUATERNION:
				return absf(v1.angle_to(v2)) < 0.001
			TYPE_FLOAT, TYPE_INT:
				var diff := abs(angle_difference(v1, v2)) \
				if mode == NetwInterpolate.Mode.ANGLE \
				else abs(v1 - v2)
				return diff < 0.001
		return true


# Writes one smoothed value. A spatial value aimed at a parented visual is
# written in global space so body writes never drag the smoothed channel while
# unsmoothed channels still inherit from the body. Everything else is a plain
# property write.
class _Output:
	extends RefCounted

	var owner_ref: WeakRef
	var target_obj: Object
	var target_prop: StringName
	var source_prop: StringName
	var global_space := false


	func owner() -> Node:
		return owner_ref.get_ref() as Node if owner_ref else null


	func write(value: Variant) -> void:
		if not is_instance_valid(target_obj):
			return
		if global_space and _write_global(value):
			return
		target_obj.set(target_prop, value)


	func _write_global(value: Variant) -> bool:
		var host := owner()
		if target_prop == &"position":
			if target_obj is Node2D and typeof(value) == TYPE_VECTOR2:
				(target_obj as Node2D).global_position = _to_global_2d(host, value)
				return true
			if target_obj is Node3D and typeof(value) == TYPE_VECTOR3:
				(target_obj as Node3D).global_position = _to_global_3d(host, value)
				return true
		elif target_prop == &"rotation":
			if target_obj is Node2D and typeof(value) == TYPE_FLOAT:
				(target_obj as Node2D).global_rotation = _to_global_rot_2d(
					host,
					value,
				)
				return true
		return false


	func _to_global_2d(host: Node, value: Vector2) -> Vector2:
		if source_prop == &"global_position":
			return value
		var parent := host.get_parent() if host else null
		if parent is Node2D:
			return (parent as Node2D).to_global(value)
		return value


	func _to_global_3d(host: Node, value: Vector3) -> Vector3:
		if source_prop == &"global_position":
			return value
		var parent := host.get_parent() if host else null
		if parent is Node3D:
			return (parent as Node3D).to_global(value)
		return value


	func _to_global_rot_2d(host: Node, value: float) -> float:
		if source_prop == &"global_rotation":
			return value
		var parent := host.get_parent() if host else null
		if parent is Node2D:
			return (parent as Node2D).global_rotation + value
		return value

#endregion
