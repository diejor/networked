## Proves the [WebRTCSession] is signaling-independent.
##
## A host and a client reach a native WebRTC connection over loopback ICE while
## a [PairedWebRTCSignaler] shortcuts signaling in process. No WebTorrent
## tracker is involved, so a green run shows the session works behind any
## [WebRTCSignaler]. The handshake runs end to end through [MultiplayerTree].
class_name TestWebRTCSessionPaired
extends NetwTestSuite

func _payload(username: String) -> JoinPayload:
	var payload := JoinPayload.new()
	payload.username = username
	return payload


func test_paired_signaler_reaches_native_connection() -> void:
	var host := await WebRTCTestSupport.start_host(self)
	assert_that(host).is_not_empty()

	var client := WebRTCTestSupport.make_client_tree(self, "_join")
	var target := WebRTCTestSupport.make_join_target(client, host.room)

	var err: Error = await client.join(target, _payload("valeria"))

	assert_int(err).is_equal(OK)
	assert_bool(client.is_online()).is_true()
	assert_int(client.role).is_equal(MultiplayerTree.Role.CLIENT)

	var res := client.last_connect_result
	assert_that(res).is_not_null()
	assert_bool(res.is_ok()).is_true()
	var diags := res.diagnostics
	assert_bool(diags.get("relay_used", true)).is_false()
	var stats: Dictionary = diags.get("candidates", { })
	assert_int(int(stats.get("host", 0))).is_greater(0)

	# The host sees the client over the native WebRTC link.
	var host_tree := host.tree as MultiplayerTree
	assert_int(host_tree.api.get_peers().size()).is_greater(0)

	await WebRTCTestSupport.stop_tree(client)
	await WebRTCTestSupport.stop_tree(host_tree)
