class_name NetwParticipant
extends RefCounted
## Live handle for one accepted session player.
##
## [member join] reads the current [ResolvedJoin] from the owning
## [MultiplayerTree]. [member current_scene] tracks the participant's primary
## scene membership independently from any spawned player node.

## Emitted when [member current_scene] changes.
signal scene_changed(from: NetwScene, to: NetwScene)

## Peer id represented by this participant.
var peer_id: int

var _tree_ref: WeakRef
var _current_scene: NetwScene


func _init(mt: MultiplayerTree, id: int) -> void:
	_tree_ref = weakref(mt)
	peer_id = id

## Accepted join data for [member peer_id], or [code]null[/code].
var join: ResolvedJoin:
	get:
		var mt := _tree_ref.get_ref() as MultiplayerTree
		return mt._get_accepted_join(peer_id) if mt else null

## Validated auth identity for [member peer_id], or [code]null[/code].
##
## This is only present when the session has an auth provider. It is not part
## of the replicated roster.
var identity: NetwIdentity:
	get:
		var mt := _tree_ref.get_ref() as MultiplayerTree
		if not mt or not mt.has_peer_context(peer_id):
			return null
		var bucket := mt.get_peer_context(peer_id).get_bucket(
			NetwIdentityBucket,
		)
		return bucket.identity

## Accepted username for [member peer_id].
var username: StringName:
	get:
		var rj := join
		return rj.username if rj else &""

## Serialized spawn data for [member peer_id].
var spawn: Dictionary:
	get:
		var rj := join
		return rj.spawn if rj else { }

## Returns [code]true[/code] when the accepted join was a debug join.
var is_debug: bool:
	get:
		var rj := join
		return rj.is_debug if rj else false

## Primary scene membership for this participant.
var current_scene: NetwScene:
	get:
		if _current_scene and _current_scene.is_valid():
			return _current_scene
		return null
	set(value):
		var from := current_scene
		if _same_scene(from, value):
			return
		_current_scene = value
		scene_changed.emit(from, value)


## Moves this participant to [param dest] without touching any player node.
##
## [br][br][b]Server Only.[/b]
func move_to(dest: NetwScene) -> void:
	var mt := _tree_ref.get_ref() as MultiplayerTree
	if not mt or not dest or not dest.is_valid():
		return
	assert(mt.is_host, "NetwParticipant.move_to() must be called on the server.")
	var from := current_scene
	if from == dest or (from and from.unwrap() == dest.unwrap()):
		return
	current_scene = dest
	if from and from.is_valid():
		from.release(self)
	dest.admit(self)


func _same_scene(a: NetwScene, b: NetwScene) -> bool:
	if a == b:
		return true
	if a == null or b == null:
		return false
	return a.unwrap() == b.unwrap()
