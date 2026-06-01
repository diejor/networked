## Outcome of [method BackendPeer.query_server_info].
##
## Replaces the deleted [code skip-lint]ProbeResult[/code]. Carries either a populated
## [ServerInfo] (on [constant Status.OK]) or a categorical failure reason.
## Use the static helpers — [method ok], [method unreachable], [method timeout],
## [method unsupported], [method busy], [method error] — to build instances.
class_name ServerInfoResult
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
}

var status: Status = Status.UNSUPPORTED
var info: ServerInfo
var latency_ms: int = 0
var message: String = ""


static func ok(info: ServerInfo, latency_ms: int = 0) -> ServerInfoResult:
	var r := ServerInfoResult.new()
	r.status = Status.OK
	r.info = info
	r.latency_ms = latency_ms
	return r


static func unreachable(message: String = "") -> ServerInfoResult:
	var r := ServerInfoResult.new()
	r.status = Status.UNREACHABLE
	r.message = message
	return r


static func timeout(message: String = "") -> ServerInfoResult:
	var r := ServerInfoResult.new()
	r.status = Status.TIMEOUT
	r.message = message
	return r


static func unsupported() -> ServerInfoResult:
	var r := ServerInfoResult.new()
	r.status = Status.UNSUPPORTED
	return r


static func busy(message: String = "") -> ServerInfoResult:
	var r := ServerInfoResult.new()
	r.status = Status.BUSY
	r.message = message
	return r


static func error(message: String = "") -> ServerInfoResult:
	var r := ServerInfoResult.new()
	r.status = Status.ERROR
	r.message = message
	return r


func is_ok() -> bool:
	return status == Status.OK


func _to_string() -> String:
	match status:
		Status.OK:
			return "ServerInfoResult(ok, %d players, %dms)" % [
				info.players if info else 0, latency_ms,
			]
		Status.UNREACHABLE:
			return "ServerInfoResult(unreachable: %s)" % message
		Status.TIMEOUT:
			return "ServerInfoResult(timeout)"
		Status.UNSUPPORTED:
			return "ServerInfoResult(unsupported)"
		Status.BUSY:
			return "ServerInfoResult(busy: %s)" % message
		Status.ERROR:
			return "ServerInfoResult(error: %s)" % message
		_:
			return "ServerInfoResult(?)"
