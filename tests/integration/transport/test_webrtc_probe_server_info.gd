## Verifies [WebRTCBackend] does not run the same-port auth probe.
##
## WebRTC discovery is tracker-based; inheriting the default
## [SceneMultiplayer] probe would force a full ICE handshake on every
## server-browser refresh. The backend must report
## [constant BackendPeer.ProbeResult.Status.UNSUPPORTED] immediately and open no
## tracker sockets.
class_name TestWebRTCProbeServerInfo
extends NetwTestSuite

func test_query_returns_unsupported_without_signaling() -> void:
	var backend := TrackerWebRTCBackend.new()

	var start_ms := Time.get_ticks_msec()
	var result: BackendPeer.ProbeResult = backend.probe_server_info(
		"deadbeefdeadbeefdead",
		2.0,
	)
	var elapsed_ms := Time.get_ticks_msec() - start_ms

	assert_int(result.status).is_equal(BackendPeer.ProbeResult.Status.UNSUPPORTED)
	# Returns without touching trackers/ICE, so it is effectively instant.
	assert_int(elapsed_ms).is_less(1000)
	# No signaler or session was built by the probe.
	assert_that(backend._signaler).is_null()
	assert_that(backend._session).is_null()
