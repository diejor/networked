## DEPRECATED: use [NetwTestHarness] from [code]addons/networked_test/[/code].
## This shim preserves the old class name for tests not yet migrated.
## Removed in Phase F of the test infrastructure refactor.
class_name NetworkTestHarness
extends NetwTestHarness


# Pre-rename method aliases. Each forwards to its new name on the parent class.

func get_server() -> MultiplayerTree:
	return server()


func get_all_clients() -> Array[MultiplayerTree]:
	return clients()


func get_session() -> LocalLoopbackSession:
	return session()


func get_server_scene(scene_name: StringName = "") -> MultiplayerScene:
	return scene_on_server(scene_name)


func client_player_name(client: MultiplayerTree) -> StringName:
	return player_name_for(client)


func wait_for_client_scene_spawn(client: MultiplayerTree, scene_name: StringName) -> MultiplayerScene:
	return await wait_for_scene(client, scene_name)


func wait_for_client_player_spawn(
	client: MultiplayerTree,
	scene_name: StringName,
	player_name: StringName = &"",
) -> Node:
	return await wait_for_player(client, scene_name, player_name)
