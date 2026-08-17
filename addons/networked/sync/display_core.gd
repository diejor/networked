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
## [NetwDisplayHandle].
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
## [method NetwDisplayHandle.get_buffer]. A playhead reads that
## buffer behind the newest entry, lands between two recorded ticks, and the
## displayed value is the interpolation between them.
## [member NetwDisplayHandle.display_lag] is how far behind the
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
## [member NetwDisplayHandle.display_role] overrides it. The
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
## that bracket it. [member ClockCore.display_tick] already trails the
## simulation by [member ClockCore.display_offset], and
## [member NetwDisplayHandle.display_lag] subtracts the extra
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
## [member NetwDisplayHandle.display_lag] eases toward a floor
## set by the expected snapshot interval and the clock display offset. When
## snapshots starve, no recorded tick sits after the playhead, so the lag grows
## to rebuild the buffer and settles back once packets resume.
## [member NetwDisplayHandle.enable_smart_dilation] turns the
## loop off for a fixed zero lag, and the tuning members on
## [NetwDisplayHandle] shape the rest.
##
## [br][br][b]Naming the displayed tick[/b]
## [br]When a stamped derived state set drives the entity, history is keyed by
## the frame's authoring tick instead of the receive tick. That lets
## [method NetwDisplayHandle.displayed_authoring_tick] name the
## exact server tick under the playhead, which a firing client sends to the
## server for lag-compensated rewind through [LagCompCore]. The displayed
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
## [member NetwDisplayHandle.predicted_mode] picks the filter.
## [constant PredictedMode.CHASE] eases the visual toward the live body with an
## exponential time from
## [member NetwDisplayHandle.predicted_smooth_time], the one role
## that reads the body instead of history.
## [constant PredictedMode.BRACKETED] records the predicted body each tick and
## interpolates the previous and current samples exactly like a remote entity, so
## it is the remote pipeline fed by a local sampler.
##
## [br][br][b]Writing the visual[/b]
## [br]Smoothing must not fight the physics engine.
## [member NetwDisplayHandle.visual_root] points at a child
## that carries the smooth output while the body, [member NetwEntity.owner],
## keeps the raw pose that the derived state set and native replication write.
## There are two write-out modes, chosen by the visual's
## [member CanvasItem.top_level] ([member Node3D.top_level] in 3D).
## A parented visual takes global-space writes for the
## [constant NetwDisplayHandle.GLOBAL_SPACE_CHANNELS], so a replicated write to
## the body never moves them through transform inheritance, while every
## channel without a [NetwInterpolate] spec still inherits from the body for
## free, a facing flip, an animation bob, or a riding attachment among them.
## Any other spatial channel has no per-channel global setter, so the service
## warns when it builds that channel's display state. A top-level visual is a
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
## [method NetwDisplayHandle.snap_property] and
## [method NetwDisplayHandle.reset] bypass smoothing for teleports.
##
## [br][br][b]RPC and signal arguments[/b]
## [br]Discrete events carry continuous values too. A [NetwInterpolate] per
## argument on [method Netw.configure_rpc] or [method Netw.configure_signal]
## smooths that argument into the property named by [member NetwInterpolate.target]
## while the handler still runs, so a periodic aim or nudge glides between events.
## Unlike a property, an argument has no implicit source, so
## [member NetwInterpolate.target] must be set.
class_name DisplayCore
extends RefCounted

## Display strategy vocabulary. Mirrors [enum NetwDisplayHandle.DisplayRole].
const DisplayRole := NetwDisplayHandle.DisplayRole

## Predicted display filter vocabulary. Mirrors
## [enum NetwDisplayHandle.PredictedMode].
const PredictedMode := NetwDisplayHandle.PredictedMode

## Remote timeline vocabulary. Mirrors [enum NetwDisplayHandle.TimelineMode].
const TimelineMode := NetwDisplayHandle.TimelineMode

const _MAX_CLOCK_BIND_ATTEMPTS := 60

# Internal pump modes resolved from the display role and predicted mode.
const _PUMP_UNRESOLVED := NetwDisplayDecl.PUMP_UNRESOLVED
const _PUMP_DISABLED := NetwDisplayDecl.PUMP_DISABLED
const _PUMP_REMOTE := NetwDisplayDecl.PUMP_REMOTE
const _PUMP_BRACKETED := NetwDisplayDecl.PUMP_BRACKETED
const _PUMP_CHASE := NetwDisplayDecl.PUMP_CHASE

const _REBUILD_KEY := &"display-rebuild"

# The pump table is the session plane's, so a read that only needs a row does
# not need this core at all. A core with no session standing behind it keeps a
# table of its own, which is what a treeless rig drives.
var _book: NetwDisplayBook:
	get:
		if _book_cache == null:
			_book_cache = _resolve_book()
		return _book_cache

var _book_cache: NetwDisplayBook
# Display config, keyed by entity RID because config precedes liveness: a route
# exists only once the entity is live, and a RID is what the flat verbs address.
var _clock: ClockCore
var _native_core: NetwMultiplayerCore
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
	api._connect_once(api.session_entered, _on_session_entered)
	api._connect_once(api.session_ended, _on_session_ended)
	_ensure_liveness_connection()
	_ensure_clock_connection()


func _api() -> NetwMultiplayer:
	return _api_ref.get_ref() as NetwMultiplayer if _api_ref else null


# The session's pump table, or a private one when no session stands behind this
# core. The drain is connected here rather than at construction because the
# table is the session plane's and is reached lazily, and a core with no session
# has no settle to drain into.
func _resolve_book() -> NetwDisplayBook:
	var api := _api()
	var core: NetwMultiplayerCore = api._native_core if api else null
	var book: NetwDisplayBook = (
			core.display_book if core else NetwDisplayBook.new()
	)
	if api:
		api._connect_once(book.went_dirty, _on_book_went_dirty)
	return book


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
	if is_instance_valid(_native_core):
		if _native_core.entity_live.is_connected(_on_entity_live):
			_native_core.entity_live.disconnect(_on_entity_live)
		if _native_core.entity_dead.is_connected(_on_entity_dead):
			_native_core.entity_dead.disconnect(_on_entity_dead)
	_clock = null
	_native_core = null
	_clock_connected = false
	_liveness_connected = false
	_clear_runtimes()


## Resolves the interpolation runtime owned by [param node]'s session.
static func for_node(node: Node) -> DisplayCore:
	var api := NetwMultiplayer.of(node)
	return api._display if api else null


func _on_session_entered() -> void:
	_clock_bind_attempts = 0
	_ensure_clock_connection()
	_ensure_liveness_connection()


func _on_session_ended() -> void:
	_clear_runtimes.call_deferred()


# Resolves the tick engine, or null while no configurator has registered, so
# callers keep today's no-clock fallback semantics against the inert interface.
func _resolve_clock() -> ClockCore:
	var api := _api()
	if api and api._clock.is_configured():
		return api._clock
	return null


func _ensure_clock_connection() -> void:
	if _clock_connected:
		return
	var clock := _resolve_clock()
	if clock:
		var api := _api()
		if api:
			api._connect_once(clock.after_tick, _on_clock_tick)
		_clock = clock
		_clock_connected = true
		return
	# The clock is inert until a configurator registers. The per-frame pump
	# retries this bind each frame, bounded by the attempt cap, so no node-tree
	# signal is needed to poll for it.
	_clock_bind_attempts += 1


# Cached session clock, resolved and memoized on first use.
func _get_clock() -> ClockCore:
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
	var lv := api._native_core if api else null
	if not lv:
		return
	api._connect_once(lv.entity_live, _on_entity_live)
	api._connect_once(lv.entity_dead, _on_entity_dead)
	_native_core = lv
	_liveness_connected = true

#region Record seam

## Records [param value] for [param target_property] on [param node].
##
## [param tick] is the key in that value's history. The interpolation spec is
## resolved from the configuration registry. An unconfigured target is a no-op.
## [param tick] is the local receive tick from a property sync feeder. An
## entity displaying under [constant DisplayRole.PREDICTED] keeps following
## its locally simulated state. Values recorded during prediction stay out
## of the display but keep the history warm, so a demotion to
## [constant DisplayRole.REMOTE] resumes from authority rows instead of
## holding the last predicted pose.
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
	# A bracketed pump fills these histories from the local sampler, so a
	# network feed must not mix its tick domain into that ring. The chase
	# pump displays the live body and never samples history, so network rows
	# recorded during prediction stay display-invisible while keeping the
	# ring warm for a demote handoff to the remote role.
	_resolve_role(runtime)
	if runtime.pump_mode == _PUMP_BRACKETED:
		return
	var channel := runtime.tracks.by_key(_state_key_for(node, target_property))
	var state: NetwDisplayChannel = runtime.states[channel] if channel >= 0 else null
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
	var api := _api()
	if api:
		api.report_event(
			NetwMultiplayerCore.DISPLAY_RECORD,
			route,
			{ track = target_property, tick = tick },
		)


# Feeds the bracketed pipelines. Records the live source value of every state
# at the current tick so the same resample path the remote role uses can drive
# a locally simulated visual.
func _on_clock_tick(_delta: float, tick: int) -> void:
	for runtime: NetwDisplayRuntime in _book.runtimes():
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

#region Config

# The config record behind one entity, which is the entity handle's own
# declaration published under the entity's RID. The handle names the entity
# rather than one of its lives, so the same declaration is published again
# under the fresh RID a re-admitted entity is given.
func _config_for(entity: NetwEntity) -> NetwDisplayDecl:
	if entity == null:
		return null
	var rid := entity.rid
	var config := _book.decl_of(rid)
	if config:
		return config
	var handle := entity.interpolation
	config = handle._declaration() if handle else NetwDisplayDecl.new()
	if rid.is_valid():
		_book.set_decl(rid, config)
	return config


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
	var config := _config_for(entity)
	if config and config.display_role != DisplayRole.AUTO:
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
	var runtime := _book.runtime_at(route)
	if runtime:
		_disconnect_hooks(runtime.entity_hooks)
		_apply_body_freeze(runtime, DisplayRole.DISABLED)
	_book.drop_route(route)
	NetwScriptModel.sweep_dead_overlays()


func _clear_runtimes() -> void:
	for runtime: NetwDisplayRuntime in _book.runtimes():
		_disconnect_hooks(runtime.entity_hooks)
		_apply_body_freeze(runtime, DisplayRole.DISABLED)
	_book.clear()


func _route_of(entity: NetwEntity) -> int:
	if not is_instance_valid(_native_core):
		var api := _api()
		_native_core = api._native_core if api else null
	if _native_core:
		var route := _native_core.liveness_route_of(entity)
		if route > 0:
			return route
	return entity.route


func _runtime_for(route: int, entity: NetwEntity) -> NetwDisplayRuntime:
	if route <= 0 or entity == null:
		return null
	var runtime := _book.runtime_at(route)
	if runtime:
		return runtime
	runtime = NetwDisplayRuntime.new()
	runtime.route = route
	runtime.bind(entity, entity.owner)
	runtime.config = _config_for(entity)
	runtime.playhead = NetwDisplayPlayhead.new()
	_book.enroll(entity.rid, route)
	_book.set_runtime(entity.rid, runtime)
	return runtime


# Returns the runtime behind one entity handle, for the reads that run before
# the flat surface is reachable.
func _runtime_for_handle(handle: NetwDisplayHandle) -> NetwDisplayRuntime:
	var entity := handle.entity()
	if not entity:
		return null
	return _book.runtime_of(entity.rid)


# Returns the runtime behind one flat entity handle.
func _runtime_for_entity(entity: RID) -> NetwDisplayRuntime:
	return _book.runtime_of(entity)


# Removes the runtime behind one flat entity handle.
func _remove_entity_runtime(entity: RID) -> void:
	var route := _book.route_of(entity)
	if route > 0:
		_on_entity_dead(route)


# Reports that the channel table under one flat entity handle is stale.
func _mark_runtime_dirty(entity: RID) -> void:
	_book.mark_dirty(entity)


# An entity with no runtime is adopted on the spot, and one that has a runtime
# waits for the settle. The two answer the same question at different times
# because a caller that configures an entity and displays it in one breath has
# no settle in between, while a cascade of writes to a runtime that is already
# showing costs one rebuild by waiting.
func _on_book_went_dirty(entity: RID, dirt: int) -> void:
	var api := _api()
	if api == null:
		return
	var runtime := _runtime_for_entity(entity)
	if runtime == null:
		_book.clear_dirty(entity)
		# The entity can go live before its configuration lands (the shell
		# pushes specs in _ready, after entity registration), so the liveness
		# pass skipped it. A server-authority entity receives no network
		# records to lazily create the runtime either, so adopt it here.
		var wrapper := api._entity_wrapper(entity)
		if wrapper:
			_on_entity_live(_route_of(wrapper), wrapper)
		return
	if dirt == NetwDisplayDecl.DIRT_ROLE:
		_resolve_role(runtime)
		return
	api._settle_schedule(_drain_dirty, _REBUILD_KEY)


func _drain_dirty() -> void:
	for entity: RID in _book.take_dirty():
		var runtime := _runtime_for_entity(entity)
		if runtime:
			_rebuild_runtime(runtime)


# Re-resolves only the display source while preserving its channel outputs.
func _mark_role_dirty(entity: RID) -> void:
	var runtime := _runtime_for_entity(entity)
	if not runtime:
		_mark_runtime_dirty(entity)
		return
	_resolve_role(runtime)


# Connects [param cb] to [param sig] and stores the triple so one helper can
# disconnect it later even after [param source] is freed.
func _bind_hook(
		store: Array,
		source: Object,
		sig: Signal,
		cb: Callable,
) -> void:
	if not sig.is_connected(cb):
		var api := _api()
		if api:
			api._connect_once(sig, cb)
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
	var runtime := _book.runtime_at(route)
	if not runtime:
		return
	_resolve_role(runtime)


func _on_entity_reparented(
		_reparent: NetwReparentOpts,
		route: int,
) -> void:
	var runtime := _book.runtime_at(route)
	if not runtime:
		return
	var entity := runtime.entity()
	if not entity:
		return
	runtime.bind(entity, entity.owner)
	_rebuild_runtime(runtime)


func _rebuild_runtime(runtime: NetwDisplayRuntime) -> void:
	if not runtime:
		return
	runtime.rebuild_queued = false
	var entity := runtime.entity()
	var owner := runtime.owner()
	if not entity or not is_instance_valid(owner):
		return
	runtime.states.clear()
	runtime.tracks.clear()
	runtime.playhead.display_tick = -1
	_build_property_states(runtime)
	_build_arg_target_states(runtime)
	_compute_sync_intervals(runtime)
	runtime.pump_mode = _PUMP_UNRESOLVED
	_resolve_role(runtime)
	_reset_runtime(runtime)


func _build_property_states(runtime: NetwDisplayRuntime) -> void:
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


func _build_arg_target_states(runtime: NetwDisplayRuntime) -> void:
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


func _build_arg_states(runtime: NetwDisplayRuntime, node: Node, specs: Array) -> void:
	for raw_spec in specs:
		var spec := raw_spec as NetwInterpolate
		if not spec or spec.mode == NetwInterpolate.MODE_NONE:
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
		runtime: NetwDisplayRuntime,
		node: Node,
		source_prop: StringName,
		target_prop: StringName,
		spec: NetwInterpolate,
		authoring_tick: bool,
) -> NetwDisplayChannel:
	if not spec or spec.mode == NetwInterpolate.MODE_NONE:
		return null
	var has_source := not source_prop.is_empty()

	var state := NetwDisplayChannel.new()
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
	var config := runtime.config
	var visual: Node = null
	if owner and config and not config.visual_root.is_empty():
		visual = owner.get_node_or_null(config.visual_root)
	var target: Object = visual if has_source and visual else node
	state.target_obj = target

	var history := NetwDisplayHistory.new()
	history.mode = spec.mode
	history.snap_distance = spec.snap_distance
	state.history = history

	var output := _Output.new()
	output.api_ref = weakref(_api()) if _api() else null
	var runtime_entity := runtime.entity()
	output.entity = runtime_entity.rid if runtime_entity else RID()
	output.track = target_prop
	output.port.bind(target, runtime.owner())
	output.port.declare(
		target_prop,
		source_prop,
		(
				visual != null
				and target == visual
				and target_prop in NetwDisplayHandle.GLOBAL_SPACE_CHANNELS
		),
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
			and not output.port.get_global_space() \
			and not visual.get(&"top_level") \
			and target_prop in [
				&"rotation",
				&"scale",
				&"transform",
				&"quaternion",
				&"basis",
				&"skew",
			]:
		_dbg.warn(
			"interpolation: channel '%s' on parented visual '%s' is written "
			+ "in local space and will be dragged by body writes. Set "
			+ "top_level on the visual, or interpolate position/rotation "
			+ "channels instead.",
			[target_prop, visual.name],
		)

	state.last_written = _current_source_value(state)

	var declared := runtime.tracks.by_key(state.state_key)
	if declared >= 0:
		var existing: NetwDisplayChannel = runtime.states[declared]
		existing.copy_shape_from(state)
		return existing
	var claimant := runtime.tracks.by_name(state.name)
	runtime.tracks.declare(state.state_key, state.name)
	runtime.states.append(state)
	if claimant >= 0:
		_warn_double_write(runtime.states[claimant], state)
	return state


# Two states that share only the property name make a name lookup ambiguous,
# which NetwDisplayTracks already records. Two that write the same property on
# the same object are a real double-write, which only the states know.
func _warn_double_write(existing: NetwDisplayChannel, state: NetwDisplayChannel) -> void:
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
func _resolve_role(runtime: NetwDisplayRuntime) -> void:
	var role := _resolve_display_role(runtime)
	var pump := _pump_for(runtime, role)
	runtime.role = role
	if pump == runtime.pump_mode:
		return
	var previous_pump := runtime.pump_mode
	runtime.display_offset_limit = _chase_clamp(runtime)

	if NetwDisplayDecl.pump_clears_history(previous_pump, pump):
		for state in runtime.states:
			state.history.clear()
	if NetwDisplayDecl.pump_arms_offsets(previous_pump, pump):
		for state in runtime.states:
			state.offset.arm(not state.self_feedback)

	runtime.pump_mode = pump
	_apply_body_freeze(runtime, role)
	if NetwDisplayDecl.pump_is_predicted(pump):
		_warn_self_feedback(runtime)
	if pump == _PUMP_REMOTE:
		_compute_sync_intervals(runtime)
	# The chase absorbs recoveries, so entering it subscribes to the entity's
	# reconciliation writes. A source transition owns any offset after leaving.
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
		if pump == _PUMP_DISABLED:
			for state in runtime.states:
				state.offset.clear()


# Warns once per runtime when a predicted or authority pump owns a channel that
# writes back onto its own sampled property. Those pumps skip the write (RC1), and
# the author should redirect the display through a visual_root or a .to() target so
# the smoothed value never re-enters the simulation.
func _warn_self_feedback(runtime: NetwDisplayRuntime) -> void:
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


func _pump_for(runtime: NetwDisplayRuntime, role: DisplayRole) -> int:
	var config := runtime.config
	return config.pump_for(role) if config else _PUMP_DISABLED


func _resolve_display_role(runtime: NetwDisplayRuntime) -> DisplayRole:
	var config := runtime.config
	if config and config.display_role != DisplayRole.AUTO:
		return config.display_role
	var owner := runtime.owner()
	var entity := runtime.entity()
	var facts := NetwDisplayRoleFacts.new()
	facts.simulates_locally = _simulates_locally(runtime)
	facts.controlled_locally = entity.is_controlled_locally
	facts.predicted_input = entity.prediction.input_source \
			== NetwPredict.InputSource.PREDICTED
	facts.prediction_registered = entity.prediction.is_registered()
	facts.authors_streams = _authors_display_streams(runtime)
	facts.owner_is_authority = owner != null and owner.is_multiplayer_authority()
	return facts.resolve() as DisplayRole


# True when every sync channel feeding this entity's tracked values is authored
# by this peer, so no receive stream exists to consume and the display must
# sample the local simulation instead. At least one channel must exist. Derived
# sets ride the same rule: a public set whose fields feed tracked values counts
# as a stream, and authorship follows the set's policy, the same predicate the
# send-side pump gates on.
func _authors_display_streams(runtime: NetwDisplayRuntime) -> bool:
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
		for binding in api._replication.derived_group(runtime.route):
			var node := binding.node()
			if not is_instance_valid(node) or not node.is_inside_tree():
				continue
			if binding.set.audience != NetwPropertySet.Audience.AUDIENCE_PUBLIC:
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
		binding: NetwPropertySetBinding,
		entity: NetwEntity,
) -> bool:
	var node := binding.node()
	if not is_instance_valid(node):
		return false
	var api := _api()
	if api == null:
		return false
	if binding.set.record == NetwPropertySet.Record.RECORD_STATE:
		return api.get_unique_id() == 1
	# Authoring is the same question a receiver asks about an incoming write,
	# with this peer's own id as the sender.
	return NetwEntityControl.policy_admits(
		binding.set.policy,
		api.get_unique_id(),
		node.get_multiplayer_authority(),
		entity.controller,
	)


# The derived counterpart of _sync_replicates_tracked_property: a set feeds the
# runtime when any field key names a tracked value's source or name.
func _set_replicates_tracked_property(
		runtime: NetwDisplayRuntime,
		set: NetwPropertySet,
) -> bool:
	for field in set.columns:
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
func _simulates_locally(runtime: NetwDisplayRuntime) -> bool:
	var entity := runtime.entity()
	var owner := runtime.owner()
	if entity and entity.prediction.is_registered():
		return entity.prediction.sim_mode \
				!= NetwPredict.SimMode.DISPLAY
	if owner:
		return owner.get_node_or_null("%PredictionComponent") != null
	return false


func _apply_body_freeze(runtime: NetwDisplayRuntime, role: DisplayRole) -> void:
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
func pump(delta: float) -> Error:
	var frame := Engine.get_process_frames()
	if delta > 0.0 and frame == _last_update_frame:
		return OK
	_last_update_frame = frame

	var clock := _get_clock()
	if not clock:
		_ensure_clock_connection()
		return OK

	# The shell captures timing once and hands every runtime the same snapshot,
	# so no kernel ever reads the clock and a runtime pump depends only on plain
	# frame scalars owned by that entity.
	var timing := NetwDisplayTiming.capture(clock, delta)
	_stats.reset()
	var api := _api()
	if api:
		api._display_pump_timing = timing
	# TODO: fan this loop out over runtime partitions once engine kernels can run
	# off the main thread natively. Safe by construction: a runtime touches only
	# its own state, pinned by the schedule-independence property test.
	for runtime: NetwDisplayRuntime in _book.runtimes():
		if runtime.disabled:
			# A disable_for window ends when the display clock crosses its
			# deadline, the tick-space replacement for a SceneTreeTimer.
			if runtime.disable_until_tick >= 0 \
					and timing.display_tick >= runtime.disable_until_tick:
				runtime.disabled = false
				runtime.disable_until_tick = -1
			else:
				continue
		if api:
			var entity: NetwEntity = runtime.entity()
			if not entity:
				continue
			var verdict := api._display_pump_entity(
				entity.rid,
				timing.display_tick,
				timing.tick_factor,
				delta,
			)
			api.report_event(
				NetwMultiplayerCore.DISPLAY_PUMP,
				api._native_core.liveness_core.route_of(entity.rid),
				{ tick = timing.display_tick },
				0,
				&"",
				{ },
				verdict,
			)
			if verdict != OK:
				api._display_pump_timing = null
				return verdict
		else:
			_pump_runtime(runtime, timing, _stats)
	if api:
		api._display_pump_timing = null
	return OK


# Pumps one runtime selected by a flat entity handle.
func _pump_entity(entity: RID, timing: NetwDisplayTiming) -> Error:
	var runtime := _runtime_for_entity(entity)
	if runtime == null:
		return ERR_DOES_NOT_EXIST
	if timing == null:
		return ERR_UNCONFIGURED
	_pump_runtime(runtime, timing, _stats)
	return OK


# Returns the most recent pump's aggregate counters for the api stat surface.
func _stats_snapshot() -> Dictionary:
	return {
		&"runtimes": _stats.runtimes,
		&"starving": _stats.starving,
		&"sleeping": _stats.sleeping,
		&"projecting": _stats.projecting,
		&"snaps": _stats.snaps,
		&"max_display_lag": int(_stats.max_display_lag),
		&"max_forecast_age": int(_stats.max_forecast_age),
	}


func _pump_runtime(
		runtime: NetwDisplayRuntime,
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
	# Counted past the disabled return, so a pump that ran reads differently from
	# one that was entered and declined.
	match runtime.pump_mode:
		_PUMP_DISABLED:
			return
		_PUMP_CHASE:
			runtime.pumped += 1
			stats.runtimes += 1
			_pump_chase(runtime, timing, stats)
		_:
			runtime.pumped += 1
			stats.runtimes += 1
			_pump_history(runtime, timing, stats)


# Drives the remote and bracketed roles. Both sample the same history through
# the playhead. Remote dilates the playhead, bracketed follows the raw tick.
func _pump_history(
		runtime: NetwDisplayRuntime,
		timing: NetwDisplayTiming,
		stats: NetwPumpStats,
) -> void:
	var playhead := runtime.playhead
	var trace := _should_trace(runtime)
	var forecast := false
	var max_forecast_ticks := 0
	var bracketed := runtime.pump_mode != _PUMP_REMOTE
	if not bracketed:
		var config := runtime.config
		forecast = config != null and config.timeline_mode == TimelineMode.FORECAST
		if config:
			max_forecast_ticks = config.max_forecast_ticks
		if config and config.enable_smart_dilation:
			_dilate_playhead(runtime, timing, stats, trace)
		else:
			playhead.display_lag = 0.0
	var factor := playhead.place(
		timing.display_tick,
		timing.tick_factor,
		bracketed,
	)
	var dt := playhead.display_tick
	stats.max_display_lag = maxf(stats.max_display_lag, playhead.display_lag)

	var eit := playhead.expected_interval_ticks
	var glide := _display_glide(runtime, timing.frame_delta)
	for state in runtime.states:
		# The bracketed pump runs on the peer that authors this stream (the server's
		# authority, or a predicted body in bracketed mode). A self-feeding channel
		# would sample its own simulation and write the smoothed value back onto it,
		# so leave the sim value. A REMOTE display writes it harmlessly.
		if state.self_feedback and runtime.pump_mode == _PUMP_BRACKETED:
			continue
		var verdict := state.history.pass_verdict(state.spec, forecast)
		if verdict == NetwDisplayHistory.PASS_SKIP_SLEEPING:
			stats.sleeping += 1
			continue
		if verdict == NetwDisplayHistory.PASS_SKIP_EMPTY:
			continue
		var project := verdict == NetwDisplayHistory.PASS_SAMPLE_PROJECT
		var velocity: Variant = null
		var has_velocity := false
		if project and state.spec and state.spec.project_channel != &"":
			var sibling_channel := runtime.tracks.by_key(
				state.spec.project_channel,
			)
			var sibling: NetwDisplayChannel = \
					runtime.states[sibling_channel] if sibling_channel >= 0 \
					else null
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
		result = state.offset.apply(
			result,
			glide,
			runtime.display_offset_limit,
			state.last_written,
			state.spec.mode if state.spec else NetwInterpolate.MODE_LERP,
		)
		if state.history.has_projected():
			stats.projecting += 1
			stats.max_forecast_age = maxf(
				stats.max_forecast_age,
				state.history.get_project_age(),
			)
		if state.history.sleeping:
			stats.sleeping += 1
		var weight: float = state.spec.smoothing_weight(timing.frame_delta) \
				if state.spec else 1.0
		result = state.history.smooth_toward(state.last_written, result, weight)
		if state.history.has_snapped():
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
		runtime: NetwDisplayRuntime,
) -> void:
	if runtime.pump_mode != _PUMP_CHASE:
		return
	if teleported:
		for state in runtime.states:
			state.offset.clear()
		return
	var limit := _chase_clamp(runtime)
	runtime.display_offset_limit = limit
	for state in runtime.states:
		if state.self_feedback:
			continue
		if not deltas.has(state.source_prop):
			continue
		state.offset.absorb(deltas[state.source_prop], limit)


# The largest render offset a chase absorption may hold, the entity's own
# teleport tier: an offset past it would show a pose a teleport was entitled
# to snap through.
func _chase_clamp(runtime: NetwDisplayRuntime) -> float:
	var entity := runtime.entity()
	if entity and entity.prediction:
		return maxf(entity.prediction.teleport_threshold, 0.0)
	return INF


# Returns the shared exponential decay for recovery and role offsets.
static func _display_glide(runtime: NetwDisplayRuntime, frame_delta: float) -> float:
	if not runtime.config:
		return 1.0
	return exp(
		-frame_delta / maxf(runtime.config.chase_glide_time, 0.001),
	)


# Eases each visual toward its live predicted body every frame, carrying any
# chase-absorbed recovery offset as it decays.
func _pump_chase(
		runtime: NetwDisplayRuntime,
		timing: NetwDisplayTiming,
		_stats: NetwPumpStats,
) -> void:
	var weight := 1.0 - exp(
		-timing.frame_delta / _predicted_effective_smooth_time(runtime, timing),
	)
	var glide := _display_glide(runtime, timing.frame_delta)
	var trace := _should_trace(runtime)
	for state in runtime.states:
		# A self-feeding channel would smooth the live body against its own last
		# output and write the drag back onto the simulation. Leave the sim value.
		if state.self_feedback:
			continue
		if not is_instance_valid(state.source_obj):
			continue
		var value: Variant = state.source_obj.get(state.source_prop)
		value = state.offset.apply(
			value,
			glide,
			runtime.display_offset_limit,
			state.last_written,
			state.spec.mode if state.spec else NetwInterpolate.MODE_LERP,
		)
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
		runtime: NetwDisplayRuntime,
		timing: NetwDisplayTiming,
		stats: NetwPumpStats,
		trace: bool,
) -> void:
	var playhead := runtime.playhead
	var effective_dt := playhead.effective_tick(timing.display_tick)
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
		for state in runtime.states:
			state.history.sleeping = false

	playhead.dilate(
		runtime.config,
		timing.frame_ticks,
		timing.display_offset,
		timing.recommended_display_offset,
		is_starving,
	)

	if trace:
		_dbg.trace(
			"Interpolation dilation dt=%d newest=%d starving=%s lag=%.2f",
			[effective_dt, newest_tick, str(is_starving), playhead.display_lag],
		)


# Captures the timing snapshot for a shell-phase event (reset, rebuild) that
# needs the lag floor between pumps. The pump path never calls this; it receives
# its snapshot by value from the shell entry instead.
func _capture_timing() -> NetwDisplayTiming:
	return NetwDisplayTiming.capture(_get_clock(), 0.0)


func _reset_runtime(runtime: NetwDisplayRuntime) -> void:
	var timing := _capture_timing()
	runtime.playhead.settle(
		runtime.config,
		timing.display_offset,
		timing.recommended_display_offset,
	)
	for state in runtime.states:
		state.history.clear()
		state.offset.clear()
		state.last_written = _current_source_value(state)
		state.output.write(state.last_written)


func _current_source_value(state: NetwDisplayChannel) -> Variant:
	if is_instance_valid(state.source_obj):
		return state.source_obj.get(state.source_prop)
	if is_instance_valid(state.target_obj):
		return state.target_obj.get(state.target_prop)
	return null


func _snap_state(
		runtime: NetwDisplayRuntime,
		property: StringName,
		value: Variant,
) -> void:
	var state := _named_state(runtime, property)
	if state:
		state.snap(value)


# Returns one flat entity track's last output.
func _named_state(runtime: NetwDisplayRuntime, property: StringName) -> NetwDisplayChannel:
	if runtime.tracks.is_ambiguous(property):
		_dbg.warn(
			"interpolation: ambiguous name lookup for '%s'",
			[property],
		)
	var channel := runtime.tracks.by_name(property)
	return runtime.states[channel] if channel >= 0 else null


func _should_trace(runtime: NetwDisplayRuntime) -> bool:
	var config := runtime.config
	if not config or config.trace_interval <= 0:
		return false
	runtime.trace_frame = (runtime.trace_frame + 1) % config.trace_interval
	return runtime.trace_frame == 0


# The chase smoothing time, from the handle override or a timing-derived
# default of most of one tick.
func _predicted_effective_smooth_time(
		runtime: NetwDisplayRuntime,
		timing: NetwDisplayTiming,
) -> float:
	var config := runtime.config
	if config and config.predicted_smooth_time > 0.0:
		return config.predicted_smooth_time
	if timing.ticktime > 0.0:
		return maxf(timing.ticktime * 0.85, 0.001)
	return 1.0 / 60.0

#endregion

#region Sync interval and authorship (role resolution reads the sync shape)

func _compute_sync_intervals(runtime: NetwDisplayRuntime) -> void:
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
		runtime: NetwDisplayRuntime,
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
#region Runtime data

#endregion

#region Display engine

# Writes one smoothed value through two surfaces in order: the api's display
# door, which owns the RenderingServer and Callable lanes, then the node port,
# which owns the property write and the host-frame conversion.
class _Output:
	extends RefCounted

	var api_ref: WeakRef
	var entity: RID
	var track: StringName
	var port := NetwDisplayPort.new()
	var _refused_global := false


	# The node writer runs on the shell: it sets Node properties and reads parent
	# transforms for global-space output, so a runtime bound to it pumps on the
	# main thread. A future RenderingServer writer returns ANY so swarm display
	# can sample and emit off the main thread.
	# TODO: add a RenderingServer writer backend (thread_class ANY) once entities
	# can bind canvas-item or instance RIDs as display views, so swarm display
	# never touches the SceneTree.
	func thread_class() -> StringName:
		return &"SHELL"


	func write(value: Variant) -> void:
		var api := api_ref.get_ref() as NetwMultiplayer if api_ref else null
		if api:
			var verdict := api._display_write(entity, track, value)
			api.report_event(
				NetwMultiplayerCore.DISPLAY_WRITE,
				api._native_core.liveness_core.route_of(entity),
				{ track = track },
				0,
				&"",
				{ },
				verdict,
			)
			if verdict == OK:
				return
			if verdict != ERR_DOES_NOT_EXIST:
				return
		if port.write(value) == NetwDisplayPort.WRITE_REFUSED:
			_refuse_global(value)


	# A channel the port has no global setter for falls back to a LOCAL write,
	# which composes with the body and is the drag the global-space contract
	# promised to escape. Say so once rather than degrade silently.
	func _refuse_global(value: Variant) -> void:
		if _refused_global:
			return
		_refused_global = true
		Netw.dbg.warn(
			"NetwDisplay: '%s' is a global-space channel with no global setter "
			+ "on %s, so it is written locally and composes with the body",
			[port.get_target_prop(), type_string(typeof(value))],
			func(m): push_warning(m),
		)

#endregion
