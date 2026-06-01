## Verifies [WebRTCBackend] does not run the same-port auth probe.
##
## WebRTC discovery is tracker-based; inheriting the default
## [SceneMultiplayer] probe would force a full ICE handshake on every
## server-browser refresh. The backend must report
## [constant ServerInfoResult.Status.UNSUPPORTED] immediately and open no
## tracker sockets.
extends GdUnitTestSuite


func test_query_returns_unsupported_without_signaling() -> void:
	var backend := WebRTCBackend.new()

	var start_ms := Time.get_ticks_msec()
	var result: ServerInfoResult = backend.query_server_info(
		"deadbeefdeadbeefdead", 2.0
	)
	var elapsed_ms := Time.get_ticks_msec() - start_ms

	assert_int(result.status).is_equal(ServerInfoResult.Status.UNSUPPORTED)
	# Returns without touching trackers/ICE, so it is effectively instant.
	assert_int(elapsed_ms).is_less(1000)
	# No signaling sockets were opened by the probe.
	assert_that(backend._tracker).is_null()
