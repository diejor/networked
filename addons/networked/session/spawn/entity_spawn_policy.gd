@tool
class_name EntitySpawnPolicy
extends SpawnPolicy
## The default [SpawnPolicy]. Routes a joining player into a managed scene and
## spawns them at the [MultiplayerEntity] the client picked.
##
## [br][br]
## [member spawn_point] is the join contract. It must point at a
## [MultiplayerEntity] inside a scene managed by [MultiplayerSceneManager].
## The client serializes it through [method to_dict]. The server reads it in
## [method spawn], activates the [MultiplayerScene], calls
## [method MultiplayerEntity.spawn_player], and finishes with
## [method MultiplayerScene.add_player].
## [codeblock]
## var policy := EntitySpawnPolicy.from_scene_node_path(spawn_point)
## payload.spawn = policy.to_dict()
## [/codeblock]
##
## Use [EntitySpawnPolicy] when players should enter through a declared scene
## spawn point. Use [NetwEntity] directly only when gameplay owns the lower
## level [MultiplayerSpawner] identity path.

## The [MultiplayerEntity] a joining player spawns at, picked in the inspector.
## [method to_dict] splits it into the scene basename and the in scene node path
## that [method spawn] reads back.
@export_custom(PROPERTY_HINT_RESOURCE_TYPE, "SceneNodePath:MultiplayerEntity")
var spawn_point: SceneNodePath


## Builds a policy from a [SceneNodePath] picker selection, ready to serialize
## into the join payload.
## [codeblock]
## # Client: a connect popup picked a spawner.
## var policy := EntitySpawnPolicy.from_scene_node_path(picked)
## payload.spawn = policy.to_dict()
## [/codeblock]
static func from_scene_node_path(path: SceneNodePath) -> EntitySpawnPolicy:
	var policy := EntitySpawnPolicy.new()
	policy.spawn_point = path
	return policy


func to_dict() -> Dictionary:
	assert(
		spawn_point != null,
		"assign spawn_point before using ConnectBrowser or joining",
	)
	if spawn_point == null:
		return with_policy_script({ })
	return with_policy_script(
		{
			"scene_name": StringName(spawn_point.get_scene_name()),
			"spawner_path": NodePath(spawn_point.node_path),
		},
	)


func spawn(rj: ResolvedJoin, ctx: NetwContext) -> MultiplayerScene:
	var target_scene_name := StringName(rj.spawn.get("scene_name", &""))
	var target_spawner_path: NodePath = rj.spawn.get("spawner_path", NodePath())
	assert(
		not target_scene_name.is_empty(),
		(
				"client sent no spawn intent. Sender and receiver are using " +
				"different SpawnPolicies"
		),
	)
	if target_scene_name.is_empty():
		return null

	var mgr := ctx.services.scene_manager
	var scene := await mgr.activate_scene(target_scene_name)
	assert(scene, "activate_scene must guarantee scene presence")

	var entity := _entity_in(scene, target_spawner_path)
	var participant := ctx.tree.participant(rj.peer_id)
	assert(participant, "spawn requires an accepted participant")
	var player := entity.instantiate_player(participant)
	var target_scene := await mgr._resolve_hydrated_spawn_scene(player, scene)
	target_scene.add_player(NetwEntity.of(player))
	return target_scene


func _entity_in(scene: MultiplayerScene, path: NodePath) -> MultiplayerEntity:
	var node := scene.level.get_node(path)
	assert(
		node is MultiplayerEntity,
		"spawn payload's spawner_path didn't point at a MultiplayerEntity",
	)
	return node
