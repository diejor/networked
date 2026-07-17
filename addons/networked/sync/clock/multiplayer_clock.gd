## Synchronises simulation time between server and clients with drift and stall protection.
##
## The [MultiplayerClock] node is the authoring surface and protocol endpoint
## holder for the tick engine that lives in [NetwClockInterface], owned by
## [NetwMultiplayer]. Registering through
## [method MultiplayerAPI.object_configuration_add] pushes this node's export
## snapshot into [member NetwMultiplayer.clock] and starts the engine. The
## engine handles RTT smoothing, clock drift correction, and frame stall
## detection to prevent "spiral of death" scenarios.
## [codeblock]
## # The clock registers itself automatically on the MultiplayerTree.
## # Access the engine from any node via:
## var clock := NetwMultiplayer.of(self).clock
##
## # Connect to the simulation loop:
## clock.on_tick.connect(func(delta, tick):
##     _simulate_physics(tick)
## )
## [/codeblock]
@icon("res://addons/networked/assets/MultiplayerClock.svg")
@tool
class_name MultiplayerClock
extends NetwService

#region ── Signals ─────────────────────────────────────────────────────────────

## Fires when the multiplayer API and clock registration are complete.
signal configured

## Fires when a connecting peer's tickrate does not match the local tickrate.
signal tickrate_mismatch(peer_id: int, their_tickrate: int)

## Fires after each ping/pong cycle with fresh clock metrics for the debugger.
signal pong_received(data: Dictionary)

#endregion

#region ── Configuration ───────────────────────────────────────────────────────

@export_group("Simulation")
## How many simulation ticks to run per second.

@export_custom(0, "suffix:frames") var tickrate: int = 30:
	set(v):
		tickrate = v
		if _interface:
			_interface.tickrate = v

## Maximum simulation ticks allowed to run in a single physics frame.
@export_custom(0, "suffix:ticks") var max_ticks_per_frame: int = 8:
	set(v):
		max_ticks_per_frame = v
		if _interface:
			_interface.max_ticks_per_frame = v

## Frame delta threshold before resetting the accumulator.
@export_custom(0, "suffix:s") var stall_threshold: float = 1.0:
	set(v):
		stall_threshold = v
		if _interface:
			_interface.stall_threshold = v

@export var use_physics_interpolation: bool = true:
	set(v):
		use_physics_interpolation = v
		if _interface:
			_interface.use_physics_interpolation = v

@export_group("Calibration")
@export var sync_mode: NetwClockInterface.SyncMode = NetwClockInterface.SyncMode.STRETCH:
	set(v):
		sync_mode = v
		if _interface:
			_interface.sync_mode = v

## The maximum allowed divergence before a hard Snap is forced.
@export_custom(0, "suffix:ticks") var panic_snap_threshold: int = 20:
	set(v):
		panic_snap_threshold = v
		if _interface:
			_interface.panic_snap_threshold = v

## Fraction of the remaining divergence the Stretch clock closes each frame.
@export_range(0.01, 0.5) var stretch_nudge_factor: float = 0.05:
	set(v):
		stretch_nudge_factor = v
		if _interface:
			_interface.stretch_nudge_factor = v

## How often the client pings the server to refresh RTT and recalibrate.
@export_custom(0, "suffix:s") var ping_interval: float = 0.1:
	set(v):
		ping_interval = v
		if _interface:
			_interface.ping_interval = v

@export_group("Network Buffering")
## The number of ticks the visual display lags behind the simulation.
@export_custom(0, "suffix:ticks") var display_offset: int = 2:
	set(v):
		display_offset = v
		if _interface:
			_interface.display_offset = v

## Scales jitter impact on the recommended display offset.
@export var jitter_multiplier: float = 2.0:
	set(v):
		jitter_multiplier = v
		if _interface:
			_interface.jitter_multiplier = v

## Number of recent RTT samples averaged for jitter and the recommendation.
@export_custom(0, "suffix:samples") var jitter_window: int = 16:
	set(v):
		jitter_window = v
		if _interface:
			_interface.jitter_window = v

## The threshold below which the connection is considered stable.
@export_custom(0, "suffix:s") var jitter_stability_threshold: float = 0.05:
	set(v):
		jitter_stability_threshold = v
		if _interface:
			_interface.jitter_stability_threshold = v

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
@export var enable_drift_logging: bool = false:
	set(v):
		enable_drift_logging = v
		if _interface:
			_interface.enable_drift_logging = v

#endregion

#region ── Public API ──────────────────────────────────────────────────────────

## The [NetwClockInterface] engine this node configures, or [code]null[/code]
## before registration. Consumers should reach the engine through
## [member NetwMultiplayer.clock] rather than this node.
var _interface: NetwClockInterface

# The typed payload registered with the API on entry, snapshotting the exports.
var _config: NetwClockConfig


## Locates the [MultiplayerClock] registered on the node's multiplayer API.
static func for_node(node: Node) -> MultiplayerClock:
	var api := node.multiplayer
	if api and api.has_meta(&"_multiplayer_clock"):
		return api.get_meta(&"_multiplayer_clock")
	return null

#endregion

#region ── Lifecycle ───────────────────────────────────────────────────────────

func _service_type() -> Script:
	return MultiplayerClock


func _service_entered(mt: MultiplayerTree) -> void:
	if mt.api:
		_interface = mt.api.clock
		_config = _build_config()
		mt.api.object_configuration_add(self, _config)

	if not mt.session_entered.is_connected(_on_tree_configured):
		mt.session_entered.connect(_on_tree_configured)

	if not mt.session_entered.is_connected(configured.emit):
		mt.session_entered.connect(configured.emit)

	if mt.is_online():
		_on_tree_configured.call_deferred()


func _service_exiting(mt: MultiplayerTree) -> void:
	# Detaching keeps the config registered, so freeing this node never stops
	# the clock.
	if mt.api:
		mt.api.clock.detach_node(self)

	if mt.session_entered.is_connected(_on_tree_configured):
		mt.session_entered.disconnect(_on_tree_configured)

	if mt.session_entered.is_connected(configured.emit):
		mt.session_entered.disconnect(configured.emit)


# Snapshots the current exports into the typed payload the interface configures
# from. Export setters keep pushing live edits straight to the interface, so
# this runs once per registration.
func _build_config() -> NetwClockConfig:
	var config := NetwClockConfig.new()
	config.tickrate = tickrate
	config.max_ticks_per_frame = max_ticks_per_frame
	config.stall_threshold = stall_threshold
	config.use_physics_interpolation = use_physics_interpolation
	config.sync_mode = sync_mode
	config.panic_snap_threshold = panic_snap_threshold
	config.stretch_nudge_factor = stretch_nudge_factor
	config.ping_interval = ping_interval
	config.display_offset = display_offset
	config.jitter_multiplier = jitter_multiplier
	config.jitter_window = jitter_window
	config.jitter_stability_threshold = jitter_stability_threshold
	config.enable_drift_logging = enable_drift_logging
	return config


func _physics_process(delta: float) -> void:
	if Engine.is_editor_hint() or not multiplayer or \
			not multiplayer.has_multiplayer_peer():
		return

	if multiplayer.multiplayer_peer.get_connection_status() != \
			MultiplayerPeer.CONNECTION_CONNECTED:
		return

	if not _interface or _interface.manual_tick:
		return

	if delta > stall_threshold and not multiplayer.is_server():
		_interface.request_handshake()

	_interface.physics_step(delta)

	if not multiplayer.is_server() and _interface.is_synchronized:
		if _interface.consume_ping_due(delta):
			_interface.send_ping()

		if enable_drift_logging and _interface.consume_drift_log_due(delta):
			_interface.log_drift()


func _on_tree_configured() -> void:
	var api := multiplayer
	if api:
		api.set_meta(&"_multiplayer_clock", self)
		if not api.server_disconnected.is_connected(_on_server_disconnect):
			api.server_disconnected.connect(_on_server_disconnect)
		if not api.connection_failed.is_connected(_on_server_disconnect):
			api.connection_failed.connect(_on_server_disconnect)

	if not multiplayer.is_server() and _interface:
		if multiplayer.multiplayer_peer.get_connection_status() == \
				MultiplayerPeer.CONNECTION_CONNECTED:
			_interface.request_handshake()
		elif not multiplayer.connected_to_server.is_connected(
			_interface.request_handshake,
		):
			multiplayer.connected_to_server.connect(
				_interface.request_handshake,
				CONNECT_ONE_SHOT,
			)


func _on_server_disconnect() -> void:
	if _interface:
		_interface.is_synchronized = false

#endregion

#region ── Internal Logic ──────────────────────────────────────────────────────

func _run_auto_config() -> void:
	if multiplayer.is_server():
		return
	Netw.dbg.info("MultiplayerClock: Starting 5s auto-config test...")
	var max_rec := 0
	for i in range(50):
		await get_tree().create_timer(0.1).timeout
		if not is_instance_valid(self):
			return
		max_rec = maxi(max_rec, _interface.recommended_display_offset if _interface else 0)
	display_offset = max_rec
	Netw.dbg.info(
		"MultiplayerClock: Auto-config complete. display_offset = %d" % \
				[max_rec],
	)

#endregion
