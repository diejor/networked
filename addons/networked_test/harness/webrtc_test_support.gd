## Erases the benign native WebRTC teardown error from a running framework.
##
## Tearing down a connected WebRTC session, either when the retry path replaces
## a stale peer or on close, makes libdatachannel log
## [code]SctpTransport::sendReset ... errno=2[/code] on Linux. That is a
## harmless [code]ENOENT[/code] on an already-gone stream, and it is the only
## error this helper claims is benign. A framework that records engine errors
## would otherwise report it as a failure of whatever test happened to be
## running.
## [codeblock]
##     WebRTCTestSupport.erase_benign_error = my_framework_eraser
##     # ... after any WebRTC teardown
##     await WebRTCTestSupport.clear_optional_sctp_reset_error()
## [/codeblock]
class_name WebRTCTestSupport
extends RefCounted

## The two strings that identify the benign SCTP reset, both required.
const SCTP_RESET_MARKERS: Array[String] = [
	"SctpTransport::sendReset",
	"errno=2",
]

## Erases a recorded error the running framework would otherwise report as a
## failure, as [code]func(Array[String]) -> void[/code].
##
## Injected the way [member NetwGameHarness.reporter] is, because reaching into
## a framework's error monitor is the framework adapter's business and this file
## runs under any framework or none. Left unassigned it erases nothing, which is
## the right answer for a caller whose framework records no errors.
static var erase_benign_error: Callable = Callable()


## Drops the benign native SCTP reset the running framework would otherwise
## report as a failure. Call it right after WebRTC teardown.
static func clear_optional_sctp_reset_error() -> void:
	if not erase_benign_error.is_valid():
		return
	await erase_benign_error.call(SCTP_RESET_MARKERS)
