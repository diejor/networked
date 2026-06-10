## Static helpers for WebRTC session integration tests.
##
## The helper embodies the [NetwHarnessSession.BackendAdapter] shape. It is
## kept static until a second WebRTC harness consumer needs an adapter instance.
## [br][br]
## [NetwTestHarness] is built around [LocalLoopbackBackend] and does not
## generalize to a real [WebRTCSession]. This helper mirrors [EnetTestSupport]
## for the complementary case. It hosts and joins real [MultiplayerTree]s over
## a [PairedWebRTCBackend], so the WebRTC handshake runs over loopback ICE with
## signaling shortcut in process. No trackers or sockets are touched.
## [codeblock]
## var host := await WebRTCTestSupport.start_host(self)
## var client := WebRTCTestSupport.make_client_tree(self)
## var target := WebRTCTestSupport.make_join_target(client, host.room)
## await client.join(target, payload)
## [/codeblock]
class_name WebRTCTestSupport
extends RefCounted

## Builds and hosts a [MultiplayerTree] backed by [PairedWebRTCBackend].
##
## Returns a dictionary with [code]tree[/code] (the [MultiplayerTree]),
## [code]backend[/code] (the host backend the tree duplicated), and
## [code]room[/code] (the generated room id clients join with).
static func start_host(parent: Node) -> Dictionary:
	var tree := MultiplayerTree.new()
	tree.name = "WebRTCHost"
	tree.auto_host_headless = false
	tree.backend = _make_backend()
	parent.add_child(tree)

	var err: Error = await tree.host(true)
	if err != OK:
		push_error("WebRTCTestSupport: host failed: %s" % error_string(err))
		tree.queue_free()
		return { }
	return {
		tree = tree,
		backend = tree.backend,
		room = tree.backend.get_join_address(),
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
	tree.backend = _make_backend()
	parent.add_child(tree)
	return tree


## Builds a [JoinTarget] pointing [param client] at [param room].
static func make_join_target(
		client: MultiplayerTree,
		room: String,
) -> JoinTarget:
	var target := JoinTarget.new()
	target.backend = client.backend
	target.address = room
	return target


## Tears down [param tree] and drains the SceneTree so its peer is released
## before the next test begins.
static func stop_tree(tree: MultiplayerTree) -> void:
	if not is_instance_valid(tree):
		return
	var scene_tree := tree.get_tree()
	if scene_tree and tree.backend is WebRTCBackend:
		# Let the join handshake's trailing reliable RPCs flush over open
		# channels before resetting the SCTP streams. Closing first leaves
		# api.poll() dispatching request_join_player into a closed channel
		# whenever the host has not finished the handshake yet, which is the
		# slow-machine ordering CI hits.
		await NetwTestSuite.drain_frames(scene_tree, 8)
		(tree.backend as WebRTCBackend).close_channels()
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


# Offline ice_servers keep the loopback handshake from reaching the network.
static func _make_backend() -> PairedWebRTCBackend:
	var backend := PairedWebRTCBackend.new()
	backend.ice_servers = []
	return backend
