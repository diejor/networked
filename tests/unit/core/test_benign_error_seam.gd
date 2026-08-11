## The seam that lets the kit tolerate a known-harmless native error without
## naming a test framework.
##
## Everything under [code]addons/networked_test/harness/[/code] runs under any
## framework or none, so the one operation that reaches into GdUnit4's error
## monitor lives in the adapter and arrives by injection. That buys containment
## and costs a failure mode the direct call did not have: nothing anywhere
## errors if the injection is simply never made, and the symptom is a suite that
## goes red months later on a machine where the tolerated error actually fires.
##
## These cases are that failure mode's only alarm. The suite the seam serves
## cannot raise it, because the error it exists to erase does not occur on every
## machine, so a green WebRTC run is not evidence that the seam is wired.
class_name TestBenignErrorSeam
extends NetwTestSuite


## The session hook installs the eraser, so any case in any suite has it.
func test_the_session_hook_installed_the_eraser() -> void:
	assert_bool(WebRTCTestSupport.erase_benign_error.is_valid()).is_true()


## The end-to-end proof: an error matching every marker is recorded by the
## framework and then erased, so the case ends clean despite having raised one.
## Without the injection this case fails on the error it planted.
func test_an_error_matching_every_marker_is_erased() -> void:
	push_error("SctpTransport::sendReset failed, errno=2")

	await WebRTCTestSupport.clear_optional_sctp_reset_error()

	assert_bool(true).is_true()


## Erasure is narrow by construction: every marker has to appear before an entry
## is dropped, so widening what the kit tolerates means adding a marker rather
## than loosening a pattern.
func test_the_tolerated_error_is_named_by_every_marker() -> void:
	assert_int(WebRTCTestSupport.SCTP_RESET_MARKERS.size()).is_greater(1)
