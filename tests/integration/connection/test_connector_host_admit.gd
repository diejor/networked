## Guards the ConnectBrowser host path: hosting through the [NetwConnect] facade
## with a [JoinPayload] admits the host into its lobby scene and presents that
## scene, even when the lobby is a roster with no player entity.
##
## Regression guard for the browser-host case where a listen host was admitted
## to the lobby but [ParticipantDisplaySource] fell back to the first active
## world because no local player entity existed, leaving the host on an empty
## world instead of the lobby.
@tool
class_name TestConnectorHostAdmit
extends NetwTestSuite

const MAIN := preload("res://examples/bomber/main.tscn")


func test_facade_host_presents_lobby_without_player_entity() -> void:
	var main := MAIN.instantiate()
	add_child(main)
	var tree := main.get_node("MultiplayerTree") as MultiplayerTree
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

	var sm: MultiplayerSceneManager = tree.get_service(MultiplayerSceneManager)
	var lobby_scene: MultiplayerScene = null
	var world_scene: MultiplayerScene = null
	if sm:
		lobby_scene = sm.active_scenes.get(&"Lobby")
		world_scene = sm.active_scenes.get(&"World")

	# The host participant is admitted to the lobby, and the lobby is a roster
	# with no player entity.
	assert_object(lobby_scene).is_not_null()
	assert_int(tree.get_participants().size()).is_equal(1)
	assert_int(lobby_scene.participants.size()).is_equal(1)
	assert_object(tree.local_player).is_null()

	# The host window presents the lobby it was admitted to, not the first world.
	var ds := ParticipantDisplaySource.new()
	ds.configure(tree)
	ds.refresh()
	var resolved: SubViewport = ds.current
	ds.dispose()
	assert_bool(resolved == (lobby_scene as Node as SubViewport)) \
			.override_failure_message(
					"host display resolved to the world, not the lobby") \
			.is_true()
	assert_bool(resolved == (world_scene as Node as SubViewport)).is_false()

	main.queue_free()
	await get_tree().process_frame
