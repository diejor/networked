class_name NetwParticipant
extends RefCounted
## Live handle for one accepted session player.
##
## [member join] reads the current [ResolvedJoin] from the owning
## [NetwMultiplayer]. [member current_scene] tracks the participant's primary
## scene membership independently from any spawned player node.

## Emitted when [member current_scene] changes.
signal scene_changed(from: NetwSceneHandle, to: NetwSceneHandle)

## Peer id represented by this participant.
var peer_id: int

var _api_ref: WeakRef
# The scene's entity RID rather than its handle, so membership survives the
# container being rebuilt and never pins a freed node.
var _current_scene := RID()


func _init(api: NetwMultiplayer, id: int) -> void:
	_api_ref = weakref(api)
	peer_id = id

## Accepted join data for [member peer_id], or [code]null[/code].
var join: ResolvedJoin:
	get:
		var api := _api_ref.get_ref() as NetwMultiplayer
		return api.peer_get_accepted_join(peer_id) if api else null

## Validated auth identity for [member peer_id], or [code]null[/code].
##
## This is only present when the session has an auth provider. It is not part
## of the replicated roster.
var identity: NetwIdentity:
	get:
		var api := _api_ref.get_ref() as NetwMultiplayer
		if not api or not api.peer_has_context(peer_id):
			return null
		var bucket := api.peer_get_context(peer_id).get_bucket(
			NetwIdentityBucket,
		)
		return bucket.identity

## Accepted username for [member peer_id].
var username: StringName:
	get:
		var rj := join
		return rj.username if rj else &""

## Typed join args accepted for [member peer_id]. See
## [member ResolvedJoin.arg_values].
var arg_values: Array:
	get:
		var rj := join
		return rj.arg_values if rj else []

## Returns [code]true[/code] when the accepted join was a debug join.
var is_debug: bool:
	get:
		var rj := join
		return rj.is_debug if rj else false

## Primary scene membership, or [code]null[/code] outside every scene.
##
## A participant is admitted to one scene at a time, so this is a scalar even
## though the mechanism underneath it is not. Assigning it records membership
## without admitting anyone: [method move_to] is the verb that also moves the
## admission edge.
var current_scene: NetwSceneHandle:
	get:
		var record := _scene_record()
		return record.scene if record else null
	set(value):
		var next := value.entity if value else RID()
		if _current_scene == next:
			return
		var from := current_scene
		_current_scene = next
		scene_changed.emit(from, current_scene)


## Moves this participant to [param dest] without touching any player node.
##
## [br][br][b]Server Only.[/b]
func move_to(dest: NetwSceneHandle) -> void:
	var api := _api_ref.get_ref() as NetwMultiplayer
	if not api or dest == null or not dest.is_declared:
		return
	assert(api.is_server(), "NetwParticipant.move_to() must be called on the server.")
	var from := current_scene
	if from == dest:
		return
	current_scene = dest
	if from != null:
		from.release(self)
	dest.admit(self)


# The scene entity's record while it is alive, or null.
func _scene_record() -> NetwEntity:
	var api := _api_ref.get_ref() as NetwMultiplayer
	if api == null or not _current_scene.is_valid():
		return null
	return NetwEntity.of(api.entity_get_node(_current_scene))
