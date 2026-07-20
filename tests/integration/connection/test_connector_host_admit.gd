## Guards the ConnectBrowser host path: hosting through the [NetwConnect] facade
## with a [JoinPayload] admits the host into its lobby scene and presents that
## scene, even when the lobby is a roster with no player entity.
@tool
class_name TestConnectorHostAdmit
extends NetwTestSuite

const MAIN := preload("res://examples/bomber/main.tscn")


func test_facade_host_presents_lobby_without_player_entity() -> void:
	# The bomber main is tree-less, so the test wraps it in its own tree the
	# way the game harness does for a scoped embedding.
	var tree := MultiplayerTree.new()
	add_child(tree)
	var main := MAIN.instantiate()
	tree.add_child(main)
	await get_tree().process_frame

	var facade := tree.api.connect
	# Mirror the host popup: default to the first hostable transport, no params.
	var hostable := facade.hostable_transports()
	var config := NetwHostConfig.new()
	config.scheme = hostable[0].scheme() if not hostable.is_empty() else tree.scheme

	var payload := JoinPayload.new()
	payload.username = "HostPlayer"
	await facade.host(config, payload)

	for i in 40:
		await get_tree().process_frame

	var scenes := tree.api.scenes
	var lobby_scene: MultiplayerScene = null
	if scenes:
		lobby_scene = scenes.scene(&"Lobby")

	# The host participant is admitted to the lobby, and the lobby is a roster
	# with no player entity.
	assert_object(lobby_scene).is_not_null()
	assert_int(tree.get_participants().size()).is_equal(1)
	assert_int(lobby_scene.participants.size()).is_equal(1)
	assert_object(tree.local_player).is_null()

	# The host presents the lobby it was admitted to. Bomber declares
	# SINGLE concurrency, so the scene shows natively and current_scene is the
	# presentation fact to pin.
	assert_object(scenes.current_scene).is_equal(lobby_scene)

	tree.queue_free()
	await get_tree().process_frame
