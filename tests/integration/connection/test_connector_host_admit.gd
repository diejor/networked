## Guards the ConnectBrowser host path: hosting through
## [method NetwConnector.host] with a [JoinPayload] admits the host into its
## lobby scene and presents that scene, even when the lobby is a roster with no
## player entity.
@tool
class_name TestConnectorHostAdmit
extends NetwTestSuite

const MAIN := preload("res://examples/bomber/main.tscn")


func test_session_host_presents_lobby_without_player_entity() -> void:
	# The bomber main is tree-less, so the test wraps it in its own tree the
	# way the game harness does for a scoped embedding.
	var tree := MultiplayerTree.new()
	add_child(tree)
	var main := MAIN.instantiate()
	tree.add_child(main)
	await get_tree().process_frame

	var browser := NetwServerBrowser.new(tree.api)
	# Mirror the host popup: default to the first hostable transport, no params.
	var hostable := browser.hostable_transports()
	var config := NetwHostConfig.new()
	config.transport = hostable[0]._params_from_dict({ }) if not hostable.is_empty() \
	else tree.transport

	var payload := JoinPayload.new()
	payload.username = "HostPlayer"
	await NetwConnector.of(tree.api).host(payload, config)

	for i in 40:
		await get_tree().process_frame

	var lobby_scene := tree.api.scene(&"Lobby")

	# The host participant is admitted to the lobby, and the lobby is a roster
	# with no player entity.
	assert_object(lobby_scene).is_not_null()
	assert_int(tree.api.participants.size()).is_equal(1)
	assert_int(lobby_scene.participants.size()).is_equal(1)
	assert_object(tree.api.local_player).is_null()

	# The host presents the lobby it was admitted to. A session has no current
	# scene of its own, so the presentation fact to pin is where the local
	# participant is.
	assert_object(tree.api.local_participant.current_scene).is_same(lobby_scene)

	tree.queue_free()
	await get_tree().process_frame
