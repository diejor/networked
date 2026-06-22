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

var _connect_progress := ConnectProgress.new()

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
	0.01,
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
## Availability is a separate axis from [method probe_server_info]. A backend
## that returns [method ProbeResult.unsupported] connects fine. It just
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


## Looks up [ServerDescriptor.Info] for [param _address] without joining the server.
##
## The default result is [method ProbeResult.unsupported]. Backends with a
## lightweight connection path can override this with [AuthProtocol.Client].
## Directory based backends can return cached metadata or keep the default.
## [codeblock]
## # Lightweight connection. Probe the same endpoint that join would use.
## return await AuthProtocol.Client.new(self).query(address, timeout)
##
## # Directory metadata. Use cached lobby data, or report unsupported.
## return BackendPeer.ProbeResult.unsupported()
## [/codeblock]
func probe_server_info(
		_address: String,
		_timeout: float = 2.0,
) -> ProbeResult:
	return ProbeResult.unsupported()


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


## UI hint for the address accepted by a [BackendPeer].
##
## [method BackendPeer.get_address_hint] returns this for generic connect
## dialogs.
class AddressHint:
	extends RefCounted

	## Label for the address field.
	var label: String = "Address"

	## Placeholder shown in an empty field.
	var placeholder: String = ""

	## Help text for tooltips or inline hints.
	var help_text: String = ""

	## Optional regular expression for validation.
	var validator_regex: String = ""

	## [code]true[/code] when an empty address is valid.
	var accepts_empty: bool = false

	## [code]true[/code] when [method BackendPeer.probe_server_info] is useful.
	var supports_probe: bool = false

	## [code]true[/code] when address input should be hidden.
	var hides_address_field: bool = false


	## Creates an [BackendPeer.AddressHint] from the common UI fields.
	static func make(
			p_label: String,
			p_placeholder: String = "",
			p_help: String = "",
			p_accepts_empty: bool = false,
			p_supports_probe: bool = false,
	) -> AddressHint:
		var h := AddressHint.new()
		h.label = p_label
		h.placeholder = p_placeholder
		h.help_text = p_help
		h.accepts_empty = p_accepts_empty
		h.supports_probe = p_supports_probe
		return h


## Smoothly eases connection progress bar values during client join attempts.
##
## [MultiplayerTree] or [BackendPeer] drives this tracker during a client connection
## to provide a smooth, time-eased percentage to the connection UI instead of
## freezing at static thresholds.
##
## [codeblock]
## var tracker := BackendPeer.ConnectProgress.new()
## tracker.start(Time.get_ticks_msec(), 10.0)
## # ...in poll loop...
## var sample := tracker.poll(Time.get_ticks_msec())
## if not sample.is_empty():
##     print("Progress: ", sample.message, " ratio: ", sample.ratio)
## [/codeblock]
class ConnectProgress:
	extends RefCounted

	## The exponential ease factor used to calculate the progress curve.
	const EASE := 5.0

	## The maximum ratio limit returned by this tracker to reserve the final tick for success.
	const MAX_RATIO := 0.98

	## Throttling interval in milliseconds for regular poll updates.
	const EMIT_INTERVAL_MS := 100

	var _start_ms := 0
	var _bound := 0.0
	var _message := ""
	var _step := &""
	var _last_emit_ms := 0


	## Starts tracking progress with the starting timestamp [param start_ms] and timeout budget [param bound].
	##
	## The [param bound] determines the scaling factor for the eased progress ratio.
	## [codeblock]
	## tracker.start(Time.get_ticks_msec(), 10.0)
	## [/codeblock]
	func start(start_ms: int, bound: float) -> void:
		_start_ms = start_ms
		_bound = maxf(bound, 0.1)
		_message = ""
		_step = &""
		_last_emit_ms = 0


	## Resets the tracker state to idle.
	##
	## Clears the starting timestamp and all active progress messages/steps.
	## [codeblock]
	## tracker.stop()
	## [/codeblock]
	func stop() -> void:
		_start_ms = 0
		_bound = 0.0
		_message = ""
		_step = &""
		_last_emit_ms = 0


	## Sets the current progress [param message] and returns the updated sample.
	##
	## Bypasses the polling throttle interval to immediately return the updated status.
	## [codeblock]
	## var sample := tracker.set_message("Handshaking...", Time.get_ticks_msec())
	## [/codeblock]
	func set_message(message: String, now_ms: int) -> Dictionary:
		_message = message
		return _sample(now_ms, true)


	## Sets the current progress [param step] and returns the updated sample.
	##
	## Bypasses the polling throttle interval to immediately return the updated status.
	## [codeblock]
	## var sample := tracker.set_step(&"handshake", Time.get_ticks_msec())
	## [/codeblock]
	func set_step(step: StringName, now_ms: int) -> Dictionary:
		_step = step
		return _sample(now_ms, true)


	## Polls the current tracker state and returns the sample.
	##
	## Throttles emissions to [constant EMIT_INTERVAL_MS] (100ms) unless forced. Returns
	## an empty [Dictionary] when throttled or idle.
	## [codeblock]
	## var sample := tracker.poll(Time.get_ticks_msec())
	## [/codeblock]
	func poll(now_ms: int) -> Dictionary:
		return _sample(now_ms, false)


	## Returns the current time-eased ratio.
	##
	## The ratio is eased using the exponent of [constant EASE] and clamped between
	## [code]0.0[/code] and [constant MAX_RATIO].
	## [codeblock]
	## var val := tracker.ratio(Time.get_ticks_msec())
	## [/codeblock]
	func ratio(now_ms: int) -> float:
		if _start_ms <= 0 or _bound <= 0.0:
			return 0.0
		var elapsed := float(now_ms - _start_ms) * 0.001
		var scaled := maxf(0.0, elapsed / _bound)
		return clampf(1.0 - exp(-EASE * scaled), 0.0, MAX_RATIO)


	func _sample(now_ms: int, force: bool) -> Dictionary:
		if _start_ms <= 0 or (_message.is_empty() and _step.is_empty()):
			return { }
		if not force and now_ms - _last_emit_ms < EMIT_INTERVAL_MS:
			return { }
		_last_emit_ms = now_ms
		return {
			"step": _step,
			"message": _message,
			"ratio": ratio(now_ms),
		}


## Structured outcome of a connection handshake.
##
## ConnectResult replaces generic error codes with typed statuses and
## diagnostics so the caller can distinguish signaling failures, unreachable
## relays, and user aborts.
##
## [codeblock]
## var result := BackendPeer.ConnectResult.timed_out("Signaler timed out")
## if not result.is_ok():
##     match result.status:
##         BackendPeer.ConnectResult.Status.TIMED_OUT:
##             print(result.message)
## [/codeblock]
##
## ConnectResult is returned from [method MultiplayerTree.join] and emitted by
## [signal BackendPeer.connect_failed] to provide structured connection
## feedback. A sibling discovery phase maps outcome statuses through
## [BackendPeer.ProbeResult].
class ConnectResult:
	extends RefCounted

	## Categorical outcome of the connection attempt.
	enum Status {
		## The connection succeeded.
		OK,
		## The connection expired before completing.
		TIMED_OUT,
		## The target host or signaling is unreachable.
		UNREACHABLE,
		## The host explicitly refused the connection.
		REFUSED,
		## The connection attempt was aborted by the user.
		ABORTED,
		## A generic error occurred.
		ERROR,
	}

	## Categorical outcome status mapping to [enum Status].
	var status: Status = Status.ERROR

	## Backend-specific refinement code, or &"" when none.
	var detail: StringName = &""

	## Human-readable details about the outcome.
	var message: String = ""

	## Opaque diagnostics payload populated by backends.
	##
	## [codeblock]
	## Dictionary
	##  ┠╴phases (Dictionary)
	##  ┃  ┖╴{ }
	##  ┃     ┠╴offer_ms (int)
	##  ┃     ┠╴answer_ms (int)
	##  ┃     ┖╴native_ms (int)
	##  ┠╴candidates (Dictionary)
	##  ┃  ┖╴{ }
	##  ┃     ┠╴host (int)
	##  ┃     ┠╴srflx (int)
	##  ┃     ┖╴relay (int)
	##  ┖╴relay_used (bool)                    # true if only relay candidates gathered
	## [/codeblock]
	var diagnostics: Dictionary = { }


	## Builds an ok connection result.
	static func ok() -> ConnectResult:
		var r := ConnectResult.new()
		r.status = Status.OK
		return r


	## Builds a timed out connection result.
	static func timed_out(message: String = "") -> ConnectResult:
		var r := ConnectResult.new()
		r.status = Status.TIMED_OUT
		r.message = message
		return r


	## Builds an unreachable connection result.
	static func unreachable(
			detail: StringName = &"",
			message: String = "",
			diagnostics: Dictionary = { },
	) -> ConnectResult:
		var r := ConnectResult.new()
		r.status = Status.UNREACHABLE
		r.detail = detail
		r.message = message
		r.diagnostics = diagnostics
		return r


	## Builds a connection refused result.
	static func refused(message: String = "") -> ConnectResult:
		var r := ConnectResult.new()
		r.status = Status.REFUSED
		r.message = message
		return r


	## Builds a connection aborted result.
	static func aborted(message: String = "") -> ConnectResult:
		var r := ConnectResult.new()
		r.status = Status.ABORTED
		r.message = message
		return r


	## Builds a generic error connection result.
	static func error(message: String = "") -> ConnectResult:
		var r := ConnectResult.new()
		r.status = Status.ERROR
		r.message = message
		return r


	## Returns [code]true[/code] when status is [constant Status.OK].
	func is_ok() -> bool:
		return status == Status.OK


	func _to_string() -> String:
		match status:
			Status.OK:
				return "ConnectResult(ok)"
			Status.TIMED_OUT:
				return "ConnectResult(timed_out)"
			Status.UNREACHABLE:
				return "ConnectResult(unreachable: %s, detail: %s)" % [
					message,
					detail,
				]
			Status.REFUSED:
				return "ConnectResult(refused: %s)" % message
			Status.ABORTED:
				return "ConnectResult(aborted)"
			Status.ERROR:
				return "ConnectResult(error: %s)" % message
			_:
				return "ConnectResult(?)"


## Outcome of [method BackendPeer.probe_server_info].
##
## Carries either a populated [ServerDescriptor.Info] (on [constant Status.OK]) or a categorical failure reason.
## Use the static helpers — [method ok], [method unreachable], [method timeout],
## [method unsupported], [method busy], [method error] — to build instances.
##
## This describes whether a server is reachable, never whether the transport can
## run on this platform. That platform gate is [method BackendPeer.is_available].
## [constant Status.UNSUPPORTED] means the backend skips probing, not that it is
## unusable here.
class ProbeResult:
	extends RefCounted

	## Categorical outcome of the probe.
	enum Status {
		## A probe reply was received and decoded successfully.
		OK,
		## The backend completed without receiving a valid reply (e.g. server
		## refused, port closed).
		UNREACHABLE,
		## The probe expired before a reply arrived.
		TIMEOUT,
		## The backend cannot answer probes (e.g. session-id transports like
		## Steam, Local loopback). Treat as "skip probing".
		UNSUPPORTED,
		## The server is rate-limiting probes; retry later.
		BUSY,
		## The probe itself failed (socket error, invalid address, encoder
		## mismatch, etc.).
		ERROR,
		## The server runs a different game build ([member MultiplayerTree.app_id]),
		## so joining would be rejected at the auth handshake. Carries [member info]
		## when discovery provided it.
		INCOMPATIBLE,
	}

	var status: Status = Status.UNSUPPORTED
	var info: ServerDescriptor.Info
	var latency_ms: int = 0
	var message: String = ""


	static func ok(info: ServerDescriptor.Info, latency_ms: int = 0) -> ProbeResult:
		var r := ProbeResult.new()
		r.status = Status.OK
		r.info = info
		r.latency_ms = latency_ms
		return r


	static func unreachable(message: String = "") -> ProbeResult:
		var r := ProbeResult.new()
		r.status = Status.UNREACHABLE
		r.message = message
		return r


	static func timeout(message: String = "") -> ProbeResult:
		var r := ProbeResult.new()
		r.status = Status.TIMEOUT
		r.message = message
		return r


	static func unsupported() -> ProbeResult:
		var r := ProbeResult.new()
		r.status = Status.UNSUPPORTED
		return r


	static func busy(message: String = "") -> ProbeResult:
		var r := ProbeResult.new()
		r.status = Status.BUSY
		r.message = message
		return r


	static func error(message: String = "") -> ProbeResult:
		var r := ProbeResult.new()
		r.status = Status.ERROR
		r.message = message
		return r


	## Builds an incompatible result, keeping [param info] when discovery already
	## provided player counts so the row can still render them.
	static func incompatible(
			info: ServerDescriptor.Info = null,
			message: String = "",
	) -> ProbeResult:
		var r := ProbeResult.new()
		r.status = Status.INCOMPATIBLE
		r.info = info
		r.message = message
		return r


	func is_ok() -> bool:
		return status == Status.OK


	func _to_string() -> String:
		match status:
			Status.OK:
				return "ProbeResult(ok, %d players, %dms)" % [
					info.players if info else 0,
					latency_ms,
				]
			Status.UNREACHABLE:
				return "ProbeResult(unreachable: %s)" % message
			Status.TIMEOUT:
				return "ProbeResult(timeout)"
			Status.UNSUPPORTED:
				return "ProbeResult(unsupported)"
			Status.BUSY:
				return "ProbeResult(busy: %s)" % message
			Status.ERROR:
				return "ProbeResult(error: %s)" % message
			Status.INCOMPATIBLE:
				return "ProbeResult(incompatible)"
			_:
				return "ProbeResult(?)"
