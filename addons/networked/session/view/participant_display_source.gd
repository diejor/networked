## Tracks the [SubViewport] that represents one participant's rendered world.
##
## Host roles display the active scene's viewport. Pure clients use
## [member fallback] because their world is mounted under the participant
## window. Resolution reads session facts through [NetwMultiplayer], so a
## root-installed host with no owning [MultiplayerTree] resolves the same way a
## tree-scoped one does.
extends RefCounted

## Emitted when [member current] resolves to a different [SubViewport].
signal changed(viewport: SubViewport)

## [NetwMultiplayer] session used to resolve the participant's active world.
var api: NetwMultiplayer:
	get:
		return _api

## Viewport returned for non-host local clients.
var fallback: SubViewport:
	get:
		return _fallback

## Currently resolved participant display viewport.
var current: SubViewport:
	get:
		if not is_instance_valid(_current):
			_current = null
		return _current

var _api: NetwMultiplayer = null
var _fallback: SubViewport = null
var _current: SubViewport = null
var _watched_scenes: Array[Node] = []
var _local_player: Node = null


## Sets the session and fallback viewport this resolver should watch.
func configure(
		p_api: NetwMultiplayer,
		p_fallback: SubViewport = null,
) -> void:
	if _api == p_api and _fallback == p_fallback:
		return
	dispose()
	_api = p_api
	_fallback = p_fallback
	_subscribe_api()
	refresh.call_deferred()


## Disconnects all watched signals and clears [member current].
func dispose() -> void:
	_unsubscribe_api()
	_unwatch_local_player()
	_api = null
	_fallback = null
	_set_current(null)


## Resolves [member current] immediately.
func refresh() -> void:
	_set_current(_resolve())


func _resolve() -> SubViewport:
	if not _api:
		return null
	if _api.role == NetwMultiplayer.Role.NONE:
		return null
	if _api.role == NetwMultiplayer.Role.CLIENT:
		return _fallback
	if _api.role != NetwMultiplayer.Role.LISTEN_SERVER:
		return null

	var player := _find_local_player()
	if is_instance_valid(player):
		var handle: NetwSceneHandle = NetwEntity.of(player).scene
		var scene := handle.level_container()
		var viewport := scene as Node as SubViewport if scene else null
		if viewport:
			return viewport
	return _local_scene_viewport()


# With no local player entity yet (a lobby roster, a between-scenes host, a
# spectator), presents the scene the local participant was admitted to rather
# than an arbitrary active scene, so the host window matches
# [member SceneCore.current_scene] instead of the first spawned world.
func _local_scene_viewport() -> SubViewport:
	var current := _api._scenes.current_scene if _api else null
	var viewport := current as Node as SubViewport if current else null
	if viewport:
		return viewport
	return _find_active_viewport()


func _find_local_player() -> Node:
	if not _api:
		return null
	var player_entity := _api.local_player
	if player_entity != null and is_instance_valid(player_entity.owner):
		var player := player_entity.owner
		_watch_local_player(player)
		return player
	var scenes := _scene_api()
	if not scenes:
		return null
	var local_id := _api.get_unique_id()
	for scene: Node in scenes.scenes.values():
		for entity: NetwEntity in _handle(scene).players:
			if entity != null and is_instance_valid(entity.owner):
				var p := entity.owner
				if entity.peer_id == local_id:
					_watch_local_player(p)
					return p
				if NetwEntity.parse_peer(p.name) == local_id:
					_watch_local_player(p)
					return p
	return null


func _find_active_viewport() -> SubViewport:
	if not _api or _api.role != NetwMultiplayer.Role.LISTEN_SERVER:
		return null
	var scenes := _scene_api()
	if not scenes:
		return null
	for scene: Node in scenes.scenes.values():
		if not is_instance_valid(scene):
			continue
		var level := _handle(scene).level
		if not is_instance_valid(level):
			continue
		if level.process_mode == Node.PROCESS_MODE_DISABLED:
			continue
		var viewport := scene as Node as SubViewport
		if viewport:
			return viewport
	return null


func _set_current(viewport: SubViewport) -> void:
	if _current == viewport:
		return
	_current = viewport
	changed.emit(_current)


func _subscribe_api() -> void:
	if not _api:
		return
	if not _api.session_entered.is_connected(_subscribe_scene_manager):
		_api.session_entered.connect(_subscribe_scene_manager)
	if not _api.session_ended.is_connected(_on_session_ended):
		_api.session_ended.connect(_on_session_ended)
	if not _api.local_player_changed.is_connected(_on_local_player_changed):
		_api.local_player_changed.connect(_on_local_player_changed)
	if not _api.local_scene_changed.is_connected(_on_local_scene_changed):
		_api.local_scene_changed.connect(_on_local_scene_changed)
	_subscribe_scene_manager()


func _unsubscribe_api() -> void:
	if not _api:
		return
	if _api.session_entered.is_connected(_subscribe_scene_manager):
		_api.session_entered.disconnect(_subscribe_scene_manager)
	if _api.session_ended.is_connected(_on_session_ended):
		_api.session_ended.disconnect(_on_session_ended)
	if _api.local_player_changed.is_connected(_on_local_player_changed):
		_api.local_player_changed.disconnect(_on_local_player_changed)
	if _api.local_scene_changed.is_connected(_on_local_scene_changed):
		_api.local_scene_changed.disconnect(_on_local_scene_changed)
	_unsubscribe_scene_manager()


func _subscribe_scene_manager() -> void:
	if not _api or _api.role != NetwMultiplayer.Role.LISTEN_SERVER:
		return
	var scenes := _scene_api()
	if not scenes:
		return
	if not scenes.scene_despawned.is_connected(_on_scene_changed):
		scenes.scene_despawned.connect(_on_scene_changed)
	if not scenes.scene_spawned.is_connected(_watch_scene):
		scenes.scene_spawned.connect(_watch_scene)
	if not scenes.scene_activated.is_connected(_on_scene_changed):
		scenes.scene_activated.connect(_on_scene_changed)
	if not scenes._startup_scenes_spawned.is_connected(_refresh_deferred):
		scenes._startup_scenes_spawned.connect(_refresh_deferred)
	for scene: Node in scenes.scenes.values():
		_watch_scene(scene)


func _unsubscribe_scene_manager() -> void:
	var scenes := _scene_api()
	if scenes:
		if scenes.scene_despawned.is_connected(_on_scene_changed):
			scenes.scene_despawned.disconnect(_on_scene_changed)
		if scenes.scene_spawned.is_connected(_watch_scene):
			scenes.scene_spawned.disconnect(_watch_scene)
		if scenes.scene_activated.is_connected(_on_scene_changed):
			scenes.scene_activated.disconnect(_on_scene_changed)
		if scenes._startup_scenes_spawned.is_connected(_refresh_deferred):
			scenes._startup_scenes_spawned.disconnect(_refresh_deferred)
	for scene in _watched_scenes.duplicate():
		_unwatch_scene(scene)


# The scene one container stands in for.
func _handle(scene: Node) -> NetwSceneHandle:
	var record := NetwEntity.of(scene)
	return record.scene if record else NetwSceneHandle.new()


# The session's scene interface while the session stays live.
func _scene_api() -> SceneCore:
	return _api._scenes if _api else null


func _watch_scene(scene: Node) -> void:
	if not is_instance_valid(scene):
		return
	if _watched_scenes.has(scene):
		_refresh_deferred()
		return
	_watched_scenes.append(scene)
	if _api:
		_api.scene_observe(
			_api.entity_of(scene),
			NetwMultiplayer.SceneEvent.SCENE_EVENT_ENTITY,
			_on_scene_population_changed,
		)
	var tree_exiting := _unwatch_scene.bind(scene)
	if not scene.tree_exiting.is_connected(tree_exiting):
		scene.tree_exiting.connect(tree_exiting)
	_refresh_deferred()


func _unwatch_scene(scene: Node) -> void:
	if not _watched_scenes.has(scene):
		return
	_watched_scenes.erase(scene)
	if not is_instance_valid(scene):
		return
	if _api:
		_api.scene_unobserve(
			_api.entity_of(scene),
			NetwMultiplayer.SceneEvent.SCENE_EVENT_ENTITY,
			_on_scene_population_changed,
		)
	var tree_exiting := _unwatch_scene.bind(scene)
	if scene.tree_exiting.is_connected(tree_exiting):
		scene.tree_exiting.disconnect(tree_exiting)


func _watch_local_player(player: Node) -> void:
	if _local_player == player:
		return
	_unwatch_local_player()
	_local_player = player
	if is_instance_valid(_local_player) \
			and not _local_player.tree_entered.is_connected(_refresh_deferred):
		_local_player.tree_entered.connect(_refresh_deferred)


func _unwatch_local_player() -> void:
	if not is_instance_valid(_local_player):
		_local_player = null
		return
	if _local_player.tree_entered.is_connected(_refresh_deferred):
		_local_player.tree_entered.disconnect(_refresh_deferred)
	_local_player = null


func _on_local_player_changed(player: NetwEntity) -> void:
	if player != null and is_instance_valid(player.owner):
		_watch_local_player(player.owner)
	_refresh_deferred()


func _on_local_scene_changed(_from: NetwSceneHandle, _to: NetwSceneHandle) -> void:
	_refresh_deferred()


func _on_scene_population_changed(_present: bool, _entity: RID) -> void:
	_refresh_deferred()


func _on_scene_changed(_scene: Node) -> void:
	_refresh_deferred()


func _on_session_ended() -> void:
	_unsubscribe_scene_manager()
	_unwatch_local_player()
	_set_current(null)


func _refresh_deferred() -> void:
	refresh.call_deferred()
