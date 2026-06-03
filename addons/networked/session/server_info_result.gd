## Outcome of [method BackendPeer.query_server_info].
##
## Replaces the deleted [code skip-lint]ProbeResult[/code]. Carries either a populated
## [ServerInfo] (on [constant Status.OK]) or a categorical failure reason.
## Use the static helpers — [method ok], [method unreachable], [method timeout],
## [method unsupported], [method busy], [method error] — to build instances.
##
## This describes whether a server is reachable, never whether the transport can
## run on this platform. That platform gate is [method BackendPeer.is_available].
## [constant Status.UNSUPPORTED] means the backend skips probing, not that it is
## unusable here.
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
	## The server runs a different game build ([member MultiplayerTree.app_id]),
	## so joining would be rejected at the auth handshake. Carries [member info]
	## when discovery provided it.
	INCOMPATIBLE,
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


## Builds an incompatible result, keeping [param info] when discovery already
## provided player counts so the row can still render them.
static func incompatible(
		info: ServerInfo = null,
		message: String = "",
) -> ServerInfoResult:
	var r := ServerInfoResult.new()
	r.status = Status.INCOMPATIBLE
	r.info = info
	r.message = message
	return r


func is_ok() -> bool:
	return status == Status.OK


func _to_string() -> String:
	match status:
		Status.OK:
			return "ServerInfoResult(ok, %d players, %dms)" % [
				info.players if info else 0,
				latency_ms,
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
		Status.INCOMPATIBLE:
			return "ServerInfoResult(incompatible)"
		_:
			return "ServerInfoResult(?)"
