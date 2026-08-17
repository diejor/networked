## The session's clock, owned by [NetwMultiplayer] and reached through it.
##
## Provides a stable, tick-based time source required for deterministic
## simulation and smooth visual interpolation. The session always has one. It is
## constructed inert with the [NetwMultiplayer] that owns it and starts ticking
## only after [method NetwMultiplayer.service_install] receives a
## [NetwClockConfig], so [method is_configured] replaces null-checking as the
## absence story. The node remains one authoring surface and the physics pump.
##
## [br][br]
## The schedule itself is not here. [member core] decides it, because a tick is
## a fixed amount of simulated time and everything that follows from that is
## arithmetic over integers and durations that reaches no [Object]. What stays
## in this interface is everything the engine below it cannot see: the
## handshake and ping/pong protocol over the carrier, the node that authored the
## configuration, and the wall-clock cadence of the two loops that pump it.
## [codeblock]
## NetwClockCore   the tick loop, the simulation gate, the calibration
## ClockCore       the protocol, the node, the pump's measured cadence
## [/codeblock]
## Every engine signal is forwarded through the matching signal here, so a
## caller connects to the session's clock without having to know which side of
## that line ticks.
## [codeblock]
## var api := NetwMultiplayer.of(self)
## api.on_tick.connect(func(delta: float, tick: int) -> void:
##     _simulate_physics(tick)
## )
## var now := api.clock.tick
## [/codeblock]
class_name ClockCore
extends RefCounted

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

## Fires when the client successfully synchronises its clock with the server.
signal clock_synchronized()

## Fires when the current [member display_offset] is lower than
## [member recommended_display_offset].
signal display_offset_insufficient(recommended: int)

## Fires when the network stability status changes based on jitter.
signal stability_changed(is_stable: bool)

#endregion

#region ── Configuration ───────────────────────────────────────────────────────

## Strategy used to align the local clock with the server.
enum SyncMode {
	## Hard-jump the local tick to the calibrated target on every sample.
	SNAP,
	## Nudge the tick accumulator toward the target. Falls back to a snap
	## when the divergence exceeds [member panic_snap_threshold].
	STRETCH,
}

## How many simulation ticks to run per second.
var tickrate: int:
	set(v):
		core.tickrate = v
	get:
		return core.tickrate

## Maximum simulation ticks allowed to run in a single physics frame.
var max_ticks_per_frame: int:
	set(v):
		core.max_ticks_per_frame = v
	get:
		return core.max_ticks_per_frame

## Frame delta threshold before resetting the accumulator.
var stall_threshold: float:
	set(v):
		core.stall_threshold = v
	get:
		return core.stall_threshold

## Reads the engine's physics interpolation fraction for [member tick_factor]
## instead of the wall-clock span since the last step.
var use_physics_interpolation: bool:
	set(v):
		core.use_physics_interpolation = v
	get:
		return core.use_physics_interpolation

## Strategy used to align the local clock with the server, one of [enum SyncMode].
var sync_mode: SyncMode:
	set(v):
		core.sync_mode = int(v) as NetwClockCore.SyncMode
	get:
		return int(core.sync_mode) as SyncMode

## The maximum allowed divergence before a hard [constant SyncMode.SNAP] is
## forced.
var panic_snap_threshold: int:
	set(v):
		core.panic_snap_threshold = v
	get:
		return core.panic_snap_threshold

## Fraction of the remaining divergence the [constant SyncMode.STRETCH] clock
## closes each frame.
var stretch_nudge_factor: float:
	set(v):
		core.stretch_nudge_factor = v
	get:
		return core.stretch_nudge_factor

## How often the client pings the server to refresh RTT and recalibrate.
var ping_interval: float:
	set(v):
		core.ping_interval = v
	get:
		return core.ping_interval

## Ticks the client's clock deliberately leads the half-RTT estimate by, the
## margin that gets an input authored for server tick [code]T[/code] to the
## server before it consumes [code]T[/code].
##
## The calibration target is continuous ([method handle_pong] carries the
## server's intra-tick phase), so this lead is the whole margin and nothing is
## left to a rounding artifact. Too small and the server's consume cursor runs
## dry ([member NetwPredictStats.starved]);
## too large and every input waits that much longer to be simulated. Pair it
## with [member NetwPredictionHandle.consume_buffer_ticks],
## which absorbs the residual arrival-phase drift this lead does not cover.
##
## Ignored on the server, which has no flight time to its own authority and so
## never leads itself.
var lead_ticks: float:
	set(v):
		core.lead_ticks = v
	get:
		return core.lead_ticks

## The number of ticks the visual display lags behind the simulation.
var display_offset: int:
	set(v):
		core.display_offset = v
	get:
		return core.display_offset

## Scales jitter impact on the [member recommended_display_offset].
var jitter_multiplier: float:
	set(v):
		core.jitter_multiplier = v
	get:
		return core.jitter_multiplier

## Number of recent RTT samples averaged for jitter and the recommendation.
var jitter_window: int:
	set(v):
		core.jitter_window = v
	get:
		return core.jitter_window

## The threshold below which the connection is considered stable.
var jitter_stability_threshold: float:
	set(v):
		core.jitter_stability_threshold = v
	get:
		return core.jitter_stability_threshold

## Logs average clock drift over 60-second windows to the console.
var enable_drift_logging: bool = false

#endregion

#region ── Public API ──────────────────────────────────────────────────────────

## The record store this interface reads and writes.
##
## Held rather than mirrored: the tick schedule, the calibration and the
## simulation gate are arithmetic over integers and durations, so they outlive
## every node that authors them and reach no [Object] at all.
var core: NetwClockCore

## The current server-calibrated simulation tick.
var tick: int:
	set(v):
		core.tick = v
	get:
		return core.tick

## The duration of a single simulation tick in seconds.
var ticktime: float:
	get:
		return core.ticktime()

## Multiplier to scale velocities from physics rate to tick rate. Compensates
## for [method CharacterBody2D.move_and_slide] and
## [method CharacterBody3D.move_and_slide] ignoring tick delta.
var physics_factor: float:
	get:
		return core.physics_factor()

## Physics steps one tick is worth, [member physics_factor] as a whole number.
##
## The physics server runs exactly one step per frame, so a tick can be worth
## one step or two but never one and a fifth. A fractional
## [member physics_factor] is a declaration the engine cannot honour, and
## [LagCompCore] reports it against the entity that drives a
## solver body under it.
var physics_steps_per_tick: int:
	get:
		return core.physics_steps_per_tick()

## Whether this frame's simulated world may advance.
##
## A tick is a fixed amount of simulated time, so a frame that emits no tick
## must advance no physics or the two peers stop meaning the same thing by a
## transition. [LagCompCore] reads this after the tick loop and
## holds the predicted bodies' spaces on a frame that answers
## [code]false[/code]. It stays [code]true[/code] for a whole session unless a
## gate is armed, so a game that predicts nothing never sees a held frame.
var is_simulating: bool:
	get:
		return core.is_simulating()

## Frames the tick loop wanted more ticks than it was allowed to emit.
##
## Under a gate the loop emits at most one tick per frame, so a peer whose
## physics cannot sustain [member tickrate] times
## [member physics_steps_per_tick] steps per wall second cannot catch up and
## falls behind the authority it tracks. Any sustained growth here means this
## peer is too slow to predict, which no netcode setting repairs.
var simulation_behind_count: int:
	get:
		return core.simulation_behind_count()

## Where a display sits inside the current tick, unclamped, per
## [method NetwClockCore.tick_factor]. Assigning places it, and a negative
## assignment hands the reading back to the clock.
var tick_factor: float:
	set(v):
		core.tick_factor_override = v
	get:
		return core.tick_factor()

## The tick index used for visual display: [code]tick - display_offset[/code].
var display_tick: int:
	get:
		return core.display_tick()

## Latest Round Trip Time measurement in seconds.
var rtt: float:
	get:
		return core.rtt()

## Averaged Round Trip Time in seconds.
var rtt_avg: float:
	get:
		return core.rtt_avg()

## Mean absolute deviation of RTT samples (jitter) in seconds.
var rtt_jitter: float:
	get:
		return core.rtt_jitter()

## Estimated one-way network latency in seconds.
var one_way_latency: float:
	get:
		return core.one_way_latency()

## The [member display_offset] recommended for the current network conditions.
var recommended_display_offset: int:
	get:
		return core.recommended_display_offset()

## Returns [code]true[/code] if the client has calibrated with the server.
var is_synchronized: bool:
	set(v):
		core.synchronized = v
	get:
		return core.synchronized

## Test seam. When [code]true[/code], [method MultiplayerClock._physics_process]
## stops advancing the tick loop so a deterministic stepper owns ticking through
## [method force_step]. Enabling it marks the clock [member is_synchronized] so
## the real ping calibration is bypassed.
var manual_tick: bool = false:
	set(value):
		manual_tick = value
		if value:
			is_synchronized = true

## Returns [code]true[/code] if jitter is below
## [member jitter_stability_threshold].
var is_stable: bool:
	get:
		return core.is_stable()


## Applies [param config] to the engine and optionally binds [param node] as the
## protocol endpoint. Called by [NetwMultiplayer] when it installs a
## [NetwClockConfig].
func configure(node: MultiplayerClock, config: NetwClockConfig) -> void:
	if node:
		_node = node
	tickrate = config.tickrate
	max_ticks_per_frame = config.max_ticks_per_frame
	stall_threshold = config.stall_threshold
	use_physics_interpolation = config.use_physics_interpolation
	# The config authors this in the public enum, which mirrors SyncMode exactly.
	sync_mode = int(config.sync_mode)
	panic_snap_threshold = config.panic_snap_threshold
	stretch_nudge_factor = config.stretch_nudge_factor
	ping_interval = config.ping_interval
	display_offset = config.display_offset
	jitter_multiplier = config.jitter_multiplier
	jitter_window = config.jitter_window
	jitter_stability_threshold = config.jitter_stability_threshold
	enable_drift_logging = config.enable_drift_logging


func _attach_node(node: MultiplayerClock) -> void:
	_node = node


## True once a [MultiplayerClock] configurator has registered. Until then the
## engine is inert: [member tick] stays 0 and no tick signal fires.
func is_configured() -> bool:
	return _configured


## Clears [param node] as the clock's live endpoint while keeping its
## [NetwClockConfig] registered.
##
## A [MultiplayerClock] lives inside a scene, so a scene change frees it. The
## config outlives that node so [method is_configured] stays true and the clock
## keeps running.
## [codeblock]
## # MultiplayerClock, leaving the tree:
## api._clock.detach_node(self)
## api._clock.is_configured()  # still true, the config stays
## [/codeblock]
## The clear only fires when [param node] is the current endpoint, so a node on
## its way out never unbinds a [MultiplayerClock] that registered after it.
func detach_node(node: MultiplayerClock) -> void:
	if _node == node:
		_node = null


## Returns the measured wall-clock cadence of the two loops that pump this
## clock.
##
## Every prediction tier states a sustained-rate precondition and nothing used
## to check it: a peer whose main loop is throttled degrades its physics
## catch-up, and that degradation was only ever derived offline from log
## timestamps. [method physics_step] counts the physics pump and
## [method mark_poll] counts the idle pump, each against the wall clock, so
## both real rates are session truth.
## [codeblock]
## {
##  ┠╴physics_frames (int)   physics_step calls since the first count
##  ┠╴polls (int)            mark_poll calls since the first count
##  ┠╴wall_seconds (float)   span since the first counted call
##  ┠╴physics_hz (float)     physics_frames over wall_seconds
##  ┖╴poll_hz (float)        polls over wall_seconds
## }
## [/codeblock]
## [method force_step] counts nothing, because a manually stepped clock has no
## wall-clock meaning. A gated stepper therefore reports an empty cadence.
func cadence() -> Dictionary:
	var wall := 0.0
	if _cadence_started_usec > 0:
		wall = float(Time.get_ticks_usec() - _cadence_started_usec) / 1_000_000.0
	return {
		&"physics_frames": _physics_frame_count,
		&"polls": _poll_count,
		&"wall_seconds": wall,
		&"physics_hz": _physics_frame_count / wall if wall > 0.0 else 0.0,
		&"poll_hz": _poll_count / wall if wall > 0.0 else 0.0,
	}


## Counts one idle-frame poll toward [method cadence]. [NetwMultiplayer] calls
## this once per [method MultiplayerAPI.poll], which is the session's main-loop
## pump, so the poll rate is the peer's real main-loop rate.
func mark_poll() -> void:
	_poll_count += 1
	if _cadence_started_usec == 0:
		_cadence_started_usec = Time.get_ticks_usec()


## Test seam. Synchronously emits [param count] full ticks without consulting
## real time, mirroring the [method MultiplayerClock._physics_process] tick loop
## body so [signal before_tick], [signal on_tick], [signal after_tick] and
## [member tick] advance identically. Pair with [member manual_tick] so the real
## loop does not also run.
func force_step(count: int = 1) -> void:
	core.force_step(count)

#endregion

#region ── Internal State ──────────────────────────────────────────────────────

const _DRIFT_LOG_INTERVAL := 60.0

# The owning NetwMultiplayer. A weakref because the owner holds this interface
# strongly and both are reference counted.
var _api_ref: WeakRef

# The registered configurator node, kept for signal emission and export reads
# the calibration protocol needs (tickrate_mismatch_action, tickrate_mismatch,
# pong_received).
var _node: MultiplayerClock

var _configured: bool:
	set(v):
		core.configured = v
	get:
		return core.configured

# Cadence counters behind cadence(). Zero until the first counted call, so a
# never-pumped clock reports an empty span rather than a stale one.
var _cadence_started_usec: int = 0
var _physics_frame_count: int = 0
var _poll_count: int = 0
var _drift_samples: Array[int] = []
var _drift_timer: float = 0.0

#endregion

#region ── Engine ──────────────────────────────────────────────────────────────

func _init(api: NetwMultiplayer = null) -> void:
	_api_ref = weakref(api) if api else null
	core = api._native_core.clock_core if api else NetwClockCore.new()
	# The schedule is emitted where it is decided, and this interface is what
	# the session's consumers connect to, so every engine signal is forwarded
	# rather than re-derived. Forwarding is what keeps a caller's connection
	# valid across the seam instead of asking it to know which side ticks.
	core.before_tick_loop.connect(before_tick_loop.emit)
	core.before_tick.connect(before_tick.emit)
	core.on_tick.connect(on_tick.emit)
	core.after_tick.connect(after_tick.emit)
	core.after_tick_loop.connect(after_tick_loop.emit)
	core.clock_synchronized.connect(clock_synchronized.emit)
	if api:
		api._replication.register_protocol(
			NetwFrameEnvelope.Channel.CLOCK_HANDSHAKE,
			_handle_handshake,
		)
		api._replication.register_protocol(
			NetwFrameEnvelope.Channel.CLOCK_HANDSHAKE_REPLY,
			_handle_handshake_reply,
		)
		api._replication.register_protocol(
			NetwFrameEnvelope.Channel.CLOCK_PING,
			_handle_ping,
		)
		api._replication.register_protocol(
			NetwFrameEnvelope.Channel.CLOCK_PONG,
			_handle_pong,
		)
	core.stability_changed.connect(stability_changed.emit)
	core.display_offset_insufficient.connect(display_offset_insufficient.emit)


func _api() -> NetwMultiplayer:
	return _api_ref.get_ref() as NetwMultiplayer if _api_ref else null


## Advances the tick loop by one physics frame. The [MultiplayerClock]
## configurator's pump calls this once per physics frame after its connection
## and [member manual_tick] guards pass.
func physics_step(delta: float) -> void:
	# The cadence counters stay here because they are wall-clock truth about the
	# pump, which is this interface's concern rather than the schedule's.
	_physics_frame_count += 1
	if _cadence_started_usec == 0:
		_cadence_started_usec = Time.get_ticks_usec()

	core.physics_step(delta)


## Arms one simulation gate on this clock, held until [method release_gate].
##
## The clock decides whether a frame advances simulated time. It never touches a
## physics space itself, because it has no scene: [LagCompCore]
## arms a gate when it registers a body the physics server integrates, reads
## [member is_simulating] after the tick loop, and holds that body's space.
func arm_gate() -> void:
	core.arm_gate()


## Releases one gate armed by [method arm_gate]. The last release restores an
## ungated clock, which simulates every frame.
func release_gate() -> void:
	core.release_gate()


## Returns [code]true[/code] while any gate is armed.
func is_gated() -> bool:
	return core.is_gated()


## Advances the tick loop from the API poll while no [MultiplayerClock] drives
## it, so a session keeps ticking after a scene change frees the clock.
##
## A [MultiplayerClock] pumps [method physics_step] every physics frame. With no
## such node, the poll takes over and steps from wall-clock time between calls.
## [codeblock]
## # NetwMultiplayer._poll, every frame:
## clock.poll_step()   # steps only when configured and no node is bound
## [/codeblock]
## The step yields the moment a [MultiplayerClock] rebinds through
## [method detach_node]'s counterpart [method configure], so the two pumps never
## both run.
func poll_step() -> void:
	var span := core.seconds_since_step()
	if _node or manual_tick or not is_configured() or span < 0.0:
		core.mark_step()
		return
	physics_step(span)


## Accumulates the client ping cadence. Returns [code]true[/code] and resets
## the timer when a ping is due, so the [MultiplayerClock] pump owns the send.
func consume_ping_due(delta: float) -> bool:
	return core.consume_ping_due(delta)


## Accumulates the drift-log cadence. Returns [code]true[/code] when the
## 60-second window elapsed, so the pump triggers [method log_drift].
func consume_drift_log_due(delta: float) -> bool:
	_drift_timer += delta
	return _drift_timer >= _DRIFT_LOG_INTERVAL


## Ingests one RTT sample and the server clock position it was measured against,
## recalibrating the local clock. Returns the fresh metrics payload the
## [MultiplayerClock] emits as [signal MultiplayerClock.pong_received].
##
## [param server_tick_phase] is the server's position within
## [param server_tick_at_pong], at or above zero and below one, so the target is a
## continuous clock position rather than a whole tick. Rounding it away would
## make the target jump by a full tick as the ping's arrival phase slid across a
## server tick boundary, and [constant SyncMode.STRETCH] would then chase that
## sawtooth for about a second at a time, dragging the client's tick boundary
## back and forth through the server's consume boundary. The margin that used to
## ride on that rounding is now [member lead_ticks], which is explicit.
func handle_pong(
		sample: float,
		server_tick_at_pong: int,
		server_tick_phase: float = 0.0,
) -> Dictionary:
	# A server echoing its own probe is the identity case: no flight time to
	# cover and nothing to lead, so it anchors on the position it just reported
	# instead of nudging itself forward on every pong. Authority is this
	# interface's to know, so it is passed down rather than looked up.
	var api := _api()
	return core.handle_pong(
		sample,
		server_tick_at_pong,
		server_tick_phase,
		not (api and api.is_server()),
	)

#endregion

#region ── Messaging ───────────────────────────────────────────────────────────

## Sends a clock handshake request to the server over
## [constant NetwFrameEnvelope.Channel.CLOCK_HANDSHAKE]. The [MultiplayerClock]
## pump calls this on connect and after a frame stall, and the server answers
## with its [member tickrate].
func request_handshake() -> void:
	var api := _api()
	if not api:
		return
	api._replication.send_to(
		1,
		0,
		NetwFrameEnvelope.Channel.CLOCK_HANDSHAKE,
		var_to_bytes(tickrate),
		true,
	)


## Sends a latency probe to the server over
## [constant NetwFrameEnvelope.Channel.CLOCK_PING], stamped with the local send
## time. Sent un-batched so aggregation delay never enters the round-trip
## sample [method handle_pong] measures. The [MultiplayerClock] pump calls this
## every [member ping_interval].
func send_ping() -> void:
	var api := _api()
	if not api:
		return
	# The send time is opaque to the server (it only echoes it back), so the low
	# 32 bits are enough: the client recovers the RTT as a wraparound-safe delta.
	var payload := PackedByteArray()
	payload.resize(4)
	payload.encode_u32(0, Time.get_ticks_usec() & 0xFFFFFFFF)
	api._replication.send_to(
		1,
		0,
		NetwFrameEnvelope.Channel.CLOCK_PING,
		payload,
		false,
		0,
		"",
		false,
	)


# Server receive for a handshake request. Replies with the local tickrate.
func _handle_handshake(_payload: PackedByteArray, sender: int) -> void:
	var api := _api()
	if not api or not api.is_server():
		return
	api._replication.send_to(
		sender,
		0,
		NetwFrameEnvelope.Channel.CLOCK_HANDSHAKE_REPLY,
		var_to_bytes(tickrate),
		true,
	)


# Client receive for a handshake reply. Reconciles the server tickrate per the
# configurator's mismatch policy, then opens the ping loop.
func _handle_handshake_reply(payload: PackedByteArray, sender: int) -> void:
	if sender != 1:
		return
	var server_tickrate := int(bytes_to_var(payload))
	if server_tickrate != tickrate and _node:
		match _node.tickrate_mismatch_action:
			0:
				Netw.dbg.warn(
					"MultiplayerClock: tickrate mismatch, local=%d server=%d" % \
							[tickrate, server_tickrate],
					func(m): push_warning(m)
				)
			1:
				var api := _api()
				if api and api.multiplayer_peer:
					api.multiplayer_peer.close()
			2:
				_node.tickrate_mismatch.emit(sender, server_tickrate)
	send_ping()


# Server receive for a latency probe. Echoes the client timestamp with the
# current server clock position: the whole tick plus the phase within it,
# quantized to a byte, so the client can calibrate against a continuous target.
func _handle_ping(payload: PackedByteArray, sender: int) -> void:
	var api := _api()
	if not api or not api.is_server():
		return
	if payload.size() < 4:
		return
	var client_usec := payload.decode_u32(0)
	var phase := core.tick_phase()
	var reply := PackedByteArray()
	reply.resize(9)
	reply.encode_u32(0, client_usec)
	reply.encode_u32(4, tick & 0xFFFFFFFF)
	reply.encode_u8(8, int(phase * 255.0))
	api._replication.send_to(
		sender,
		0,
		NetwFrameEnvelope.Channel.CLOCK_PONG,
		reply,
		false,
		0,
		"",
		false,
	)


# Client receive for a latency reply. Turns the echoed timestamp into an RTT
# sample and recalibrates, emitting the configurator's metrics signal.
func _handle_pong(payload: PackedByteArray, sender: int) -> void:
	if sender != 1:
		return
	if payload.size() < 9:
		return
	var client_usec := payload.decode_u32(0)
	var server_tick := payload.decode_u32(4)
	var server_phase := payload.decode_u8(8) / 255.0
	# Both stamps live in the low 32 bits of the usec clock, so a masked
	# subtraction recovers the elapsed time even across a u32 wrap.
	var elapsed := (Time.get_ticks_usec() & 0xFFFFFFFF) - client_usec
	elapsed &= 0xFFFFFFFF
	var sample := elapsed / 1_000_000.0
	var metrics := handle_pong(sample, server_tick, server_phase)
	if _node:
		_node.pong_received.emit(metrics)

#endregion

#region ── Internal Logic ───────────────────────────────────────────────────────

## Prints the averaged drift over the elapsed window and resets the samples.
func log_drift() -> void:
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

#endregion
