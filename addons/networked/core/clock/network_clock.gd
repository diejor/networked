## Tick-based network clock with RTT measurement and server-clock calibration.
##
## Add as a child of [MultiplayerTree] and assign it to [member MultiplayerTree.clock].
## The tree will call [method _on_tree_configured] after the multiplayer API is ready,
## which registers this clock so [method for_node] can locate it from anywhere in the subtree.
##
## [b]Tick loop[/b]: each physics frame the accumulator drains [member ticktime]-sized slices.
## For each slice the signals [signal before_tick] → [signal on_tick] → [signal after_tick]
## fire in order, then [member tick] increments. After the loop [member tick_factor] holds the
## fractional position within the current tick (useful for interpolation).
##
## [b]Clock synchronisation[/b]: clients ping the host after the tickrate handshake and then
## every [constant PING_INTERVAL] seconds. RTT samples are averaged over a rolling window to
## derive stable [member rtt_avg], [member rtt_jitter], and [member recommended_display_offset].
class_name NetworkClock
extends Node


#region ── Signals ─────────────────────────────────────────────────────────────

## Fires at the start of each tick, before game logic.
signal before_tick(delta: float, tick: int)
## Fires during each tick — connect game logic here.
signal on_tick(delta: float, tick: int)
## Fires at the end of each tick, after game logic.
signal after_tick(delta: float, tick: int)
## Fires before the tick loop each physics frame.
signal before_tick_loop()
## Fires after the tick loop each physics frame.
signal after_tick_loop()
## Fires once when the multiplayer API and clock are configured.
signal configured
## Fires once when the client successfully synchronises its clock to the server.
signal clock_synchronized()
## Fires when a connecting peer's tickrate does not match ours.
## Only emitted when [member tickrate_mismatch_action] is [code]2[/code] (Signal).
signal tickrate_mismatch(peer_id: int, their_tickrate: int)
## Fires when [member recommended_display_offset] exceeds [member display_offset] after
## synchronisation. Re-fires if conditions worsen after previously recovering.
## [codeblock]
## clock.display_offset_insufficient.connect(func(rec):
##     push_warning("display_offset too low — recommended: %d" % rec)
## )
## [/codeblock]
signal display_offset_insufficient(recommended: int)

#endregion


#region ── Configuration ───────────────────────────────────────────────────────

## How many ticks per second the simulation runs.
@export var tickrate: int = 30

## How many ticks behind the server tick the local display lags, used by [TickInterpolator].
## [br][br]Set to [code]0[/code] if you are not using [TickInterpolator].
## At runtime, check [member recommended_display_offset] to verify this value is adequate
## for current network conditions.
@export var display_offset: int = 2

## Clock calibration strategy when local and server ticks diverge.
## [b]Snap[/b]: jump immediately. [b]Stretch[/b]: nudge the accumulator gradually.
@export_enum("Snap", "Stretch") var sync_mode: int = 0

## Safety cap on ticks per frame to prevent spiral-of-death on slow frames.
@export var max_ticks_per_frame: int = 8

## What to do when a connecting peer's tickrate differs from ours.
@export_enum("Warn", "Disconnect", "Signal") var tickrate_mismatch_action: int = 0

#endregion


#region ── Public API ──────────────────────────────────────────────────────────

## Current server-calibrated simulation tick.
var tick: int = 0

## Duration of one tick in seconds.
var ticktime: float:
	get: return 1.0 / float(tickrate)

## Fractional position within the current tick [0, 1). Used by [TickInterpolator].
var tick_factor: float = 0.0

## Tick used for display; lags [member tick] by [member display_offset].
var display_tick: int:
	get: return maxi(0, tick - display_offset)

## Latest single-sample round-trip time in seconds.
var rtt: float = 0.0

## Smoothed RTT averaged over the last [constant PING_INTERVAL] × [constant _RTT_SAMPLE_WINDOW]
## seconds. More stable than [member rtt] for making calibration decisions.
var rtt_avg: float = 0.0

## Mean absolute deviation of RTT samples. Higher values indicate a less stable connection.
var rtt_jitter: float = 0.0

## Estimated one-way network latency in seconds, derived from [member rtt_avg].
var one_way_latency: float:
	get: return rtt_avg * 0.5

## Minimum [member display_offset] recommended for the current network conditions,
## based on [member one_way_latency] and [member rtt_jitter].
## Returns [member display_offset] until the clock is synchronised.
## [br][br]Listen to [signal display_offset_insufficient] to react when this exceeds
## [member display_offset] at runtime.
var recommended_display_offset: int:
	get:
		if not is_synchronized:
			return display_offset
		return ceili((one_way_latency + rtt_jitter) * tickrate)

## [code]true[/code] after the first successful clock calibration from the server.
var is_synchronized: bool = false


## Returns the [NetworkClock] registered on [param node]'s [SceneMultiplayer] API,
## or [code]null[/code] if none is registered.
static func for_node(node: Node) -> NetworkClock:
	var api := node.multiplayer as SceneMultiplayer
	if not api or not api.has_meta(&"_network_clock"):
		return null
	return api.get_meta(&"_network_clock") as NetworkClock

#endregion


#region ── Internal State ──────────────────────────────────────────────────────

## Seconds between client ping RPCs for clock drift correction.
const PING_INTERVAL: float = 1.0
const _RTT_SAMPLE_WINDOW := 8

var _tick_accumulator: float = 0.0
var _ping_timer: float = 0.0
var _rtt_samples: Array[float] = []
var _display_offset_insufficient: bool = false

#endregion


#region ── Lifecycle ───────────────────────────────────────────────────────────

func _init() -> void:
	configured.connect(_on_tree_configured)


func _physics_process(delta: float) -> void:
	if Engine.is_editor_hint():
		return
	if not multiplayer or not multiplayer.has_multiplayer_peer():
		return

	tick_factor = 0.0
	before_tick_loop.emit()

	_tick_accumulator += delta
	var ticks_this_frame := 0
	while _tick_accumulator >= ticktime and ticks_this_frame < max_ticks_per_frame:
		_tick_accumulator -= ticktime
		before_tick.emit(ticktime, tick)
		on_tick.emit(ticktime, tick)
		after_tick.emit(ticktime, tick)
		tick += 1
		ticks_this_frame += 1

	tick_factor = _tick_accumulator / ticktime
	after_tick_loop.emit()

	if not multiplayer.is_server() and is_synchronized:
		_ping_timer += delta
		if _ping_timer >= PING_INTERVAL:
			_ping_timer = 0.0
			_ping.rpc_id(1, Time.get_ticks_usec())


func _on_tree_configured() -> void:
	var api := multiplayer as SceneMultiplayer
	assert(api, "NetworkClock._on_tree_configured: multiplayer is not SceneMultiplayer. " +
		"Ensure MultiplayerTree._config_api uses get_path() as the multiplayer root.")
	api.set_meta(&"_network_clock", self)

	if not multiplayer.is_server():
		_request_handshake.rpc_id(1)

#endregion


#region ── Tickrate handshake ──────────────────────────────────────────────────

@rpc("any_peer", "call_remote", "reliable")
func _request_handshake() -> void:
	_respond_handshake.rpc_id(multiplayer.get_remote_sender_id(), tickrate)


@rpc("authority", "call_remote", "reliable")
func _respond_handshake(server_tickrate: int) -> void:
	if server_tickrate != tickrate:
		match tickrate_mismatch_action:
			0: push_warning("NetworkClock: tickrate mismatch — local=%d server=%d" % [tickrate, server_tickrate])
			1:
				multiplayer.multiplayer_peer.close()
				return
			2: tickrate_mismatch.emit(multiplayer.get_remote_sender_id(), server_tickrate)
	_ping.rpc_id(1, Time.get_ticks_usec())

#endregion


#region ── RTT ping / pong ─────────────────────────────────────────────────────

@rpc("any_peer", "call_remote", "unreliable")
func _ping(client_usec: int) -> void:
	_pong.rpc_id(multiplayer.get_remote_sender_id(), client_usec, tick)


@rpc("authority", "call_remote", "unreliable")
func _pong(client_usec: int, server_tick_at_pong: int) -> void:
	rtt = (Time.get_ticks_usec() - client_usec) / 1_000_000.0
	_record_rtt_sample(rtt)
	var half_rtt_ticks := int(ceil(rtt_avg * 0.5 / ticktime))
	_calibrate(server_tick_at_pong + half_rtt_ticks)
	_notify_display_offset()

#endregion


#region ── Clock calibration ───────────────────────────────────────────────────

func _calibrate(target_tick: int) -> void:
	var diff := target_tick - tick
	if abs(diff) > 3 or sync_mode == 0:
		tick = target_tick
	else:
		_tick_accumulator += diff * ticktime * 0.1

	if not is_synchronized:
		is_synchronized = true
		clock_synchronized.emit()


func _record_rtt_sample(sample: float) -> void:
	_rtt_samples.append(sample)
	if _rtt_samples.size() > _RTT_SAMPLE_WINDOW:
		_rtt_samples.pop_front()

	var sum := 0.0
	for s in _rtt_samples:
		sum += s
	rtt_avg = sum / _rtt_samples.size()

	var deviation := 0.0
	for s in _rtt_samples:
		deviation += abs(s - rtt_avg)
	rtt_jitter = deviation / _rtt_samples.size()


func _notify_display_offset() -> void:
	var insufficient := recommended_display_offset > display_offset
	if insufficient and not _display_offset_insufficient:
		_display_offset_insufficient = true
		display_offset_insufficient.emit(recommended_display_offset)
	elif not insufficient and _display_offset_insufficient:
		_display_offset_insufficient = false

#endregion
