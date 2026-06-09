## Structured outcome of a connection handshake.
##
## ConnectResult replaces generic error codes with typed statuses and
## diagnostics so the caller can distinguish signaling failures, unreachable
## relays, and user aborts.
##
## [codeblock]
## var result := ConnectResult.timed_out("Signaler timed out")
## if not result.is_ok():
##     match result.status:
##         ConnectResult.Status.TIMED_OUT:
##             print(result.message)
## [/codeblock]
##
## ConnectResult is returned from [method MultiplayerTree.join] and emitted by
## [signal BackendPeer.connect_failed] to provide structured connection
## feedback. A sibling discovery phase maps outcome statuses through
## [ServerInfoResult].
class_name ConnectResult
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
