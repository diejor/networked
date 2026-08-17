## The scene an entity belongs to, reached as [member NetwEntity.scene].
##
## A pure view over one entity RID: it holds no authoritative state, so it can
## be reconstructed at any moment. One scene has one handle, so [code]==[/code]
## answers "the same scene". Never [code]null[/code] — an entity that declares
## itself a scene resolves to itself, and any other entity resolves to its
## nearest scene ancestor. Ask [member is_declared] when the difference matters.
## [codeblock]
## var scene := NetwEntity.of(self).scene
## if scene.is_declared:
##     scene.admit(participant)
##     for player in scene.players:
##         greet(player)
## [/codeblock]
## This is the wrapper tier of the scene surface. It takes and returns wrappers
## and nodes, while the same operations over RIDs live on the machine tier as
## the [method NetwMultiplayer.scene_admit] family.
class_name NetwSceneHandle
extends RefCounted

# Marks a node as a scene container, so an off-session walk can find one
# without a class to check against. The session stamps it when it builds the
# container, and this reads the name from there rather than spelling it twice.
static var _SCENE_META: StringName = NetwMultiplayerCore.scene_container_meta()

var _entity_ref: WeakRef


# Binds the handle once to the entity that owns it.
func _bind(entity: NetwEntity) -> void:
	assert(entity != null, "NetwSceneHandle._bind: entity is null")
	var current := _entity()
	assert(
		current == null or current == entity,
		"NetwSceneHandle cannot be rebound to another entity",
	)
	_entity_ref = weakref(entity)

## The resolved scene's entity RID, or an invalid RID outside a session.
##
## Resolution is self-inclusive and walks the parent-entity chain, so it follows
## the tree and self-heals on reparent without anything re-enrolling the entity.
var entity: RID:
	get:
		var own := _entity()
		if own == null or not is_instance_valid(own.owner):
			return RID()
		var core := NetwEntity.session_plane_for(own.owner)
		if core == null:
			return RID()
		return core.entity_scene_of(core.entity_of(own.owner))


# The container this handle resolves to. Off-session there is no RID to walk,
# so it falls back to the node walk, which is what makes the handle answer the
# same on a bare tree as it does inside a live session.
func _resolve_container() -> Node:
	var api := _api()
	if api != null:
		var resolved := entity
		if resolved.is_valid():
			return api.entity_get_node(resolved)
	var own := _entity()
	if own == null or not is_instance_valid(own.owner):
		return null
	var walker := own.owner
	while walker != null:
		if walker.has_meta(_SCENE_META):
			return walker
		walker = walker.get_parent()
	return null

## Whether a scene resolved at all. [code]false[/code] for an entity outside
## every scene, where every other member reads empty.
var is_declared: bool:
	get:
		return entity.is_valid() or _resolve_container() != null

## The resolved scene's non-unique stem. See [member NetwEntity.scene_label].
##
## Falls back to the content root's name when no script declared one, so this is
## always the string [method NetwMultiplayer.scene_find] answers to.
var label: StringName:
	get:
		var own := _entity()
		var core := NetwEntity.session_plane_for(own.owner) \
		if own and is_instance_valid(own.owner) else null
		if core == null:
			var node := _scene_node()
			var found := NetwEntity.of(node) if node else null
			return found.scene_label if found else &""
		return core.scene_stem(entity)

## The scene's own [NetwEntity], or [code]null[/code] when none resolved.
##
## A scene is an ordinary entity, so everything an entity carries is reached
## through here rather than through a scene-shaped copy of it.
## [codeblock]
## NetwEntity.of(self).scene.record.prediction.island.from_interest()
## [/codeblock]
var record: NetwEntity:
	get:
		var own := _entity()
		var core := NetwEntity.session_plane_for(own.owner) \
		if own and is_instance_valid(own.owner) else null
		if core != null:
			var resolved := core.wrapper_of(entity) as NetwEntity
			if resolved != null:
				return resolved
		var node := _scene_node()
		return NetwEntity.of(node) if node else null

## The scene's content root, or [code]null[/code] for a content-less scene that
## is only an admission boundary.
var level: Node:
	get:
		var node := _scene_node()
		if node == null or node.get_child_count() == 0:
			return null
		return node.get_child(0)

## The scene's admission boundary, or [code]null[/code] when none resolved.
var layer: NetwInterestLayer:
	get:
		var api := _api()
		if api == null:
			return null
		return api._interest.get_layer(api._scene_layer_id(entity))

## Whether the scene hosts its own world.
var isolation: NetwMultiplayer.SceneIsolation:
	get:
		var node := _scene_node()
		var record := NetwEntity.of(node) if node else null
		return record.scene_isolation if record \
		else NetwMultiplayer.SceneIsolation.SCENE_ISOLATION_NONE

## Every peer the scene admits.
var peers: PackedInt32Array:
	get:
		var api := _api()
		return api.scene_get_peers(entity) if api else PackedInt32Array()

## Every participant the scene admits.
var participants: Array[NetwParticipant]:
	get:
		var out: Array[NetwParticipant] = []
		var api := _api()
		if api == null:
			return out
		for peer: int in api.scene_get_peers(entity):
			var participant := api.peer_get_participant(peer)
			if participant:
				out.append(participant)
		return out

## Every entity the scene encloses. See
## [method NetwMultiplayer.scene_get_entities].
var entities: Array[NetwEntity]:
	get:
		var out: Array[NetwEntity] = []
		var api := _api()
		if api == null:
			return out
		for member: RID in api.scene_get_entities(entity):
			var record := api._entity_wrapper(member)
			if record != null and is_instance_valid(record.owner):
				out.append(record)
		return out

## Every player entity inside the scene.
##
## A player is an entity carrying a peer, so this is [member entities] filtered
## rather than a separate roster.
var players: Array[NetwEntity]:
	get:
		var out: Array[NetwEntity] = []
		for record: NetwEntity in entities:
			if record.peer_id != 0:
				out.append(record)
		return out

## The local peer's player entity inside this scene, or [code]null[/code].
var local_player: NetwEntity:
	get:
		var own := _entity()
		var core := NetwEntity.session_plane_for(own.owner) \
		if own and is_instance_valid(own.owner) else null
		if core == null:
			return null
		return player_by_peer(core.get_unique_id())


## Returns whether the scene admits [param peer_id].
func admits(peer_id: int) -> bool:
	var api := _api()
	return api.scene_admits(entity, peer_id) if api else false


## Returns the player entity [param peer_id] controls inside this scene, or
## [code]null[/code].
func player_by_peer(peer_id: int) -> NetwEntity:
	for player: NetwEntity in players:
		if player.peer_id == peer_id:
			return player
	return null


## Admits [param who] to the scene, as a [NetwParticipant] or a peer id.
##
## One verb because admission is one fact. A peer that has not been accepted
## yet has no participant to name, which is the only reason the id form exists.
## [br][br][b]Server Only.[/b]
func admit(who: Variant) -> Error:
	var api := _api()
	var peer := _peer_of(who)
	if api == null or peer == 0:
		return ERR_UNCONFIGURED
	return api.scene_admit(entity, peer)


## Removes [param who] from the scene, as a [NetwParticipant] or a peer id.
## [br][br][b]Server Only.[/b]
func release(who: Variant) -> void:
	var api := _api()
	var peer := _peer_of(who)
	if api and peer != 0:
		api.scene_release(entity, peer)


## Moves every participant in [param moving] into this scene.
##
## The returned [NetwGroupPromise] reports each arrival through
## [signal NetwGroupPromise.completed_single] and settles once the last one has
## landed, which is the same one-to-many handle every other group operation
## answers with.
## [codeblock]
## arena.move_participants(squad) \
##     .then(func(_arrivals: Dictionary) -> void: start_round())
## [/codeblock]
## [br][br][b]Server Only.[/b]
func move_participants(moving: Array[NetwParticipant]) -> NetwGroupPromise:
	var peers: Array[int] = []
	for participant: NetwParticipant in moving:
		if participant:
			peers.append(participant.peer_id)
	var batch := NetwGroupPromise.create(PackedInt32Array(peers))
	for participant: NetwParticipant in moving:
		if participant:
			participant.move_to(self)
	# Arrivals report at the next settle, so a caller chaining on the returned
	# promise is subscribed before the group settles, and unkeyed, so two moves
	# in one cascade each report their own batch.
	var api := _api()
	if api:
		api._settle_schedule(_resolve_moved.bind(batch, moving))
	return batch


# Reports each moved participant onto the group promise, settling it once the
# last one lands.
func _resolve_moved(
		batch: NetwGroupPromise,
		moved: Array[NetwParticipant],
) -> void:
	for participant: NetwParticipant in moved:
		if participant:
			batch.resolve_peer(participant.peer_id, participant)
	# A move nobody was waiting on settles here, because the last arrival is
	# what settles every other one and there is no arrival to be last.
	if not batch.is_completed:
		batch.resolve_all()


# The peer id [param who] names, as a participant or an id already. Zero when it
# names neither, which every admission verb refuses.
func _peer_of(who: Variant) -> int:
	if who is NetwParticipant:
		return (who as NetwParticipant).peer_id
	return int(who) if who is int else 0


## Places [param player] under [member level] and admits its peer.
##
## The one call that seats a freshly instantiated player: parenting alone makes
## it a member, because membership is ancestry, but its peer still has to be
## admitted before the subtree is replicated to it.
## [codeblock]
## var node := template.instantiate_player(participant)
## NetwEntity.of(self).scene.add_player(NetwEntity.of(node))
## [/codeblock]
## [br][br][b]Server Only.[/b]
func add_player(player: NetwEntity) -> Error:
	var api := _api()
	var content := level
	if api == null or player == null or not is_instance_valid(player.owner) \
			or not is_instance_valid(content):
		return ERR_UNAVAILABLE
	# Admission is flushed before the node enters the tree, so the spawn packets
	# the subtree produces already target the peer about to receive them. Only
	# authority admits; every peer still parents its own copy.
	var verdict := OK
	if api.is_server():
		verdict = api.scene_admit(entity, player.peer_id)
	content.add_child(player.owner)
	player.owner.owner = content
	if api.is_server():
		api._scene_watch_entity(player)
		api.interest_flush()
	return verdict


## Moves [param moved] into this scene, settling once the move has landed.
## [br][br][b]Server Only.[/b]
func move_in(moved: NetwEntity, opts: SceneMoveOpts = null) -> NetwPromise:
	var api := _api()
	if api == null or moved == null or not is_instance_valid(moved.owner):
		var refused := NetwPromise.new()
		refused.reject(ERR_UNAVAILABLE, "move_in: no session or entity")
		return refused
	return api.scene_move(api.entity_of(moved.owner), entity, opts)


## Calls [param callback] with each [NetwParticipant] the scene admits.
##
## Chainable, and registered against the scene resolved right now, so a node
## inside the scene wires its own arrivals in [method Node._ready] and never
## sees another scene's.
## [codeblock]
## NetwEntity.of(self).scene \
##     .on_participant_entered(_greet) \
##     .on_participant_left(_farewell)
## [/codeblock]
func on_participant_entered(callback: Callable) -> NetwSceneHandle:
	return _observe(
		NetwMultiplayer.SceneEvent.SCENE_EVENT_PARTICIPANT,
		true,
		callback,
	)


## Calls [param callback] with each [NetwParticipant] the scene releases.
func on_participant_left(callback: Callable) -> NetwSceneHandle:
	return _observe(
		NetwMultiplayer.SceneEvent.SCENE_EVENT_PARTICIPANT,
		false,
		callback,
	)


## Calls [param callback] with each player [NetwEntity] entering the scene.
func on_player_entered(callback: Callable) -> NetwSceneHandle:
	return _observe(NetwMultiplayer.SceneEvent.SCENE_EVENT_PLAYER, true, callback)


## Calls [param callback] with each player [NetwEntity] leaving the scene.
func on_player_left(callback: Callable) -> NetwSceneHandle:
	return _observe(NetwMultiplayer.SceneEvent.SCENE_EVENT_PLAYER, false, callback)


## Calls [param callback] with each [NetwEntity] entering the scene.
##
## Players report here too. Read [method on_player_entered] to see only them.
func on_entity_entered(callback: Callable) -> NetwSceneHandle:
	return _observe(NetwMultiplayer.SceneEvent.SCENE_EVENT_ENTITY, true, callback)


## Calls [param callback] with each [NetwEntity] leaving the scene.
func on_entity_left(callback: Callable) -> NetwSceneHandle:
	return _observe(NetwMultiplayer.SceneEvent.SCENE_EVENT_ENTITY, false, callback)


# Registers one direction of a machine-tier edge. The relay is a method of this
# handle rather than a lambda so the core can prune it: the handle dies with the
# entity that owns it, which is the lifetime a caller expects of its own hook.
func _observe(
		event: NetwMultiplayer.SceneEvent,
		want: bool,
		callback: Callable,
) -> NetwSceneHandle:
	var api := _api()
	if api and callback.is_valid():
		var scene := entity
		api.scene_observe(scene, event, _relay.bind(scene, event, want, callback))
	return self


# Re-types one edge's subject from an id back into the wrapper the caller asked
# in terms of, dropping the direction it did not register for. A callback whose
# object is gone retires its own registration, so a scene outliving its
# observers does not accumulate them.
func _relay(
		present: bool,
		subject: Variant,
		scene: RID,
		event: NetwMultiplayer.SceneEvent,
		want: bool,
		callback: Callable,
) -> void:
	var api := _api()
	if api == null:
		return
	if not callback.is_valid():
		api.scene_unobserve(
			scene,
			event,
			_relay.bind(scene, event, want, callback),
		)
		return
	if present != want:
		return
	if event == NetwMultiplayer.SceneEvent.SCENE_EVENT_PARTICIPANT:
		var participant := api.peer_get_participant(int(subject))
		if participant:
			callback.call(participant)
		return
	var node := api.entity_get_node(subject as RID)
	var record := NetwEntity.of(node) if node else null
	if record:
		callback.call(record)


## Returns the resolved scene's container node, or [code]null[/code].
##
## The escape hatch for the few framework paths that need the node itself rather
## than a fact about the scene. Gameplay reads [member level].
func level_container() -> Node:
	return _scene_node()


func _entity() -> NetwEntity:
	return _entity_ref.get_ref() as NetwEntity if _entity_ref else null


func _api() -> NetwMultiplayer:
	var own := _entity()
	if own == null or not is_instance_valid(own.owner):
		return null
	return NetwMultiplayer.of(own.owner)


func _scene_node() -> Node:
	return _resolve_container()
