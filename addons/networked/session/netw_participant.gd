class_name NetwParticipant
extends RefCounted
## Live handle for one accepted session player.
##
## [member join] reads the current [ResolvedJoin] from the owning
## [NetwMultiplayer]. [member current_scene] tracks the participant's primary
## scene membership independently from any spawned player node.

## Emitted when [member current_scene] changes.
signal scene_changed(from: MultiplayerScene, to: MultiplayerScene)

## Peer id represented by this participant.
var peer_id: int

var _api_ref: WeakRef
var _current_scene: MultiplayerScene


func _init(api: NetwMultiplayer, id: int) -> void:
	_api_ref = weakref(api)
	peer_id = id

## Accepted join data for [member peer_id], or [code]null[/code].
var join: ResolvedJoin:
	get:
		var api := _api_ref.get_ref() as NetwMultiplayer
		return api.get_accepted_join(peer_id) if api else null

## Validated auth identity for [member peer_id], or [code]null[/code].
##
## This is only present when the session has an auth provider. It is not part
## of the replicated roster.
var identity: NetwIdentity:
	get:
		var api := _api_ref.get_ref() as NetwMultiplayer
		if not api or not api.has_peer_context(peer_id):
			return null
		var bucket := api.get_peer_context(peer_id).get_bucket(
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

## Primary scene membership for this participant.
var current_scene: MultiplayerScene:
	get:
		return _current_scene if is_instance_valid(_current_scene) else null
	set(value):
		var from := current_scene
		if from == value:
			return
		_current_scene = value
		scene_changed.emit(from, value)


## Moves this participant to [param dest] without touching any player node.
##
## [br][br][b]Server Only.[/b]
func move_to(dest: MultiplayerScene) -> void:
	var api := _api_ref.get_ref() as NetwMultiplayer
	if not api or not is_instance_valid(dest):
		return
	assert(api.is_server(), "NetwParticipant.move_to() must be called on the server.")
	var from := current_scene
	if from == dest:
		return
	current_scene = dest
	if is_instance_valid(from):
		from.release(self)
	dest.admit(self)
