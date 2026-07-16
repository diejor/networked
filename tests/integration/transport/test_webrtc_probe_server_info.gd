## Verifies [TrackerWebRTCTransport] does not run the same-port auth probe.
##
## WebRTC discovery is tracker-based; inheriting the default same-port probe
## would force a full ICE handshake on every server-browser refresh. The
## transport must report [constant NetwProbeResult.Status.UNSUPPORTED]
## immediately and open no tracker sockets.
class_name TestWebRTCProbeServerInfo
extends NetwTestSuite

func test_query_returns_unsupported_without_signaling() -> void:
	var transport := TrackerWebRTCTransport.new()

	var target := NetwConnectTarget.new()
	target.scheme = &"webrtc"
	target.address = "deadbeefdeadbeefdead"

	var start_ms := Time.get_ticks_msec()
	var result: NetwProbeResult = await transport._probe(target)
	var elapsed_ms := Time.get_ticks_msec() - start_ms

	assert_int(result.status).is_equal(NetwProbeResult.Status.UNSUPPORTED)
	# Returns without touching trackers/ICE, so it is effectively instant.
	assert_int(elapsed_ms).is_less(1000)
