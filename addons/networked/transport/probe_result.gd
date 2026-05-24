## Outcome of [method BackendPeer.probe] - reports whether a local session
## is reachable without actually opening a [MultiplayerPeer].
##
## Use the static helpers [method reachable], [method unreachable],
## [method unsupported], and [method error] to build instances. The
## convenience getters [method is_reachable] and [method is_unsupported]
## let callers skip switching on [member status] directly.
@tool
class_name ProbeResult
extends RefCounted


## Categorical outcome of the probe attempt.
enum Status {
	## The backend cannot answer probes (e.g. session-id transports like
	## WebRTC, Tube, Steam). Callers should treat as "skip probing".
	UNSUPPORTED,
	## A live local session was detected at the probed address.
	REACHABLE,
	## No server is listening at the probed address.
	UNREACHABLE,
	## The probe itself failed (socket error, invalid address, etc.).
	ERROR,
}

var status: Status = Status.UNSUPPORTED
var latency_ms: int = 0
var info: Dictionary = {}


static func unsupported() -> ProbeResult:
	var r := ProbeResult.new()
	r.status = Status.UNSUPPORTED
	return r


static func reachable(latency: int = 0, extra: Dictionary = {}) -> ProbeResult:
	var r := ProbeResult.new()
	r.status = Status.REACHABLE
	r.latency_ms = latency
	r.info = extra
	return r


static func unreachable(extra: Dictionary = {}) -> ProbeResult:
	var r := ProbeResult.new()
	r.status = Status.UNREACHABLE
	r.info = extra
	return r


static func error(message: String = "") -> ProbeResult:
	var r := ProbeResult.new()
	r.status = Status.ERROR
	r.info = { "message": message }
	return r


func is_reachable() -> bool:
	return status == Status.REACHABLE


func is_unsupported() -> bool:
	return status == Status.UNSUPPORTED


func _to_string() -> String:
	match status:
		Status.REACHABLE:
			return "ProbeResult(reachable, %dms)" % latency_ms
		Status.UNREACHABLE:
			return "ProbeResult(unreachable)"
		Status.UNSUPPORTED:
			return "ProbeResult(unsupported)"
		Status.ERROR:
			return "ProbeResult(error: %s)" % info.get("message", "")
		_:
			return "ProbeResult(?)"
