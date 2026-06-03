@tool
class_name SpawnerComponentPolicy
extends SpawnPolicy
## The default [SpawnPolicy]. Routes a joining player into a managed scene and
## spawns them at the [SpawnerComponent] the client picked.
##
## [br][br]
## The target is a single [member spawn_point] picked in the inspector. The
## client serializes it through [method to_dict] and the server reads it back in
## [method spawn]. See [SpawnPolicy] for the client and server split.

## The [SpawnerComponent] a joining player spawns at, picked in the inspector.
## [method to_dict] splits it into the scene basename and the in scene node path
## that [method spawn] reads back.
@export_custom(PROPERTY_HINT_RESOURCE_TYPE, "SceneNodePath:SpawnerComponent")
var spawn_point: SceneNodePath


## Builds a policy from a [SceneNodePath] picker selection, ready to serialize
## into the join payload.
## [codeblock]
## # Client: a connect popup picked a spawner.
## var policy := SpawnerComponentPolicy.from_scene_node_path(picked)
## payload.spawn = policy.to_dict()
## [/codeblock]
static func from_scene_node_path(path: SceneNodePath) -> SpawnerComponentPolicy:
	var policy := SpawnerComponentPolicy.new()
	policy.spawn_point = path
	return policy


func to_dict() -> Dictionary:
	if spawn_point == null:
		return { }
	return {
		"scene_name": StringName(spawn_point.get_scene_name()),
		"spawner_path": NodePath(spawn_point.node_path),
	}


func spawn(rj: ResolvedJoin, ctx: NetwContext) -> MultiplayerScene:
	var target_scene_name := StringName(rj.spawn.get("scene_name", &""))
	var target_spawner_path: NodePath = rj.spawn.get("spawner_path", NodePath())
	if target_scene_name.is_empty():
		return null

	var mgr := ctx.services.get_scene_manager()
	var scene := await mgr.activate_scene(target_scene_name)
	assert(scene, "activate_scene must guarantee scene presence")

	var spawner := _spawner_in(scene, target_spawner_path)
	var player := spawner.instantiate_player(rj)
	var target_scene := await mgr._resolve_hydrated_spawn_scene(player, scene)
	target_scene.add_player(player)
	return target_scene


func _spawner_in(scene: MultiplayerScene, path: NodePath) -> SpawnerComponent:
	var node := scene.level.get_node(path)
	assert(
		node is SpawnerComponent,
		"spawn payload's spawner_path didn't point at a SpawnerComponent",
	)
	return node
