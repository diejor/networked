## Entity wide display settings, owned by [member NetwEntity.interpolation].
##
## A [NetwInterpolate] spec configures one value stream. Everything that applies
## to the whole entity instead of one stream lives here, so there is one answer
## per entity to which node receives the smoothed write, which timeline it
## renders on, and how far behind the newest snapshot the playhead sits.
## [DisplayCore] pumps what this declares.
## [br][br]Every setting below is a view, not a store. A write goes out through
## [method NetwMultiplayer.display_set_param] and a read comes back through
## [method NetwMultiplayer.display_get_param], so the two spellings can never
## disagree about one entity. A setting authored before the entity has a live
## [member NetwEntity.rid] waits until it does, and is re-applied to each of
## the entity's lives, because liveness hands a re-admitted entity a fresh
## [member NetwEntity.rid] and the settings have to outlive it.
## [codeblock]
## var handle := NetwEntity.of(self).interpolation
## handle.visual_root = NodePath("Visual")
## handle.display_role = NetwDisplayHandle.DisplayRole.REMOTE
## [/codeblock]
class_name NetwDisplayHandle
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

# The flat verb selector every setting below reads and writes itself through.
const _Param := NetwMultiplayer.DisplayParam

## Visual child that receives smoothed output. A parented visual takes
## global-space writes for the [constant GLOBAL_SPACE_CHANNELS] while
## every other channel inherits from the body. A visual with
## [member CanvasItem.top_level] set takes absolute writes on any channel.
var visual_root: NodePath:
	get:
		return _read(_Param.DISPLAY_PARAM_VISUAL_ROOT, NodePath(""))
	set(value):
		_write(_Param.DISPLAY_PARAM_VISUAL_ROOT, value)

## Display strategy override for this entity.
var display_role: DisplayRole:
	get:
		return _read(_Param.DISPLAY_PARAM_ROLE, DisplayRole.AUTO)
	set(value):
		_write(_Param.DISPLAY_PARAM_ROLE, value)

## Predicted display filter used for local prediction.
var predicted_mode: PredictedMode:
	get:
		return _read(_Param.DISPLAY_PARAM_PREDICTED_MODE, PredictedMode.CHASE)
	set(value):
		_write(_Param.DISPLAY_PARAM_PREDICTED_MODE, value)

## Exponential smoothing time for [constant PredictedMode.CHASE].
var predicted_smooth_time: float:
	get:
		return _read(_Param.DISPLAY_PARAM_PREDICTED_SMOOTH_TIME, 0.0)
	set(value):
		_write(_Param.DISPLAY_PARAM_PREDICTED_SMOOTH_TIME, value)

## Seconds a recovery or display role offset takes to decay by
## [code]1/e[/code]. Role changes retain the last displayed value and glide
## onto the first target supplied by the new source.
##
## Under [constant PredictedMode.CHASE] each sub-teleport recovery's pose
## change is absorbed as a decaying render offset, so the visual stays
## where it was and glides onto the corrected body instead of jumping with
## it. Each recovery resets its channel's offset rather than accumulating
## into it, the offset is clamped to the entity's
## [member NetwPredictionHandle.teleport_threshold],
## and a teleported recovery clears every offset, because a genuine desync
## should be seen to snap. Those three rules are what keep a correction
## train from winding the visual away from the body.
var chase_glide_time: float:
	get:
		return _read(_Param.DISPLAY_PARAM_CHASE_GLIDE_TIME, 0.15)
	set(value):
		_write(_Param.DISPLAY_PARAM_CHASE_GLIDE_TIME, value)

## Enables display lag adaptation for remote interpolation.
var enable_smart_dilation: bool:
	get:
		return _read(_Param.DISPLAY_PARAM_SMART_DILATION, true)
	set(value):
		_write(_Param.DISPLAY_PARAM_SMART_DILATION, value)

## Where on the timeline remote display renders. [constant TimelineMode.FORECAST]
## targets the newest sample and projects each [NetwInterpolate] channel across
## the snapshot gaps. Only [constant DisplayRole.REMOTE] entities forecast, so
## an authored or predicted display ignores this.
var timeline_mode: TimelineMode:
	get:
		return _read(_Param.DISPLAY_PARAM_TIMELINE_MODE, TimelineMode.BUFFERED)
	set(value):
		_write(_Param.DISPLAY_PARAM_TIMELINE_MODE, value)

## Ticks the display may project past its newest sample under
## [constant TimelineMode.FORECAST]. The projection age clamps here, so a
## stalled stream freezes at the cap instead of drifting away.
var max_forecast_ticks: int:
	get:
		return _read(_Param.DISPLAY_PARAM_MAX_FORECAST_TICKS, 6)
	set(value):
		_write(_Param.DISPLAY_PARAM_MAX_FORECAST_TICKS, value)

## Maximum extra ticks that display lag can grow while starving.
var max_extra_dilation: float:
	get:
		return _read(_Param.DISPLAY_PARAM_MAX_EXTRA_DILATION, 0.0)
	set(value):
		_write(_Param.DISPLAY_PARAM_MAX_EXTRA_DILATION, value)

## Per frame fraction used to track the measured lag floor.
var lag_adapt_rate: float:
	get:
		return _read(_Param.DISPLAY_PARAM_LAG_ADAPT_RATE, 0.05)
	set(value):
		_write(_Param.DISPLAY_PARAM_LAG_ADAPT_RATE, value)

## Ticks per frame added after starvation is sustained.
var starvation_growth: float:
	get:
		return _read(_Param.DISPLAY_PARAM_STARVATION_GROWTH, 0.95)
	set(value):
		_write(_Param.DISPLAY_PARAM_STARVATION_GROWTH, value)

## Per frame fraction used to low pass the lag floor.
var floor_smoothing: float:
	get:
		return _read(_Param.DISPLAY_PARAM_FLOOR_SMOOTHING, 0.05)
	set(value):
		_write(_Param.DISPLAY_PARAM_FLOOR_SMOOTHING, value)

## Starving frames tolerated before lag starts growing.
var starvation_grace_frames: int:
	get:
		return _read(_Param.DISPLAY_PARAM_STARVATION_GRACE_FRAMES, 3)
	set(value):
		_write(_Param.DISPLAY_PARAM_STARVATION_GRACE_FRAMES, value)

## Frames between interpolation trace logs. [code]0[/code] disables logs.
var trace_interval: int:
	get:
		return _read(_Param.DISPLAY_PARAM_TRACE_INTERVAL, 0)
	set(value):
		_write(_Param.DISPLAY_PARAM_TRACE_INTERVAL, value)

## Read-only extra display delay in ticks measured by smart dilation.
var display_lag: float:
	get:
		var api := _flat_api()
		if api:
			var value := api.display_get_track_stat(
				_entity_rid(),
				&"",
				&"display_lag",
			)
			return float(value) if value != null else 0.0
		var runtime := _runtime()
		return runtime.playhead.display_lag if runtime else 0.0

## The [enum DisplayRole] this entity is displayed by right now, with
## [constant DisplayRole.AUTO] already resolved.
##
## [member display_role] is the request and this is the answer, so a display
## that is not behaving is read here rather than by re-deriving the rule.
## Reports [constant DisplayRole.AUTO] before the first resolve.
var resolved_display_role: DisplayRole:
	get:
		var runtime := _runtime()
		return runtime.role if runtime else DisplayRole.AUTO


## Diagnostic shape of this entity's channel table: how many channels the pump
## iterates, and how many display names more than one of them claims.
##
## A name two channels claim is served to a reader by whichever was indexed
## first while the pump writes them all, so a display that disagrees with what
## this interface reports is read here before anything else.
func channel_census() -> Dictionary:
	var api := _flat_api()
	if api:
		return {
			&"channels": api.display_get_track_stat(
				_entity_rid(),
				&"",
				&"channels",
			),
			&"ambiguous": api.display_get_track_stat(
				_entity_rid(),
				&"",
				&"ambiguous",
			),
		}
	var runtime := _runtime()
	if not runtime:
		return { &"channels": 0, &"ambiguous": 0 }
	return {
		&"channels": runtime.states.size(),
		&"ambiguous": runtime.ambiguous_targets.size(),
	}

## Read-only count of pump passes this entity's display has taken.
##
## A display standing still while this stands still is a pump that never ran,
## which is a different fault from a pump running and writing the same value.
## A role resolved to [constant DisplayRole.DISABLED] writes nothing and is
## not counted, so the two faults stay distinguishable here.
var pumped_frames: int:
	get:
		var api := _flat_api()
		if api:
			var value := api.display_get_track_stat(
				_entity_rid(),
				&"",
				&"pumped_frames",
			)
			return int(value) if value != null else 0
		var runtime := _runtime()
		return runtime.pumped if runtime else 0

## Read-only count of consecutive frames starved for fresh snapshots.
var starvation_ticks: int:
	get:
		var api := _flat_api()
		if api:
			var value := api.display_get_track_stat(
				_entity_rid(),
				&"",
				&"starvation_ticks",
			)
			return int(value) if value != null else 0
		var runtime := _runtime()
		return runtime.playhead.starvation_ticks if runtime else 0

var _entity_ref: WeakRef
# Settings authored on this handle, kept so they can be re-applied to the
# record the core builds for each of the entity's lives.
var _authored: Dictionary[int, Variant] = { }


## Snaps [param property] to [param value] and clears its history.
func snap_property(property: StringName, value: Variant) -> void:
	var api := _flat_api()
	if api:
		api.display_snap(_entity_rid(), property, value)
		return
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


## Returns whether [param property]'s channel has parked itself on its newest
## value, so the pump is deliberately writing nothing for it.
##
## A history sleeps once the playhead reaches the newest recorded tick and the
## display already shows it. A channel asleep while its source is visibly
## moving is the pump declining to write, which is what a pinned display looks
## like from here.
func is_sleeping(property: StringName) -> bool:
	var api := _flat_api()
	if api:
		return bool(
			api.display_get_track_stat(
				_entity_rid(),
				property,
				&"sleeping",
			),
		)
	var iface := _interface()
	var runtime := _runtime()
	if not iface or not runtime:
		return false
	var state := iface._named_state(runtime, property)
	return state.history.sleeping if state else false


## Returns the value the pump last wrote for [param property], or
## [code]null[/code] when no channel writes that name.
##
## This is what the pump decided, before whatever the game does with it. A
## value here that advances while the visual stands still puts the fault
## after the pump; one that stands still too puts it inside.
func displayed_value(property: StringName) -> Variant:
	var api := _flat_api()
	if api:
		return api.display_get_value(_entity_rid(), property)
	var iface := _interface()
	var runtime := _runtime()
	if not iface or not runtime:
		return null
	var state := iface._named_state(runtime, property)
	return state.last_written if state else null


## Returns the displayed STATE authoring tick, or [code]-1[/code].
func displayed_authoring_tick() -> int:
	var api := _flat_api()
	if api:
		return api.display_get_tick(_entity_rid())
	var iface := _interface()
	var runtime := _runtime()
	if not iface or not runtime:
		return -1
	return iface._displayed_authoring_tick(runtime)


## Returns the [NetwRingBuffer] for [param property], or [code]null[/code].
func get_buffer(property: StringName) -> NetwRingBuffer:
	var api := _flat_api()
	if api:
		return api.display_get_track_stat(
			_entity_rid(),
			property,
			&"buffer",
		) as NetwRingBuffer
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


func _interface() -> DisplayCore:
	var ent := entity()
	if not ent or not ent.owner:
		return null
	return DisplayCore.for_node(ent.owner)


func _flat_api() -> NetwMultiplayer:
	var ent := entity()
	if not ent or not ent.owner:
		return null
	return ent.owner.multiplayer as NetwMultiplayer


func _entity_rid() -> RID:
	var ent := entity()
	return ent.rid if ent else RID()


func _runtime() -> DisplayCore._Runtime:
	var iface := _interface()
	return iface._runtime_for_handle(self) if iface else null


# Writes one setting through the flat verb, or buffers it when the entity has
# no live RID to address yet.
func _write(param: int, value: Variant) -> void:
	var api := _flat_api()
	var rid := _entity_rid()
	if api and rid.is_valid():
		api.display_set_param(rid, param, value)
		return
	_record(param, value)


# Journals one setting for replay. The core calls this for every write that
# reaches it, whatever spelling made the write, so a setting outlives the
# record it landed on.
func _record(param: int, value: Variant) -> void:
	_authored[param] = value


# Reads one setting from the core, falling back to the authored buffer while
# the entity has no live RID.
func _read(param: int, fallback: Variant) -> Variant:
	var api := _flat_api()
	var rid := _entity_rid()
	if api and rid.is_valid():
		var value: Variant = api.display_get_param(rid, param)
		if value != null:
			return value
	return _authored.get(param, fallback)


# Drains the authored buffer onto a config record the core has just created.
# Called once per entity life, because liveness hands a re-admitted entity a
# fresh RID and with it a fresh record.
func _author_into(config: DisplayCore._Config) -> void:
	for param: int in _authored:
		DisplayCore._apply_config_param(config, param, _authored[param])
