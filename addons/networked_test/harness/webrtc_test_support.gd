## Static helpers for WebRTC session integration tests.
##
## The helper embodies the [NetwHarnessSession.BackendAdapter] shape. It is
## kept static until a second WebRTC harness consumer needs an adapter instance.
## [br][br]
## [NetwTestHarness] is built around the [LocalTransport] and does not
## generalize to a real [WebRTCSession]. This helper mirrors [EnetTestSupport]
## for the complementary case, hosting and joining real [MultiplayerTree]s over
## the [code]&"webrtc"[/code] scheme so the [WebRTCSession] handshake runs end to
## end through [MultiplayerTree].
## [codeblock]
## var host := await WebRTCTestSupport.start_host(self)
## var client := WebRTCTestSupport.make_client_tree(self)
## var target := WebRTCTestSupport.make_join_target(client, host.room)
## await client.join(target, payload)
## [/codeblock]
class_name WebRTCTestSupport
extends RefCounted

## Builds and hosts a [MultiplayerTree] backed by paired WebRTC.
##
## Returns a dictionary with [code]tree[/code] (the [MultiplayerTree]),
## [code]room[/code] (the generated room id clients join with).
static func start_host(parent: Node) -> Dictionary:
	var tree := MultiplayerTree.new()
	tree.name = "WebRTCHost"
	tree.auto_host_headless = false
	tree.scheme = &"webrtc"
	parent.add_child(tree)
	_install_paired_transport(tree)

	var err: Error = await tree._open_host(true)
	if err != OK:
		push_error("WebRTCTestSupport: host failed: %s" % error_string(err))
		tree.queue_free()
		return { }
	var room := ""
	var view := tree.connector.peer_view if tree.connector else null
	if view:
		room = view.join_address()
	return {
		tree = tree,
		room = room,
	}


## Builds an offline client [MultiplayerTree] wired with a paired WebRTC
## backend. The tree is added under [param parent] but has not connected.
static func make_client_tree(
		parent: Node,
		name_suffix: String = "",
) -> MultiplayerTree:
	var tree := MultiplayerTree.new()
	tree.name = "WebRTCClient%s" % name_suffix
	tree.auto_host_headless = false
	tree.scheme = &"webrtc"
	parent.add_child(tree)
	_install_paired_transport(tree)
	return tree


# Overrides tree's connector to signal through the in-process
# PairedWebRTCSignaler instead of the shipped WebTorrent tracker, so the real
# WebRTCSession handshake runs with no tracker or socket.
static func _install_paired_transport(tree: MultiplayerTree) -> void:
	tree.connector.transports = [PairedWebRTCTransport.new()]


## Builds a [NetwConnectTarget] pointing [param client] at [param room].
static func make_join_target(
		client: MultiplayerTree,
		room: String,
) -> NetwConnectTarget:
	var target := NetwConnectTarget.new()
	target.scheme = client.scheme
	target.address = room
	return target


## Tears down [param tree] and drains the SceneTree so its peer is released
## before the next test begins.
static func stop_tree(tree: MultiplayerTree) -> void:
	if not is_instance_valid(tree):
		return
	var scene_tree := tree.get_tree()
	var view := tree.connector.peer_view if tree.connector else null
	if scene_tree and view:
		# Let the join handshake's trailing reliable RPCs flush over open
		# channels before resetting the SCTP streams.
		await NetwTestSuite.drain_frames(scene_tree, 8)
		view.close()
		await NetwTestSuite.drain_frames(scene_tree)
	tree.queue_free()
	if scene_tree:
		for i in 3:
			await scene_tree.process_frame
	await clear_optional_sctp_reset_error()


## Detaches every outward signal subscriber from [param session], then closes
## it.
##
## A recovery test cross-wires two raw [WebRTCSession]s by forwarding each one's
## [signal WebRTCSession.signal_out] into the other's
## [method WebRTCSession.deliver]. Those connections make the two sessions
## reference each other through their listener closures, a cycle GDScript cannot
## collect. [method WebRTCSession.close] only clears
## [member WebRTCSession.webrtc_peer], so it leaves the cross edges intact.
## Dropping the subscribers here releases both sessions at test end.
static func dispose_session(session: WebRTCSession) -> void:
	if session == null:
		return
	var signals: Array[Signal] = [
		session.signal_out,
		session.native_connected,
		session.native_disconnected,
		session.failed,
	]
	for sig in signals:
		for conn in sig.get_connections():
			sig.disconnect(conn.callable)
	session.close()


## Drops the benign native SCTP reset gdUnit would otherwise report as a
## failure.
##
## Tearing down a connected native [WebRTCSession], either when the retry path
## replaces a stale peer or on close, makes libdatachannel log
## [code]SctpTransport::sendReset ... errno=2[/code] on Linux. That is a
## harmless [code]ENOENT[/code] on an already-gone stream, but
## [code]report/godot/push_error[/code] turns it into a failure. Call this right
## after WebRTC teardown to erase just that entry from the gdUnit error monitor.
static func clear_optional_sctp_reset_error() -> void:
	var monitor := GdUnitThreadManager.get_current_context() \
			.get_execution_context().error_monitor
	var entries: Array[ErrorLogEntry] = await monitor.scan(true)
	for entry: ErrorLogEntry in entries.duplicate():
		if "SctpTransport::sendReset" in entry._message \
				and "errno=2" in entry._message:
			monitor.erase_log_entry(entry)
