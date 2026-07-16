## Structured outcome of a connection attempt.
##
## A [NetwConnectResult] replaces a bare error code with a typed
## [member status], an optional transport-specific [member detail] refinement,
## and a [member diagnostics] payload, so a caller can tell a signaling failure
## from an unreachable relay from a user abort. [NetwConnectAttempt] resolves
## [signal NetwConnectAttempt.finished] with one.
## [codeblock]
## var result: NetwConnectResult = await attempt.finished
## if not result.is_ok():
##     match result.status:
##         NetwConnectResult.Status.TIMED_OUT:
##             push_warning(result.message)
## [/codeblock]
class_name NetwConnectResult
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
	## The connection attempt was aborted.
	ABORTED,
	## A generic error occurred.
	ERROR,
}

## Categorical outcome mapping to [enum Status].
var status: Status = Status.ERROR

## Transport-specific refinement code, or [code]&""[/code] when none.
var detail: StringName = &""

## Human-readable details about the outcome.
var message: String = ""

## Opaque diagnostics payload populated by a transport or view.
var diagnostics: Dictionary = { }


## Builds an ok result, optionally carrying a transport [param diagnostics]
## snapshot such as the WebRTC candidate stats behind
## [method NetwPeerView.diagnostics].
static func ok(diagnostics: Dictionary = { }) -> NetwConnectResult:
	var r := NetwConnectResult.new()
	r.status = Status.OK
	r.diagnostics = diagnostics
	return r


## Builds a timed-out result.
static func timed_out(message: String = "") -> NetwConnectResult:
	var r := NetwConnectResult.new()
	r.status = Status.TIMED_OUT
	r.message = message
	return r


## Builds an unreachable result.
static func unreachable(
		detail: StringName = &"",
		message: String = "",
		diagnostics: Dictionary = { },
) -> NetwConnectResult:
	var r := NetwConnectResult.new()
	r.status = Status.UNREACHABLE
	r.detail = detail
	r.message = message
	r.diagnostics = diagnostics
	return r


## Builds a refused result.
static func refused(message: String = "") -> NetwConnectResult:
	var r := NetwConnectResult.new()
	r.status = Status.REFUSED
	r.message = message
	return r


## Builds an aborted result.
static func aborted(message: String = "") -> NetwConnectResult:
	var r := NetwConnectResult.new()
	r.status = Status.ABORTED
	r.message = message
	return r


## Builds a generic error result.
static func error(message: String = "") -> NetwConnectResult:
	var r := NetwConnectResult.new()
	r.status = Status.ERROR
	r.message = message
	return r


## Returns [code]true[/code] when [member status] is [constant Status.OK].
func is_ok() -> bool:
	return status == Status.OK


func _to_string() -> String:
	match status:
		Status.OK:
			return "NetwConnectResult(ok)"
		Status.TIMED_OUT:
			return "NetwConnectResult(timed_out)"
		Status.UNREACHABLE:
			return "NetwConnectResult(unreachable: %s, detail: %s)" % [
				message,
				detail,
			]
		Status.REFUSED:
			return "NetwConnectResult(refused: %s)" % message
		Status.ABORTED:
			return "NetwConnectResult(aborted)"
		Status.ERROR:
			return "NetwConnectResult(error: %s)" % message
		_:
			return "NetwConnectResult(?)"
