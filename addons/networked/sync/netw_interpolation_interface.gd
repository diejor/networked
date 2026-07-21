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
extends RefCounted

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

## Selects where on the timeline a remote entity renders.
##
## [constant FORECAST] projects along the last replicated velocity, which has no
## knowledge of geometry, so a remote body projected toward a wall penetrates it
## until the bounce sample arrives. A dynamic-body game with world collision should
## keep remotes on [constant BUFFERED] (freeze on the newest truth rather than
## extrapolate past it), the stance the racing example takes.
enum TimelineMode {
	## Renders behind the newest sample by the jitter buffer, always a delayed
	## truth. This is the default and the only mode the server ever resolves.
	BUFFERED = 0,
	## Targets the newest sample and projects each channel across the gaps
	## between snapshots, trading the buffer delay for extrapolation error. Collision
	## blind, so prefer [constant BUFFERED] for a body that meets world geometry.
	FORECAST = 1,
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
var _clock: NetwClockInterface
var _liveness: NetwLivenessInterface
var _clock_connected := false
var _clock_bind_attempts := 0
var _liveness_connected := false
var _last_update_frame := -1
var _stats := NetwPumpStats.new()
var _dbg: NetwHandle = Netw.dbg.handle(self)

# The owning NetwMultiplayer. A weakref because the owner holds this interface
# as a member, so a strong reference back would form a cycle neither side frees.
var _api_ref: WeakRef


func _init(api: NetwMultiplayer = null) -> void:
	_api_ref = weakref(api) if api else null
	if api == null:
		return
	api.session_entered.connect(_on_session_entered)
	api.session_ended.connect(_on_session_ended)
	_ensure_liveness_connection()
	_ensure_clock_connection()


func _api() -> NetwMultiplayer:
	return _api_ref.get_ref() as NetwMultiplayer if _api_ref else null


## Releases every session binding so the owning session can free. Mirrors the
## teardown the other session interfaces run from the session's own disposal.
func dispose() -> void:
	var api := _api()
	if api:
		if api.session_entered.is_connected(_on_session_entered):
			api.session_entered.disconnect(_on_session_entered)
		if api.session_ended.is_connected(_on_session_ended):
			api.session_ended.disconnect(_on_session_ended)
	if is_instance_valid(_clock) \
			and _clock.after_tick.is_connected(_on_clock_tick):
		_clock.after_tick.disconnect(_on_clock_tick)
	if is_instance_valid(_liveness):
		if _liveness.entity_live.is_connected(_on_entity_live):
			_liveness.entity_live.disconnect(_on_entity_live)
		if _liveness.entity_dead.is_connected(_on_entity_dead):
			_liveness.entity_dead.disconnect(_on_entity_dead)
	_clock = null
	_liveness = null
	_clock_connected = false
	_liveness_connected = false
	_clear_runtimes()


## Resolves the interpolation runtime owned by [param node]'s session.
static func for_node(node: Node) -> NetwInterpolationInterface:
	var api := NetwMultiplayer.of(node)
	return api.interpolation if api else null


func _on_session_entered() -> void:
	_clock_bind_attempts = 0
	_ensure_clock_connection()
	_ensure_liveness_connection()


func _on_session_ended() -> void:
	_clear_runtimes.call_deferred()


# Resolves the tick engine, or null while no configurator has registered, so
# callers keep today's no-clock fallback semantics against the inert interface.
func _resolve_clock() -> NetwClockInterface:
	var api := _api()
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
	# The clock is inert until a configurator registers. The per-frame pump
	# retries this bind each frame, bounded by the attempt cap, so no node-tree
	# signal is needed to poll for it.
	_clock_bind_attempts += 1


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
	var api := _api()
	var lv := api.liveness if api else null
	if not lv:
		return
	if not lv.entity_live.is_connected(_on_entity_live):
		lv.entity_live.connect(_on_entity_live)
	if not lv.entity_dead.is_connected(_on_entity_dead):
		lv.entity_dead.connect(_on_entity_dead)
	_liveness = lv
	_liveness_connected = true


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
		_apply_body_freeze(runtime, DisplayRole.DISABLED)
	_runtimes.erase(route)
	NetwScriptModel.sweep_dead_overlays()


func _clear_runtimes() -> void:
	for runtime in _runtimes.values():
		_disconnect_hooks(runtime.entity_hooks)
		_apply_body_freeze(runtime, DisplayRole.DISABLED)
	_runtimes.clear()


func _route_of(entity: NetwEntity) -> int:
	if not is_instance_valid(_liveness):
		var api := _api()
		_liveness = api.liveness if api else null
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

	# The output writes back onto the sampled property when it has a source, no
	# visual redirect took it elsewhere, and no .to() renamed the target. Harmless
	# for a frozen REMOTE display, but a self-feeding drag for a PREDICTED or
	# AUTHORITY pump, so the predicted kernels skip it and warn.
	state.self_feedback = has_source and target == node and target_prop == source_prop

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

# Recomputes the pump mode and applies the transition when it changes. Called on
# the events that can change the role (liveness, control, reparent, rebuild,
# handle writes, each record) rather than per frame, so the pump kernel never
# scans control, authority, and streams.
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
	if _pump_is_predicted(pump):
		_warn_self_feedback(runtime)
	if pump == _PUMP_REMOTE:
		_compute_sync_intervals(runtime)
	# The chase absorbs recoveries, so entering it subscribes to the entity's
	# reconciliation writes and leaving it forgets them with their offsets.
	if pump == _PUMP_CHASE:
		var entity := runtime.entity()
		if entity and entity.prediction:
			_bind_hook(
				runtime.chase_hooks,
				entity.prediction,
				entity.prediction.recovered,
				_on_chase_recovered.bind(runtime),
			)
	else:
		_disconnect_hooks(runtime.chase_hooks)
		for state in runtime.states:
			state.chase_offset = null


# Warns once per runtime when a predicted or authority pump owns a channel that
# writes back onto its own sampled property. Those pumps skip the write (RC1), and
# the author should redirect the display through a visual_root or a .to() target so
# the smoothed value never re-enters the simulation.
func _warn_self_feedback(runtime: _Runtime) -> void:
	if runtime.warned_self_feedback:
		return
	for state in runtime.states:
		if not state.self_feedback:
			continue
		runtime.warned_self_feedback = true
		_dbg.warn(
			"interpolation: channel '%s' on '%s' interpolates a locally simulated "
			+ "property in place, so its smoothed output is skipped to keep it out "
			+ "of the control loop. Redirect the display with a .to() target or a "
			+ "visual_root to interpolate it.",
			[state.target_prop, runtime.owner().name if runtime.owner() else "?"],
		)
		return


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
	if _simulates_locally(runtime) and entity.is_controlled_locally:
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
	var api := _api()
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
		var api := _api()
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


# True when this peer runs the entity's own simulation forward, which is the
# body a [constant DisplayRole.PREDICTED] display chases.
#
# Registration alone does not answer this. An entity whose recovery policy
# closes the delay is registered for prediction and simulates nothing here, so
# there is no live body to chase and its display has an authoritative stream to
# play back instead. A component that has not registered yet has no axis to read
# and is taken at its declaration.
func _simulates_locally(runtime: _Runtime) -> bool:
	var entity := runtime.entity()
	var owner := runtime.owner()
	if entity and entity.prediction.is_registered():
		return entity.prediction.sim_mode \
				!= NetwLagCompensationInterface.PredictionHandle.SimMode.DISPLAY
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

## Advances every interpolation runtime one display frame, using [param delta]
## as the frame time. The session drives this once per frame from its poll, so a
## runtime smooths without the interface needing to be a node in the scene tree.
func pump(delta: float) -> void:
	var frame := Engine.get_process_frames()
	if delta > 0.0 and frame == _last_update_frame:
		return
	_last_update_frame = frame

	var clock := _get_clock()
	if not clock:
		_ensure_clock_connection()
		return

	# The shell captures timing once and hands every runtime the same snapshot,
	# so no kernel ever reads the clock and a runtime pump depends only on plain
	# frame scalars owned by that entity.
	var timing := NetwDisplayTiming.capture(clock, delta)
	_stats.reset()
	# TODO: fan this loop out over runtime partitions once engine kernels can run
	# off the main thread natively. Safe by construction: a runtime touches only
	# its own state, pinned by the schedule-independence property test.
	for runtime in _runtimes.values():
		if runtime.disabled:
			# A disable_for window ends when the display clock crosses its
			# deadline, the tick-space replacement for a SceneTreeTimer.
			if runtime.disable_until_tick >= 0 \
					and timing.display_tick >= runtime.disable_until_tick:
				runtime.disabled = false
				runtime.disable_until_tick = -1
			else:
				continue
		_pump_runtime(runtime, timing, _stats)


func _pump_runtime(
		runtime: _Runtime,
		timing: NetwDisplayTiming,
		stats: NetwPumpStats,
) -> void:
	if not runtime.owner():
		return
	# Role resolution is event-driven. It runs on entity liveness, control
	# changes, reparenting, config rebuilds, handle writes, and every network
	# record, so the pump only resolves a runtime that has never been resolved
	# and never scans control, authority, and streams per frame.
	if runtime.pump_mode == _PUMP_UNRESOLVED:
		_resolve_role(runtime)
	match runtime.pump_mode:
		_PUMP_DISABLED:
			return
		_PUMP_CHASE:
			stats.runtimes += 1
			_pump_chase(runtime, timing, stats)
		_:
			stats.runtimes += 1
			_pump_history(runtime, timing, stats)


# Drives the remote and bracketed roles. Both sample the same history through
# the playhead. Remote dilates the playhead, bracketed follows the raw tick.
func _pump_history(
		runtime: _Runtime,
		timing: NetwDisplayTiming,
		stats: NetwPumpStats,
) -> void:
	var playhead := runtime.playhead
	var trace := _should_trace(runtime)
	var dt: int
	var factor: float
	var forecast := false
	var max_forecast_ticks := 0
	if runtime.pump_mode == _PUMP_REMOTE:
		var handle := runtime.handle
		forecast = handle != null and handle.timeline_mode == TimelineMode.FORECAST
		if handle:
			max_forecast_ticks = handle.max_forecast_ticks
		if handle and handle.enable_smart_dilation:
			_dilate_playhead(runtime, timing, stats, trace)
		else:
			playhead.display_lag = 0.0
		var time := (float(timing.display_tick) + timing.tick_factor) \
				- playhead.display_lag
		dt = int(floor(time))
		factor = time - float(dt)
	else:
		dt = maxi(0, timing.display_tick - 1)
		factor = timing.tick_factor
		playhead.display_lag = 0.0
	playhead.display_tick = dt
	stats.max_display_lag = maxf(stats.max_display_lag, playhead.display_lag)

	var eit := playhead.expected_interval_ticks
	for state in runtime.states:
		# The bracketed pump runs on the peer that authors this stream (the server's
		# authority, or a predicted body in bracketed mode). A self-feeding channel
		# would sample its own simulation and write the smoothed value back onto it,
		# so leave the sim value. A REMOTE display writes it harmlessly.
		if state.self_feedback and runtime.pump_mode == _PUMP_BRACKETED:
			continue
		# An empty history has no stream to show. Defer to whatever wrote the
		# property instead of clobbering it with a stale value.
		if state.history.is_empty() or state.history.is_sleeping:
			if state.history.is_sleeping:
				stats.sleeping += 1
			continue
		# A HOLD channel never projects even while the entity forecasts. Others
		# project by their project_by sibling read at this channel's newest tick,
		# an atomic pair, and fall back to their own finite difference.
		var project := forecast and (
			state.spec == null
			or state.spec.forecast_tail != NetwInterpolate.Tail.HOLD
		)
		var velocity: Variant = null
		var has_velocity := false
		if project and state.spec and state.spec.project_channel != &"":
			var sibling: _PropertyState = \
					runtime.states_by_key.get(state.spec.project_channel)
			if sibling and not sibling.history.is_empty():
				velocity = sibling.history.get_at(state.history.newest_tick())
				has_velocity = velocity != null
		var result: Variant = state.history.sample(
			dt,
			factor,
			state.last_written,
			eit,
			project,
			max_forecast_ticks,
			timing.ticktime,
			velocity,
			has_velocity,
		)
		if state.history.last_projected:
			stats.projecting += 1
			stats.max_forecast_age = maxf(
				stats.max_forecast_age,
				state.history.last_project_age,
			)
		if state.history.is_sleeping:
			stats.sleeping += 1
		var weight := 1.0
		if state.spec and state.spec.smoothing > 0.0:
			weight = 1.0 - exp(-timing.frame_delta / state.spec.smoothing)
		result = state.history.smooth_toward(state.last_written, result, weight)
		if state.history.snapped:
			stats.snaps += 1
		if trace:
			_dbg.trace(
				"Interp %s dt=%d lag=%.2f val=%s",
				[state.name, dt, playhead.display_lag, result],
			)
		state.output.write(result)
		state.last_written = result


# Absorbs one reconciliation write into the chase: the offsets are seeded with
# the negated pose change, so the visual stays where it was and glides onto the
# corrected body. Each recovery resets its channel's offset rather than
# accumulating into it, so a correction train cannot wind the visual up, and a
# teleported recovery clears every offset because a genuine desync should be
# seen to snap.
func _on_chase_recovered(
		_entry: int,
		deltas: Dictionary,
		teleported: bool,
		_attribution: int,
		runtime: _Runtime,
) -> void:
	if runtime.pump_mode != _PUMP_CHASE:
		return
	if teleported:
		for state in runtime.states:
			state.chase_offset = null
		return
	var limit := _chase_clamp(runtime)
	for state in runtime.states:
		if state.self_feedback:
			continue
		if not deltas.has(state.source_prop):
			continue
		state.chase_offset = _clamp_delta(
			_scale_delta(deltas[state.source_prop], -1.0),
			limit,
		)


# The largest render offset a chase absorption may hold, the entity's own
# teleport tier: an offset past it would show a pose a teleport was entitled
# to snap through.
func _chase_clamp(runtime: _Runtime) -> float:
	var entity := runtime.entity()
	if entity and entity.prediction:
		return maxf(entity.prediction.teleport_threshold, 0.0)
	return INF


# Multiplies a spatial delta, the negate and decay both chases need. A type
# with no scalable form answers null, so it is never absorbed.
static func _scale_delta(delta: Variant, factor: float) -> Variant:
	match typeof(delta):
		TYPE_FLOAT:
			return (delta as float) * factor
		TYPE_VECTOR2:
			return (delta as Vector2) * factor
		TYPE_VECTOR3:
			return (delta as Vector3) * factor
	return null


# Clamps a delta's magnitude to [param limit], preserving its direction.
static func _clamp_delta(delta: Variant, limit: float) -> Variant:
	match typeof(delta):
		TYPE_FLOAT:
			return clampf(delta as float, -limit, limit)
		TYPE_VECTOR2:
			return (delta as Vector2).limit_length(limit)
		TYPE_VECTOR3:
			return (delta as Vector3).limit_length(limit)
	return null


static func _add_delta(value: Variant, delta: Variant) -> Variant:
	match typeof(delta):
		TYPE_FLOAT:
			return (value as float) + (delta as float)
		TYPE_VECTOR2:
			return (value as Vector2) + (delta as Vector2)
		TYPE_VECTOR3:
			return (value as Vector3) + (delta as Vector3)
	return value


static func _delta_spent(delta: Variant) -> bool:
	match typeof(delta):
		TYPE_FLOAT:
			return absf(delta as float) < 0.0001
		TYPE_VECTOR2:
			return (delta as Vector2).length() < 0.0001
		TYPE_VECTOR3:
			return (delta as Vector3).length() < 0.0001
	return true


# Eases each visual toward its live predicted body every frame, carrying any
# chase-absorbed recovery offset as it decays.
func _pump_chase(
		runtime: _Runtime,
		timing: NetwDisplayTiming,
		stats: NetwPumpStats,
) -> void:
	var weight := 1.0 - exp(
			-timing.frame_delta / _predicted_effective_smooth_time(runtime, timing),
	)
	var glide := 1.0
	if runtime.handle:
		glide = exp(
				-timing.frame_delta / maxf(runtime.handle.chase_glide_time, 0.001),
		)
	var trace := _should_trace(runtime)
	for state in runtime.states:
		# A self-feeding channel would smooth the live body against its own last
		# output and write the drag back onto the simulation. Leave the sim value.
		if state.self_feedback:
			continue
		if not is_instance_valid(state.source_obj):
			continue
		var value: Variant = state.source_obj.get(state.source_prop)
		if state.chase_offset != null:
			state.chase_offset = _scale_delta(state.chase_offset, glide)
			if state.chase_offset == null or _delta_spent(state.chase_offset):
				state.chase_offset = null
			else:
				value = _add_delta(value, state.chase_offset)
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
		timing: NetwDisplayTiming,
		stats: NetwPumpStats,
		trace: bool,
) -> void:
	var playhead := runtime.playhead
	var handle := runtime.handle
	var raw_floor := _calculate_min_lag(runtime, timing)
	playhead.smoothed_floor += (raw_floor - playhead.smoothed_floor) \
			* handle.floor_smoothing

	var effective_dt := int(floor(float(timing.display_tick) - playhead.display_lag))
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
		stats.starving += 1
		playhead.starvation_ticks += 1
		for state in runtime.states:
			state.history.is_sleeping = false
	else:
		playhead.starvation_ticks = 0

	if playhead.starvation_ticks >= handle.starvation_grace_frames:
		playhead.display_lag = minf(
			playhead.display_lag + timing.frame_ticks * handle.starvation_growth,
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


func _calculate_min_lag(runtime: _Runtime, timing: NetwDisplayTiming) -> float:
	var handle := runtime.handle
	if handle and handle.timeline_mode == TimelineMode.FORECAST:
		# Forecast targets the newest sample: no jitter buffer, the tail
		# projection covers the snapshot gaps instead of a display delay.
		return 0.0
	var needed := float(runtime.playhead.expected_interval_ticks + 1)
	var network_padding := float(
		maxi(
			0,
			timing.recommended_display_offset - timing.display_offset,
		),
	)
	return maxf(
		0.0,
		needed - float(timing.display_offset) + network_padding,
	)


# Captures the timing snapshot for a shell-phase event (reset, rebuild) that
# needs the lag floor between pumps. The pump path never calls this; it receives
# its snapshot by value from the shell entry instead.
func _capture_timing() -> NetwDisplayTiming:
	return NetwDisplayTiming.capture(_get_clock(), 0.0)


func _reset_runtime(runtime: _Runtime) -> void:
	var playhead := runtime.playhead
	var target_lag := _calculate_min_lag(runtime, _capture_timing())
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


# The chase smoothing time, from the handle override or a timing-derived
# default of most of one tick.
func _predicted_effective_smooth_time(
		runtime: _Runtime,
		timing: NetwDisplayTiming,
) -> float:
	var handle := runtime.handle
	if handle and handle.predicted_smooth_time > 0.0:
		return handle.predicted_smooth_time
	if timing.ticktime > 0.0:
		return maxf(timing.ticktime * 0.85, 0.001)
	return 1.0 / 60.0

#endregion

#region Sync interval and authorship (role resolution reads the sync shape)

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


# True for a plain display synchronizer whose receive path is the consumed
# apply feed. Read by role resolution to tell an authored stream from a
# received one.
func _sync_feeds_consumed(sync: MultiplayerSynchronizer) -> bool:
	if not sync.replication_config:
		return false
	if not sync.public_visibility:
		return false
	return true

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

	## Seconds a chase-absorbed recovery offset takes to decay by
	## [code]1/e[/code].
	##
	## Under [constant PredictedMode.CHASE] each sub-teleport recovery's pose
	## change is absorbed as a decaying render offset, so the visual stays
	## where it was and glides onto the corrected body instead of jumping with
	## it. Each recovery resets its channel's offset rather than accumulating
	## into it, the offset is clamped to the entity's
	## [member NetwLagCompensationInterface.PredictionHandle.teleport_threshold],
	## and a teleported recovery clears every offset, because a genuine desync
	## should be seen to snap. Those three rules are what keep a correction
	## train from winding the visual away from the body.
	var chase_glide_time: float = 0.15

	## Enables display lag adaptation for remote interpolation.
	var enable_smart_dilation: bool = true

	## Where on the timeline remote display renders. [constant TimelineMode.FORECAST]
	## targets the newest sample and projects each [NetwInterpolate] channel across
	## the snapshot gaps. Only [constant DisplayRole.REMOTE] entities forecast, so
	## an authored or predicted display ignores this.
	var timeline_mode: TimelineMode = TimelineMode.BUFFERED

	## Ticks the display may project past its newest sample under
	## [constant TimelineMode.FORECAST]. The projection age clamps here, so a
	## stalled stream freezes at the cap instead of drifting away.
	var max_forecast_ticks: int = 6

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


	## Temporarily disables interpolation for about [param duration] seconds.
	##
	## The runtime re-enables once the display clock crosses the tick deadline the
	## duration implies, so the engine owns the timer through
	## [member NetwDisplayTiming.display_tick] instead of a [SceneTreeTimer] bound
	## to the scene tree.
	func disable_for(duration: float) -> void:
		var iface := _interface()
		var runtime := _runtime()
		if not iface or not runtime:
			return
		runtime.disabled = true
		reset()
		var timing := iface._capture_timing()
		var ticks := 1
		if timing.ticktime > 0.0:
			ticks = maxi(1, ceili(duration / timing.ticktime))
		runtime.disable_until_tick = timing.display_tick + ticks


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
	var pump_mode: int = _PUMP_UNRESOLVED
	var rebuild_queued := false
	var authoring_binding: NetwSyncSetBinding
	var trace_frame := 0
	var saved_freeze: Dictionary = { }
	var entity_hooks: Array = []
	# Recovery-absorption subscription, held only while the chase pump runs.
	var chase_hooks: Array = []
	var disabled := false
	# Display tick past which a disable_for window re-enables the runtime, or -1
	# when the runtime is not on a timed disable.
	var disable_until_tick := -1
	# Set once the self-feedback warning has fired, so it warns per runtime not per
	# role resolution.
	var warned_self_feedback := false


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
	# True when the output writes the very property it samples on the very object it
	# samples it from, with no visual root and no .to() redirect. A REMOTE display
	# writes it harmlessly (the body is frozen), but a PREDICTED or AUTHORITY pump
	# that samples the live simulation and writes the smoothed value back onto it
	# feeds the display filter into the control loop, so those pumps skip it.
	var self_feedback := false
	var last_written: Variant
	# Decaying render offset a chase-absorbed recovery seeded, or null while no
	# recovery is being absorbed on this channel.
	var chase_offset: Variant = null


	func copy_shape_from(other: _PropertyState) -> void:
		spec = other.spec
		source_obj = other.source_obj
		source_prop = other.source_prop
		target_obj = other.target_obj
		target_prop = other.target_prop
		self_feedback = other.self_feedback
		authoring_ticks = authoring_ticks or other.authoring_ticks

#endregion

#region Display engine

# Per runtime display cursor. Carries signed display lag so a forecasting
# playhead can lead the newest tick the same way dilation trails it.
class _Playhead:
	extends RefCounted

	var display_lag := 0.0
	var starvation_ticks := 0
	var smoothed_floor := 0.0
	var expected_interval_ticks := 3
	var display_tick := -1


# Per value ring buffer with a total resample. Sampling past the newest tick
# holds the last value under a buffered playhead, or projects it forward by its
# derivative under a forecasting one, capped by the forecast tick budget. The
# tick domain is fixed by the first record and asserts on mixing authoring and
# receive ticks.
class _History:
	extends RefCounted

	var buffer := NetwRingBuffer.new(16)
	var mode: NetwInterpolate.Mode = NetwInterpolate.Mode.LERP
	var snap_distance := 0.0
	var is_sleeping := false
	var _tick_domain := -1
	# The value type pins on the first record and holds for the channel's life,
	# even across a teleport clear. A homogeneous history lets the native port
	# specialize storage per type and keeps the typeof branch out of the sample
	# hot loop.
	var _value_type := TYPE_NIL
	var _last_recorded: Variant
	var _has_recorded := false
	var _cached_prev := -1
	var _search := PackedInt32Array([-1, -1])
	# Set by the last sample: whether it projected past the newest tick and by how
	# many ticks, plus whether it took a snap escape. The pump reads these into the
	# stats record the same way it reads is_sleeping, so the kernel never counts.
	var last_projected := false
	var last_project_age := 0.0
	var snapped := false


	func record(tick: int, value: Variant, authoring_tick: bool) -> void:
		var domain := 1 if authoring_tick else 0
		if _tick_domain == -1:
			_tick_domain = domain
		else:
			assert(
				_tick_domain == domain,
				"interpolation: mixing authoring and receive ticks",
			)
		var value_type := typeof(value)
		if _value_type == TYPE_NIL:
			_value_type = value_type
		else:
			assert(
				_value_type == value_type,
				"interpolation: channel value type changed",
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
	# [param last_written]. Past the newest tick holds the newest value under a
	# buffered playhead and may mark the history asleep, or projects it forward
	# under a forecasting one. Projection reads [param explicit_velocity] when
	# [param has_explicit_velocity] is set, otherwise the finite difference of the
	# last two samples, and clamps the age to [param max_forecast_ticks].
	func sample(
			dt: int,
			factor: float,
			last_written: Variant,
			expected_interval_ticks: int,
			forecast := false,
			max_forecast_ticks := 0,
			ticktime := 0.0,
			explicit_velocity: Variant = null,
			has_explicit_velocity := false,
	) -> Variant:
		last_projected = false
		snapped = false
		buffer.find_bracketing_ticks(dt, _cached_prev, _search)
		var prev_tick := _search[0]
		var next_tick := _search[1]
		_cached_prev = prev_tick
		if prev_tick == -1:
			return last_written
		if next_tick == -1:
			var result: Variant = buffer.get_at(prev_tick)
			# A forecasting playhead projects the tail. A stationary or
			# unprojectable channel falls through to the buffered hold below, which
			# is the only path that sleeps: a moving projection must keep writing.
			if forecast and NetwProject.supports(typeof(result)):
				var velocity: Variant = explicit_velocity if has_explicit_velocity \
						else _finite_velocity(prev_tick, ticktime)
				if velocity != null and not _velocity_negligible(velocity):
					var age := clampf(
						(float(dt) + factor) - float(prev_tick),
						0.0,
						float(max_forecast_ticks),
					)
					last_projected = true
					last_project_age = age
					return NetwProject.project(result, velocity, age * ticktime)
			if not buffer.has_tick_after(prev_tick) \
					and _is_close(last_written, result):
				is_sleeping = true
			return result
		var p_val: Variant = buffer.get_at(prev_tick)
		var n_val: Variant = buffer.get_at(next_tick)
		if snap_distance > 0.0 and _snap(p_val, n_val, snap_distance):
			snapped = true
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
			snapped = true
			return result
		return _interpolate(last_written, result, weight)


	# The channel's velocity implied by its two newest samples, in units per
	# second, matching the value type. Returns null when a second sample or a
	# positive span is missing, which forces the caller to hold instead.
	func _finite_velocity(newest: int, ticktime: float) -> Variant:
		if ticktime <= 0.0:
			return null
		var older := buffer.bracketing_ticks(newest - 1).x
		if older == -1 or older == newest:
			return null
		var span := float(newest - older) * ticktime
		if span <= 0.0:
			return null
		var new_val: Variant = buffer.get_at(newest)
		var old_val: Variant = buffer.get_at(older)
		match typeof(new_val):
			TYPE_FLOAT:
				var d: float = angle_difference(old_val, new_val) \
						if mode == NetwInterpolate.Mode.ANGLE \
						else float(new_val) - float(old_val)
				return d / span
			TYPE_VECTOR2:
				return ((new_val as Vector2) - (old_val as Vector2)) / span
			TYPE_VECTOR3:
				return ((new_val as Vector3) - (old_val as Vector3)) / span
			TYPE_QUATERNION:
				var delta := (new_val as Quaternion) * (old_val as Quaternion).inverse()
				var angle := 2.0 * acos(clampf(delta.w, -1.0, 1.0))
				if angle < 0.0001:
					return Vector3.ZERO
				var axis := Vector3(delta.x, delta.y, delta.z).normalized()
				return axis * (angle / span)
		return null


	# True when a projected velocity is small enough that projecting it would not
	# move the display, so the channel is free to hold and sleep instead.
	func _velocity_negligible(velocity: Variant) -> bool:
		match typeof(velocity):
			TYPE_FLOAT:
				return absf(velocity) < 0.0001
			TYPE_VECTOR2:
				return (velocity as Vector2).length() < 0.0001
			TYPE_VECTOR3:
				return (velocity as Vector3).length() < 0.0001
		return false


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


	# The node writer runs on the shell: it sets Node properties and reads parent
	# transforms for global-space output, so a runtime bound to it pumps on the
	# main thread. A future RenderingServer writer returns ANY so swarm display
	# can sample and emit off the main thread.
	# TODO: add a RenderingServer writer backend (thread_class ANY) once entities
	# can bind canvas-item or instance RIDs as display views, so swarm display
	# never touches the SceneTree.
	func thread_class() -> StringName:
		return &"SHELL"


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
