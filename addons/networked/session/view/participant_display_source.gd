## Tracks the [SubViewport] that represents one participant's rendered world.
##
## Host roles display the active [MultiplayerScene] viewport. Pure clients use
## [member fallback] because their world is mounted under the participant
## window.
class_name ParticipantDisplaySource
extends RefCounted

## Emitted when [member current] resolves to a different [SubViewport].
signal changed(viewport: SubViewport)

## [MultiplayerTree] used to resolve the participant's active world.
var tree: MultiplayerTree:
	get:
		return _tree

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

var _tree: MultiplayerTree = null
var _fallback: SubViewport = null
var _current: SubViewport = null
var _watched_scenes: Array[MultiplayerScene] = []
var _local_player: Node = null


## Sets the tree and fallback viewport this resolver should watch.
func configure(
		p_tree: MultiplayerTree,
		p_fallback: SubViewport = null,
) -> void:
	if _tree == p_tree and _fallback == p_fallback:
		return
	dispose()
	_tree = p_tree
	_fallback = p_fallback
	_subscribe_tree()
	refresh.call_deferred()


## Disconnects all watched signals and clears [member current].
func dispose() -> void:
	_unsubscribe_tree()
	_unwatch_local_player()
	_tree = null
	_fallback = null
	_set_current(null)


## Resolves [member current] immediately.
func refresh() -> void:
	_set_current(_resolve())


func _resolve() -> SubViewport:
	if not _tree:
		return null
	if _tree.role == MultiplayerTree.Role.NONE:
		return null
	if _tree.role == MultiplayerTree.Role.CLIENT:
		return _fallback
	if _tree.role != MultiplayerTree.Role.LISTEN_SERVER:
		return null

	var player := _find_local_player()
	if is_instance_valid(player):
		var scene := MultiplayerTree.scene_for_node(player)
		var viewport := scene as Node as SubViewport if scene else null
		if viewport:
			return viewport
	return _find_active_viewport()


func _find_local_player() -> Node:
	if not _tree:
		return null
	var player := _tree.local_player
	if is_instance_valid(player):
		_watch_local_player(player)
		return player
	var sm := _tree.get_service(MultiplayerSceneManager)
	if not sm:
		return null
	var local_id := _tree.multiplayer.get_unique_id()
	for scene: MultiplayerScene in sm.active_scenes.values():
		for p in scene.get_players():
			var entity := NetwEntity.of(p)
			if entity and entity.peer_id == local_id:
				_watch_local_player(p)
				return p
			if NetwEntity.parse_peer(p.name) == local_id:
				_watch_local_player(p)
				return p
	return null


func _find_active_viewport() -> SubViewport:
	if not _tree or _tree.role != MultiplayerTree.Role.LISTEN_SERVER:
		return null
	var sm := _tree.get_service(MultiplayerSceneManager)
	if not sm:
		return null
	for scene: MultiplayerScene in sm.active_scenes.values():
		if not is_instance_valid(scene):
			continue
		if not is_instance_valid(scene.level):
			continue
		if scene.level.process_mode == Node.PROCESS_MODE_DISABLED:
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


func _subscribe_tree() -> void:
	if not _tree:
		return
	if not _tree.session_entered.is_connected(_subscribe_scene_manager):
		_tree.session_entered.connect(_subscribe_scene_manager)
	if not _tree.session_ended.is_connected(_on_session_ended):
		_tree.session_ended.connect(_on_session_ended)
	if not _tree.local_player_changed.is_connected(_on_local_player_changed):
		_tree.local_player_changed.connect(_on_local_player_changed)
	_subscribe_scene_manager()


func _unsubscribe_tree() -> void:
	if not _tree:
		return
	if _tree.session_entered.is_connected(_subscribe_scene_manager):
		_tree.session_entered.disconnect(_subscribe_scene_manager)
	if _tree.session_ended.is_connected(_on_session_ended):
		_tree.session_ended.disconnect(_on_session_ended)
	if _tree.local_player_changed.is_connected(_on_local_player_changed):
		_tree.local_player_changed.disconnect(_on_local_player_changed)
	_unsubscribe_scene_manager()


func _subscribe_scene_manager() -> void:
	if not _tree or _tree.role != MultiplayerTree.Role.LISTEN_SERVER:
		return
	var sm := _tree.get_service(MultiplayerSceneManager)
	if not sm:
		return
	if not sm.scene_despawned.is_connected(_on_scene_changed):
		sm.scene_despawned.connect(_on_scene_changed)
	if not sm.scene_spawned.is_connected(_watch_scene):
		sm.scene_spawned.connect(_watch_scene)
	if not sm.scene_activated.is_connected(_on_scene_changed):
		sm.scene_activated.connect(_on_scene_changed)
	if not sm.startup_scenes_spawned.is_connected(_refresh_deferred):
		sm.startup_scenes_spawned.connect(_refresh_deferred)
	for scene: MultiplayerScene in sm.active_scenes.values():
		_watch_scene(scene)


func _unsubscribe_scene_manager() -> void:
	var sm := _tree.get_service(MultiplayerSceneManager) if _tree else null
	if sm:
		if sm.scene_despawned.is_connected(_on_scene_changed):
			sm.scene_despawned.disconnect(_on_scene_changed)
		if sm.scene_spawned.is_connected(_watch_scene):
			sm.scene_spawned.disconnect(_watch_scene)
		if sm.scene_activated.is_connected(_on_scene_changed):
			sm.scene_activated.disconnect(_on_scene_changed)
		if sm.startup_scenes_spawned.is_connected(_refresh_deferred):
			sm.startup_scenes_spawned.disconnect(_refresh_deferred)
	for scene in _watched_scenes.duplicate():
		_unwatch_scene(scene)


func _watch_scene(scene: MultiplayerScene) -> void:
	if not is_instance_valid(scene):
		return
	if _watched_scenes.has(scene):
		_refresh_deferred()
		return
	_watched_scenes.append(scene)
	var player_spawned := _on_scene_player_spawned.bind(scene)
	if not scene.player_spawned.is_connected(player_spawned):
		scene.player_spawned.connect(player_spawned)
	var tree_exiting := _unwatch_scene.bind(scene)
	if not scene.tree_exiting.is_connected(tree_exiting):
		scene.tree_exiting.connect(tree_exiting)
	_refresh_deferred()


func _unwatch_scene(scene: MultiplayerScene) -> void:
	if not _watched_scenes.has(scene):
		return
	_watched_scenes.erase(scene)
	if not is_instance_valid(scene):
		return
	var player_spawned := _on_scene_player_spawned.bind(scene)
	if scene.player_spawned.is_connected(player_spawned):
		scene.player_spawned.disconnect(player_spawned)
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


func _on_local_player_changed(player: Node) -> void:
	if is_instance_valid(player):
		_watch_local_player(player)
	_refresh_deferred()


func _on_scene_player_spawned(_player: Node, _scene: MultiplayerScene) -> void:
	_refresh_deferred()


func _on_scene_changed(_scene: MultiplayerScene) -> void:
	_refresh_deferred()


func _on_session_ended() -> void:
	_unsubscribe_scene_manager()
	_unwatch_local_player()
	_set_current(null)


func _refresh_deferred() -> void:
	refresh.call_deferred()
