## Categorical outcome of a server probe.
class_name NetwProbeResult
extends RefCounted

enum Status {
	OK,
	UNREACHABLE,
	TIMEOUT,
	UNSUPPORTED,
	BUSY,
	ERROR,
	INCOMPATIBLE,
}

var status: Status = Status.UNSUPPORTED
var info: NetwServerInfo
var latency_ms: int = 0
var message: String = ""


## Builds a successful result.
static func ok(info: NetwServerInfo, latency_ms: int = 0) -> NetwProbeResult:
	var result := NetwProbeResult.new()
	result.status = Status.OK
	result.info = info
	result.latency_ms = latency_ms
	return result


## Builds an unreachable result.
static func unreachable(message: String = "") -> NetwProbeResult:
	return _make(Status.UNREACHABLE, message)


## Builds a timed-out result.
static func timeout(message: String = "") -> NetwProbeResult:
	return _make(Status.TIMEOUT, message)


## Builds an unsupported result.
static func unsupported() -> NetwProbeResult:
	return _make(Status.UNSUPPORTED)


## Builds a busy result.
static func busy(message: String = "") -> NetwProbeResult:
	return _make(Status.BUSY, message)


## Builds an error result.
static func error(message: String = "") -> NetwProbeResult:
	return _make(Status.ERROR, message)


## Builds an incompatible result while preserving optional server metadata.
static func incompatible(
		info: NetwServerInfo = null,
		message: String = "",
) -> NetwProbeResult:
	var result := _make(Status.INCOMPATIBLE, message)
	result.info = info
	return result


## Returns whether the probe succeeded.
func is_ok() -> bool:
	return status == Status.OK


static func _make(value: Status, detail: String = "") -> NetwProbeResult:
	var result := NetwProbeResult.new()
	result.status = value
	result.message = detail
	return result


func _to_string() -> String:
	match status:
		Status.OK:
			return "NetwProbeResult(ok, %d players, %dms)" % [
				info.players if info else 0,
				latency_ms,
			]
		Status.UNREACHABLE:
			return "NetwProbeResult(unreachable: %s)" % message
		Status.TIMEOUT:
			return "NetwProbeResult(timeout)"
		Status.UNSUPPORTED:
			return "NetwProbeResult(unsupported)"
		Status.BUSY:
			return "NetwProbeResult(busy: %s)" % message
		Status.ERROR:
			return "NetwProbeResult(error: %s)" % message
		Status.INCOMPATIBLE:
			return "NetwProbeResult(incompatible)"
		_:
			return "NetwProbeResult(?)"
