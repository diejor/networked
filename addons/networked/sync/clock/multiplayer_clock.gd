## Synchronises simulation time between server and clients with drift and stall protection.
##
## The [MultiplayerClock] provides a stable, tick-based time source required for deterministic 
## simulation and smooth visual interpolation. It handles RTT smoothing, clock drift 
## correction, and frame stall detection to prevent "spiral of death" scenarios.
## [br][br]
## [b]Usage:[/b]
## [codeblock]
## # The clock registers itself automatically on the MultiplayerTree.
## # Access it from any node via:
## var clock = MultiplayerClock.for_node(self)
##
## # Connect to the simulation loop:
## clock.on_tick.connect(func(delta, tick):
##     _simulate_physics(tick)
## )
## [/codeblock]
@icon("res://addons/networked/assets/MultiplayerClock.svg")
@tool
class_name MultiplayerClock
extends Node

#region ── Signals ─────────────────────────────────────────────────────────────

## Fires at the start of each simulation tick, before game logic.
signal before_tick(delta: float, tick: int)

## Fires during each simulation tick. Primary simulation logic should connect
## here.
signal on_tick(delta: float, tick: int)

## Fires at the end of each simulation tick, after game logic.
signal after_tick(delta: float, tick: int)

## Fires once before the tick loop runs each physics frame.
signal before_tick_loop()

## Fires once after the tick loop finishes each physics frame.
signal after_tick_loop()

## Fires when the multiplayer API and clock registration are complete.
signal configured

## Fires when the client successfully synchronises its clock with the server.
signal clock_synchronized()

## Fires when a connecting peer's tickrate does not match the local tickrate.
signal tickrate_mismatch(peer_id: int, their_tickrate: int)

## Fires when the current [member display_offset] is lower than
## [member recommended_display_offset].
signal display_offset_insufficient(recommended: int)

## Fires when the network stability status changes based on jitter.
signal stability_changed(is_stable: bool)

## Fires after each ping/pong cycle with fresh clock metrics for the debugger.
signal pong_received(data: Dictionary)

#endregion

#region ── Configuration ───────────────────────────────────────────────────────

@export_group("Simulation")
## How many simulation ticks to run per second.

@export_custom(0, "suffix:frames") var tickrate: int = 30

## Maximum simulation ticks allowed to run in a single physics frame.
@export_custom(0, "suffix:ticks") var max_ticks_per_frame: int = 8

## Frame delta threshold before resetting the accumulator.
@export_custom(0, "suffix:s") var stall_threshold: float = 1.0

@export var use_physics_interpolation: bool = true

@export_group("Calibration")
## Strategy used to align the local clock with the server.
enum SyncMode {
	## Hard-jump the local tick to the calibrated target on every sample.
	SNAP,
	## Nudge the tick accumulator toward the target. Falls back to a snap
	## when the divergence exceeds [member panic_snap_threshold].
	STRETCH,
}

@export var sync_mode: SyncMode = SyncMode.STRETCH

## The maximum allowed divergence before a hard Snap is forced.
@export_custom(0, "suffix:ticks") var panic_snap_threshold: int = 20

## Fraction of the remaining divergence the Stretch clock closes each frame.
@export_range(0.01, 0.5) var stretch_nudge_factor: float = 0.05

## How often the client pings the server to refresh RTT and recalibrate.
@export_custom(0, "suffix:s") var ping_interval: float = 0.1

@export_group("Network Buffering")
## The number of ticks the visual display lags behind the simulation.
@export_custom(0, "suffix:ticks") var display_offset: int = 2

## Scales jitter impact on the [member recommended_display_offset].
@export var jitter_multiplier: float = 2.0

## Number of recent RTT samples averaged for jitter and the recommendation.
@export_custom(0, "suffix:samples") var jitter_window: int = 16

## The threshold below which the connection is considered stable.
@export_custom(0, "suffix:s") var jitter_stability_threshold: float = 0.05

@export_group("Compatibility")
## Action to take when a connecting peer has a different tickrate.
@export_enum("Warn", "Disconnect", "Signal") var tickrate_mismatch_action: int = 0

@export_group("Debug & Tools")
## [b]Runtime Only:[/b] Runs a 5-second test to determine optimal
## [member display_offset].
@export var auto_configure_offset: bool:
	set(v):
		if v and is_inside_tree() and not Engine.is_editor_hint():
			_run_auto_config()

## Logs average clock drift over 60-second windows to the console.
@export var enable_drift_logging: bool = false

#endregion

#region ── Public API ──────────────────────────────────────────────────────────

## The current server-calibrated simulation tick.
var tick: int = 0

## The duration of a single simulation tick in seconds.
var ticktime: float:
	get:
		return 1.0 / float(tickrate)

## The fractional position [0, 1) within the current tick.
var tick_factor: float:
	set(v):
		_tick_factor_override = v
	get:
		if _tick_factor_override >= 0.0:
			return _tick_factor_override

		if Engine.is_editor_hint() or not is_inside_tree():
			return 0.0

		var phys_delta := get_physics_process_delta_time()
		var time_in_frame := 0.0

		if use_physics_interpolation and \
				Engine.has_method(&"get_physics_interpolation_fraction"):
			# Engine fraction (0->1) represents time since start of physics frame
			time_in_frame = Engine.get_physics_interpolation_fraction() * phys_delta
		else:
			# Fallback to wall-clock time since start of physics frame
			time_in_frame = (Time.get_ticks_usec() - _last_physics_time_usec) / 1_000_000.0

		# CRITICAL: Do NOT clamp to 1.0. 
		# If the render frame happens just before the next physics frame and 
		# timing is slightly off, the factor might be 1.01.
		# Clamping causes the playhead to stall, creating small jagged jumps.
		# MultiplayerInterpolator already handles factor > 1.0 by floor()ing it into dt.
		return (_tick_accumulator + time_in_frame) / ticktime

## The tick index used for visual display: [code]tick - display_offset[/code].
var display_tick: int:
	get:
		return maxi(0, tick - display_offset)

## Latest Round Trip Time measurement in seconds.
var rtt: float:
	get:
		return _stats.rtt

## Averaged Round Trip Time in seconds.
var rtt_avg: float:
	get:
		return _stats.avg

## Mean absolute deviation of RTT samples (jitter) in seconds.
var rtt_jitter: float:
	get:
		return _stats.jitter

## Estimated one-way network latency in seconds.
var one_way_latency: float:
	get:
		return _stats.avg * 0.5

## The [member display_offset] recommended for the current network conditions.
var recommended_display_offset: int:
	get:
		if not is_synchronized:
			return display_offset
		return ceili(
			(one_way_latency + rtt_jitter * jitter_multiplier) * tickrate,
		)

## Returns [code]true[/code] if the client has calibrated with the server.
var is_synchronized: bool = false

## Returns [code]true[/code] if jitter is below
## [member jitter_stability_threshold].
var is_stable: bool:
	get:
		return _stats.is_stable


## Locates the [MultiplayerClock] registered on the node's multiplayer API.
static func for_node(node: Node) -> MultiplayerClock:
	var api := node.multiplayer as SceneMultiplayer
	if api and api.has_meta(&"_multiplayer_clock"):
		return api.get_meta(&"_multiplayer_clock")
	return null

#endregion

#region ── Internal State ──────────────────────────────────────────────────────

const _DRIFT_LOG_INTERVAL := 60.0

var _tick_accumulator: float = 0.0
var _target_tick_estimate: float = 0.0
var _last_physics_time_usec: int = 0
var _tick_factor_override: float = -1.0
var _ping_timer: float = 0.0
var _stats := _NetworkStats.new()
var _display_offset_insufficient: bool = false
var _drift_samples: Array[int] = []
var _drift_timer: float = 0.0

#endregion

#region ── Lifecycle ───────────────────────────────────────────────────────────

func _enter_tree() -> void:
	if Engine.is_editor_hint():
		return

	var mt := NetwServices.register(self, MultiplayerClock)
	if not is_instance_valid(mt):
		return

	if not mt.session_entered.is_connected(_on_tree_configured):
		mt.session_entered.connect(_on_tree_configured)

	if not mt.session_entered.is_connected(configured.emit):
		mt.session_entered.connect(configured.emit)

	if mt.is_online():
		_on_tree_configured.call_deferred()


func _ready() -> void:
	pass


func _exit_tree() -> void:
	if Engine.is_editor_hint():
		return

	var mt := NetwServices.unregister(self, MultiplayerClock)
	if not is_instance_valid(mt):
		return

	if mt.session_entered.is_connected(_on_tree_configured):
		mt.session_entered.disconnect(_on_tree_configured)

	if mt.session_entered.is_connected(configured.emit):
		mt.session_entered.disconnect(configured.emit)


func _physics_process(delta: float) -> void:
	if Engine.is_editor_hint() or not multiplayer or \
			not multiplayer.has_multiplayer_peer():
		return

	if multiplayer.multiplayer_peer.get_connection_status() != \
			MultiplayerPeer.CONNECTION_CONNECTED:
		return

	_last_physics_time_usec = Time.get_ticks_usec()

	if delta > stall_threshold:
		_tick_accumulator = 0.0
		if not multiplayer.is_server():
			_request_handshake.rpc_id(1)

	before_tick_loop.emit()

	_tick_accumulator += delta

	# Drift the estimate forward with the server, then close the residual gap a
	# fraction at a time so the playhead never teleports on a pong.
	if is_synchronized and sync_mode == SyncMode.STRETCH:
		_target_tick_estimate += delta * tickrate
		_nudge_toward_estimate()

	var ticks_this_frame := 0
	while _tick_accumulator >= ticktime and \
			ticks_this_frame < max_ticks_per_frame:
		_tick_accumulator -= ticktime
		before_tick.emit(ticktime, tick)
		on_tick.emit(ticktime, tick)
		after_tick.emit(ticktime, tick)
		tick += 1
		ticks_this_frame += 1

	after_tick_loop.emit()

	if not multiplayer.is_server() and is_synchronized:
		_ping_timer += delta
		if _ping_timer >= ping_interval:
			_ping_timer = 0.0
			_ping.rpc_id(1, Time.get_ticks_usec())

		if enable_drift_logging:
			_drift_timer += delta
			if _drift_timer >= _DRIFT_LOG_INTERVAL:
				_log_drift()


func _on_tree_configured() -> void:
	var api := multiplayer as SceneMultiplayer
	if api:
		api.set_meta(&"_multiplayer_clock", self)
		if not api.server_disconnected.is_connected(_on_server_disconnect):
			api.server_disconnected.connect(_on_server_disconnect)
		if not api.connection_failed.is_connected(_on_server_disconnect):
			api.connection_failed.connect(_on_server_disconnect)

	if not multiplayer.is_server():
		if multiplayer.multiplayer_peer.get_connection_status() == \
				MultiplayerPeer.CONNECTION_CONNECTED:
			_request_handshake.rpc_id(1)
		else:
			var request_handshake := _request_handshake.rpc_id.bind(1)
			if multiplayer.connected_to_server.is_connected(
				request_handshake,
			):
				return
			multiplayer.connected_to_server.connect(
				request_handshake,
				CONNECT_ONE_SHOT,
			)


func _on_server_disconnect() -> void:
	is_synchronized = false

#endregion

#region ── Messaging ───────────────────────────────────────────────────────────

@rpc("any_peer", "call_remote", "reliable")
func _request_handshake() -> void:
	if not multiplayer.is_server():
		Netw.dbg.warn("_request_handshake received on non-server peer %d", [multiplayer.get_unique_id()])
		return
	_respond_handshake.rpc_id(multiplayer.get_remote_sender_id(), tickrate)


@rpc("authority", "call_remote", "reliable")
func _respond_handshake(server_tickrate: int) -> void:
	if server_tickrate != tickrate:
		match tickrate_mismatch_action:
			0:
				Netw.dbg.warn(
					"MultiplayerClock: tickrate mismatch — local=%d server=%d" % \
							[tickrate, server_tickrate],
					func(m): push_warning(m)
				)
			1:
				multiplayer.multiplayer_peer.close()
			2:
				tickrate_mismatch.emit(
					multiplayer.get_remote_sender_id(),
					server_tickrate,
				)
	_ping.rpc_id(1, Time.get_ticks_usec())


@rpc("any_peer", "call_remote", "unreliable")
func _ping(client_usec: int) -> void:
	if not multiplayer.is_server():
		return
	_pong.rpc_id(multiplayer.get_remote_sender_id(), client_usec, tick)


@rpc("authority", "call_remote", "unreliable")
func _pong(client_usec: int, server_tick_at_pong: int) -> void:
	var sample := (Time.get_ticks_usec() - client_usec) / 1_000_000.0
	var old_stable := _stats.is_stable

	_stats.record_sample(sample, jitter_stability_threshold, jitter_window)

	if _stats.is_stable != old_stable:
		stability_changed.emit(_stats.is_stable)

	var half_rtt_ticks := int(ceil(_stats.avg * 0.5 / ticktime))
	var target_tick := server_tick_at_pong + half_rtt_ticks
	var pre_calibrate_diff := target_tick - tick

	_calibrate(target_tick)
	_notify_display_offset()

	pong_received.emit(
		{
			"rtt_raw": sample,
			"rtt_avg": _stats.avg,
			"rtt_jitter": _stats.jitter,
			"diff": pre_calibrate_diff,
			"tick": tick,
			"display_offset": display_offset,
			"recommended_display_offset": recommended_display_offset,
			"is_stable": _stats.is_stable,
			"is_synchronized": is_synchronized,
		},
	)

#endregion

#region ── Internal Logic ──────────────────────────────────────────────────────

func _calibrate(target_tick: int) -> void:
	var diff := target_tick - tick

	if enable_drift_logging:
		_drift_samples.append(diff)

	if not is_synchronized:
		# First calibration hard-aligns so STRETCH begins already converged.
		tick = target_tick
		_tick_accumulator = 0.0
		_target_tick_estimate = float(target_tick)
		is_synchronized = true
		clock_synchronized.emit()
		return

	if sync_mode == SyncMode.SNAP:
		tick = target_tick
	else:
		# Re-anchor the estimate to the fresh measurement. _physics_process
		# nudges the live clock toward it every frame.
		_target_tick_estimate = float(target_tick)


func _nudge_toward_estimate() -> void:
	var current := float(tick) + _tick_accumulator / ticktime
	var divergence := _target_tick_estimate - current

	# A large gap is a real desync (stall, RTT spike). Snap rather than crawl,
	# matching the panic path SNAP mode relies on.
	if absf(divergence) > float(panic_snap_threshold):
		tick = int(round(_target_tick_estimate))
		_tick_accumulator = (_target_tick_estimate - float(tick)) * ticktime
		return

	_tick_accumulator += divergence * ticktime * stretch_nudge_factor


func _notify_display_offset() -> void:
	var insufficient := recommended_display_offset > display_offset
	if insufficient and not _display_offset_insufficient:
		_display_offset_insufficient = true
		display_offset_insufficient.emit(recommended_display_offset)
	elif not insufficient and _display_offset_insufficient:
		_display_offset_insufficient = false


func _log_drift() -> void:
	if _drift_samples.is_empty():
		return
	var sum := 0
	for s in _drift_samples:
		sum += s
	Netw.dbg.info(
		"MultiplayerClock: 60s average drift = %.2f ticks" % \
				[float(sum) / _drift_samples.size()],
	)
	_drift_samples.clear()
	_drift_timer = 0.0


func _run_auto_config() -> void:
	if multiplayer.is_server():
		return
	Netw.dbg.info("MultiplayerClock: Starting 5s auto-config test...")
	var max_rec := 0
	for i in range(50):
		await get_tree().create_timer(0.1).timeout
		if not is_instance_valid(self):
			return
		max_rec = maxi(max_rec, recommended_display_offset)
	display_offset = max_rec
	Netw.dbg.info(
		"MultiplayerClock: Auto-config complete. display_offset = %d" % \
				[max_rec],
	)

#endregion

#region ── Inner Classes ───────────────────────────────────────────────────────

class _NetworkStats:
	var rtt: float = 0.0
	var avg: float = 0.0
	var jitter: float = 0.0
	var is_stable: bool = true

	var _samples: Array[float] = []


	func record_sample(
			sample: float,
			stability_threshold: float,
			window_size: int,
	) -> void:
		rtt = sample
		_samples.append(sample)
		while _samples.size() > maxi(1, window_size):
			_samples.pop_front()

		var sum := 0.0
		for s in _samples:
			sum += s
		avg = sum / _samples.size()

		var deviation := 0.0
		for s in _samples:
			deviation += abs(s - avg)
		jitter = deviation / _samples.size()
		is_stable = jitter < stability_threshold

#endregion
