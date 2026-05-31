@tool
class_name SpawnerComponentPolicy
extends SpawnPolicy

## Default [SpawnPolicy]: routes a joining player into a managed scene and
## spawns them at a [SpawnerComponent] the client selected. Reproduces the
## addon's classic "join auto-spawns a player" behavior.
##
## The client authors the target by serializing this policy's
## [method to_dict] into [member JoinPayload.spawn] (see
## [method from_scene_node_path]). The server's configured instance is
## stateless: [method spawn] reads the target from [member ResolvedJoin.spawn].

## Target scene basename (e.g. [code]&"Level1"[/code]). Used only for
## client-side authoring of the [method to_dict] payload.
@export var scene_name: StringName

## Path to the [SpawnerComponent] within the target scene. Used only for
## client-side authoring of the [method to_dict] payload.
@export var spawner_path: NodePath


## Builds an authoring policy from a [SceneNodePath] picker selection.
static func from_scene_node_path(path: SceneNodePath) -> SpawnerComponentPolicy:
	var policy := SpawnerComponentPolicy.new()
	if path:
		policy.scene_name = StringName(path.get_scene_name())
		policy.spawner_path = path.node_path
	return policy


func to_dict() -> Dictionary:
	return {
		"scene_name": scene_name,
		"spawner_path": spawner_path,
	}


func spawn(rj: ResolvedJoin, mgr: MultiplayerSceneManager) -> void:
	var target_scene_name := StringName(rj.spawn.get("scene_name", &""))
	var target_spawner_path: NodePath = rj.spawn.get("spawner_path", NodePath())
	if target_scene_name.is_empty():
		return

	await mgr.activate_scene(target_scene_name)
	var scene := mgr.active_scenes.get(target_scene_name) as MultiplayerScene
	assert(scene, "activate_scene must guarantee scene presence")

	var spawner := _spawner_in(scene, target_spawner_path)
	var player := spawner.instantiate_player(rj)
	var target_scene := await mgr._resolve_hydrated_spawn_scene(player, scene)
	target_scene.add_player(player)

	var tree := MultiplayerTree.for_node(mgr)
	if tree:
		tree.player_scene_ready.emit(rj, target_scene)


func _spawner_in(scene: MultiplayerScene, path: NodePath) -> SpawnerComponent:
	var node := scene.level.get_node(path)
	assert(node is SpawnerComponent,
		"spawn payload's spawner_path didn't point at a SpawnerComponent")
	return node
