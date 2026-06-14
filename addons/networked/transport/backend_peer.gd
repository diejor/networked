## Abstract transport contract for [MultiplayerTree].
##
## [MultiplayerTree] owns [member MultiplayerTree.api] and calls this resource
## to create, poll, probe, and reset the active [MultiplayerPeer].
## [codeblock]
## func create_host_peer(tree: MultiplayerTree) -> MultiplayerPeer:
##     var peer := ENetMultiplayerPeer.new()
##     peer.create_server(port)
##     return peer
##
## func create_join_peer(
##     tree: MultiplayerTree, address: String, username: String = ""
## ) -> MultiplayerPeer:
##     var peer := ENetMultiplayerPeer.new()
##     peer.create_client(address, port)
##     return peer
## [/codeblock]
@tool
@abstract
class_name BackendPeer
extends Resource

## Emitted when an in-progress client connection has a terminal failure.
## [br][br]
## Carries a [ConnectResult] outcome.
signal connect_failed(result: ConnectResult)

## Emitted while an in-progress client connection advances.
## [br][br]
## The [param step] indicates the connection phase name, [param message]
## is a human-readable progress details message, and [param ratio] is the progress
## ratio between [code]0.0[/code] and [code]1.0[/code].
## [signal connect_failed] still carries the terminal failure outcome.
signal connect_progress(step: StringName, message: String, ratio: float)

var _connect_progress := ConnectProgressTracker.new()

const _MSEC_TO_SEC := 0.001
const _PERCENT_TO_RATIO := 0.01
const _LEGACY_SECONDS_MAX := 1.0

@export_group("Lag Simulation")
## Enables [LaggyMultiplayerPeer] wrapping in [method wrap_peer].
## Lag simulation delays each packet independently.
## [codeblock]
## client ping -> one way delay -> server
## server pong -> one way delay -> client
##
## observed RTT ~= ping delay + pong delay + frame and transport cost
## [/codeblock]
## Example with [member one_way_delay_min] = [code]10 ms[/code] and
## [member one_way_delay_max] = [code]100 ms[/code]:
## [codeblock]
## artificial RTT floor   ~= 20 ms
## artificial RTT ceiling ~= 200 ms
## observed clock RTT     ~= artificial RTT + runtime overhead
## [/codeblock]
## Jitter is measured from delivered clock samples. Packet loss, polling, and
## frame timing can all widen it.
## Watch the [code]MultiplayerClock *[/code] performance monitors to compare
## [member MultiplayerClock.rtt], [member MultiplayerClock.rtt_jitter], the pong
## calibration error from [signal MultiplayerClock.pong_received], and
## [member MultiplayerClock.recommended_display_offset].
@export var simulate_lag: bool = false
## Minimum simulated one way packet delay in milliseconds.
@export_range(
	0.0,
	250.0,
	1.0,
	"suffix:ms",
)
var one_way_delay_min: float = 10.0
## Maximum simulated one way packet delay in milliseconds.
@export_range(
	0.0,
	250.0,
	1.0,
	"suffix:ms",
)
var one_way_delay_max: float = 100.0
## Simulated packet loss percentage.
@export_range(
	0.0,
	25.0,
	0.1,
	"suffix:%",
)
var lag_packet_loss_percent: float = 0.0


## Wraps [param base_peer] with [LaggyMultiplayerPeer] when enabled.
##
## Dynamic construction keeps projects without the extension loadable.
func wrap_peer(base_peer: MultiplayerPeer) -> MultiplayerPeer:
	if not base_peer:
		return null
	if not simulate_lag:
		return base_peer

	if not ClassDB.class_exists(&"LaggyMultiplayerPeer"):
		Netw.dbg.warn(
			"Lag simulation is enabled but LaggyMultiplayerPeer is missing.",
			func(m): push_warning(m)
		)
		return base_peer

	var min_delay_ms := maxf(0.0, one_way_delay_min)
	var max_delay_ms := maxf(min_delay_ms, one_way_delay_max)
	var packet_loss_percent := clampf(lag_packet_loss_percent, 0.0, 100.0)

	Netw.dbg.info(
		"Wrapping peer in LaggyMultiplayerPeer "
		+ "(delay: %.1f-%.1f ms, packet loss: %d%%)",
		[
			min_delay_ms,
			max_delay_ms,
			int(packet_loss_percent),
		],
	)

	var laggy_instance: Object = ClassDB.instantiate(&"LaggyMultiplayerPeer")
	var wrapped_peer: MultiplayerPeer = laggy_instance.call(&"create", base_peer)
	if wrapped_peer:
		wrapped_peer.set(&"delay_minimum", min_delay_ms * _MSEC_TO_SEC)
		wrapped_peer.set(&"delay_maximum", max_delay_ms * _MSEC_TO_SEC)
		wrapped_peer.set(
			&"packet_loss",
			packet_loss_percent * _PERCENT_TO_RATIO,
		)
		return wrapped_peer

	return base_peer


# Migrates saved lag properties to the current inspector units.
func _set(property: StringName, value: Variant) -> bool:
	match property:
		&"lag_min_delay":
			one_way_delay_min = _coerce_legacy_delay_msec(float(value))
			return true
		&"lag_max_delay":
			one_way_delay_max = _coerce_legacy_delay_msec(float(value))
			return true
		&"lag_packet_loss":
			lag_packet_loss_percent = float(value) * 100.0
			return true
	return false


func _coerce_legacy_delay_msec(value: float) -> float:
	if value > 0.0 and value <= _LEGACY_SECONDS_MAX:
		return value * 1000.0
	return value


## Prepares this backend for [method create_host_peer] or
## [method create_join_peer].
##
## Override to resolve scene services or external handles.
func setup(_tree: MultiplayerTree) -> Error:
	return OK


## Produces a [MultiplayerPeer] in server mode. May [code]await[/code].
##
## Return [code]null[/code] to signal [code]ERR_CANT_CREATE[/code].
## [MultiplayerTree] mounts the returned peer on [member MultiplayerTree.api].
@abstract
func create_host_peer(_tree: MultiplayerTree) -> MultiplayerPeer


## Produces a [MultiplayerPeer] in client mode connecting to [param _address].
##
## May [code]await[/code]. Return [code]null[/code] to signal failure.
@abstract
func create_join_peer(
		_tree: MultiplayerTree,
		_address: String,
		_username: String = "",
) -> MultiplayerPeer


## Begins tracking connection progress with the given timeout [param bound].
##
## Starts the time-eased progress tracking and sets the default connection message.
## [codeblock]
## backend.begin_connect_progress(10.0)
## [/codeblock]
func begin_connect_progress(bound: float) -> void:
	_connect_progress.start(Time.get_ticks_msec(), bound)
	_set_connect_message("Connecting...")


## Ends tracking connection progress.
##
## Stops the progress tracker and cleans up the active progress state.
## [codeblock]
## backend.end_connect_progress()
## [/codeblock]
func end_connect_progress() -> void:
	_connect_progress.stop()


## Polls backend state outside [member MultiplayerTree.api].
##
## [MultiplayerTree] polls [member MultiplayerTree.api] separately.
func poll(_dt: float) -> void:
	_emit_connect_progress(_connect_progress.poll(Time.get_ticks_msec()))


func _set_connect_message(message: String) -> void:
	_emit_connect_progress(
		_connect_progress.set_message(message, Time.get_ticks_msec()),
	)


func _set_connect_step(step: StringName) -> void:
	_emit_connect_progress(
		_connect_progress.set_step(step, Time.get_ticks_msec()),
	)


func _emit_connect_progress(sample: Dictionary) -> void:
	if not sample.is_empty():
		connect_progress.emit(sample.step, sample.message, sample.ratio)


## Clears backend state before a new session or teardown.
func peer_reset_state() -> void:
	pass


## Returns the address clients should use to join a hosted session.
##
## Override in subclasses that use dynamic addresses or room codes.
func get_join_address() -> String:
	return "localhost"


## Returns [code]true[/code] when [method MultiplayerTree.join_or_host] can
## create an embedded server.
##
## Lobby mediated transports should return [code]false[/code].
func supports_embedded_server() -> bool:
	return true


## Returns [code]true[/code] when this backend can run on the current platform
## and build.
##
## Availability is a separate axis from [method query_server_info]. A backend
## that returns [method ServerInfoResult.unsupported] connects fine. It just
## reports status through a directory instead of a probe. A backend that returns
## [code]false[/code] here cannot connect at all, so the browser hides it from
## host and join flows. Self-contained transports answer with a platform feature
## check. Directory mediated transports leave availability to the directory
## through [signal LobbyDirectory.provider_unavailable].
## [codeblock]
## func is_available() -> bool:
##     return not OS.has_feature("web")
## [/codeblock]
func is_available() -> bool:
	return true


## Returns [code]true[/code] when this backend can host a session on the current
## platform.
##
## Hosting is narrower than [method is_available]. A web client joins a
## [WebSocketBackend] server fine but cannot open a listening socket to host one,
## so it is available yet cannot host. The browser hides a backend that cannot
## host here from the Host form. This is distinct from
## [method supports_embedded_server], which routes
## [method MultiplayerTree.join_or_host] between probing and direct hosting.
## [codeblock]
## func can_host() -> bool:
##     return not OS.has_feature("web")
## [/codeblock]
func can_host() -> bool:
	return true


## Looks up [ServerInfo] for [param _address] without joining the server.
##
## The default result is [method ServerInfoResult.unsupported]. Backends with a
## lightweight connection path can override this with [AuthProbeClient].
## Directory based backends can return cached metadata or keep the default.
## [codeblock]
## # Lightweight connection. Probe the same endpoint that join would use.
## return await AuthProbeClient.new(self).query(address, timeout)
##
## # Directory metadata. Use cached lobby data, or report unsupported.
## return ServerInfoResult.unsupported()
## [/codeblock]
func query_server_info(
		_address: String,
		_timeout: float = 2.0,
) -> ServerInfoResult:
	return ServerInfoResult.unsupported()


## Seconds [MultiplayerTree] should allow a connect attempt before timing out.
##
## The default matches the historical fixed budget. Backends with a slower or
## self-healing connect path (a retrying WebRTC join) widen it. Return
## [code]-1[/code] to declare the backend self-managed, leaving the terminal
## outcome to its own signals plus a safety-net ceiling.
func connect_timeout_hint() -> float:
	return 5.0


## Returns the [AddressHint] for connect UI fields.
func get_address_hint() -> AddressHint:
	var hint := AddressHint.new()
	hint.label = "Address"
	hint.accepts_empty = true
	return hint


## Copies state after [member MultiplayerTree.backend] duplicates this resource.
##
## Override for shared references that [method Resource.duplicate] would reset.
func copy_from(_source: BackendPeer) -> void:
	pass


## Returns a configured copy of this backend template.
##
## [method Resource.duplicate] resets the shared references that
## [method copy_from] restores, so a bare [method Resource.duplicate] yields a
## half-built instance. This pairs the two so no caller can forget the second
## step.
## [codeblock]
## var inst := template.clone()    # duplicate() + copy_from(template)
## [/codeblock]
func clone() -> BackendPeer:
	var inst := duplicate() as BackendPeer
	inst.copy_from(self)
	return inst


## Returns a diagnostics snapshot for [param _peer_id] containing connection
## phase timestamps and statistics.
func get_connection_diagnostics(_peer_id: int) -> Dictionary:
	return { }


## Returns the display name for this backend.
func get_display_name() -> String:
	return "Generic"
