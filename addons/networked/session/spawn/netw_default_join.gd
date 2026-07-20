## The built-in join handler, resolved when no [method Netw.configure_join]
## registration or per-session override is present.
##
## It routes a joining player into a managed scene and spawns them at the player
## template the client named. The wire schema is [method spawn]'s parameters
## after the framework-owned [ResolvedJoin], so a client sends
## [code](scene_name, spawner_path)[/code] as typed join args and the server
## reflects the same two parameters to decode them. Games that want a different
## contract register their own handler with [method Netw.configure_join].
## [codeblock]
## # Client, from a picker selection.
## payload.arg_values = NetwDefaultJoin.args_from_scene_node_path(picked)
## [/codeblock]
class_name NetwDefaultJoin
extends RefCounted

# A weakref because the session that constructs this holds it strongly and the
# api holds the session strongly, so a strong back-reference would cycle and leak.
var _api_ref: WeakRef


func _init(api: NetwMultiplayer) -> void:
	_api_ref = weakref(api)


## Builds the default join args from a [SceneNodePath] picker selection: the
## scene basename and the in-scene template path [method spawn] reads back.
static func args_from_scene_node_path(snp: SceneNodePath) -> Array:
	if snp == null:
		return []
	return [StringName(snp.get_scene_name()), NodePath(snp.node_path)]


## Activates [param scene_name], resolves the template at [param spawner_path],
## and adds the accepted player for [param rj], returning the entered
## [MultiplayerScene].
##
## [br][br][b]Server Only.[/b]
func spawn(
		rj: ResolvedJoin,
		scene_name: StringName,
		spawner_path: NodePath,
) -> MultiplayerScene:
	if scene_name.is_empty():
		return null
	var api := _api_ref.get_ref() as NetwMultiplayer
	if api == null:
		return null
	var scene := await api.scenes.activate_scene(scene_name)
	assert(scene, "activate_scene must guarantee scene presence")

	var node := scene.level.get_node(spawner_path)
	var entity := NetwEntity.ensure(node)
	assert(
		entity,
		"join args' spawner_path didn't resolve to a template node",
	)
	var participant := api.participant(rj.peer_id)
	assert(participant, "spawn requires an accepted participant")
	var player := entity.instantiate_player(participant)
	var target_scene := await api.scenes.resolve_hydrated_spawn_scene(
		player, scene,
	)
	target_scene.add_player(NetwEntity.of(player))
	return target_scene
