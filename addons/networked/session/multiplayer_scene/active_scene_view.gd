## Renders an active [SubViewport] scene into the host's root viewport.
##
## Add this as a child of [MultiplayerTree] in your main scene. On
## listen-server hosts, this view pulls the local player's current scene
## directly from the tree and [MultiplayerSceneManager], rather than being
## pushed to by the scene manager.
## [br][br]
## Pure clients and dedicated servers do not need this node — pure clients
## render their scene directly into root, and dedicated servers don't render
## at all.
## [br][br]
## [b]Layout:[/b] defaults to filling its parent rect (PRESET_FULL_RECT).
## Override anchors after adding it if you want a partial-screen view.
class_name ActiveSceneView
extends Control

## If [code]true[/code], the target [SubViewport]'s size is kept in sync with
## this control's size. Disable to render at a fixed (e.g. lower) resolution.
@export var auto_resize_target: bool = true

var _target: SubViewport = null
var _mt: MultiplayerTree
var _previous_update_mode: int = SubViewport.UPDATE_DISABLED
var _previous_clear_mode: int = SubViewport.CLEAR_MODE_NEVER
var _dbg: NetwHandle = Netw.dbg.handle(self)


func _enter_tree() -> void:
	if Engine.is_editor_hint():
		return
	_mt = NetwServices.register(self, ActiveSceneView)
	if not _mt:
		return
	set_anchors_and_offsets_preset(Control.PRESET_FULL_RECT)
	mouse_filter = Control.MOUSE_FILTER_IGNORE
	resized.connect(_on_resized)
	_mt.configured.connect(_subscribe_scene_manager)
	_mt.local_player_changed.connect(_on_local_player_changed)


func _on_local_player_changed(player: Node) -> void:
	if is_instance_valid(player):
		if not player.tree_entered.is_connected(_on_local_player_entered):
			player.tree_entered.connect(_on_local_player_entered)
	_refresh.call_deferred()


func _on_local_player_entered() -> void:
	_refresh.call_deferred()


func _exit_tree() -> void:
	if Engine.is_editor_hint():
		return
	clear_target()
	_mt = null
	NetwServices.unregister(self, ActiveSceneView)


func _subscribe_scene_manager() -> void:
	if not _mt.is_host:
		return
	var sm := _mt.get_service(MultiplayerSceneManager)
	if sm:
		sm.scene_despawned.connect(func(_s): _refresh.call_deferred())
		sm.scene_spawned.connect(_watch_scene_for_local_player_arrival)
		sm.startup_scenes_spawned.connect(_on_startup_scenes_spawned)
		for scene: MultiplayerScene in sm.active_scenes.values():
			_watch_scene_for_local_player_arrival(scene)


func _watch_scene_for_local_player_arrival(scene: MultiplayerScene) -> void:
	if not scene.synchronizer.spawned.is_connected(_on_scene_player_spawned):
		scene.synchronizer.spawned.connect(
			_on_scene_player_spawned.bind(scene))
	_refresh.call_deferred()


func _on_scene_player_spawned(_player: Node, _scene: MultiplayerScene) -> void:
	_refresh.call_deferred()


func _on_startup_scenes_spawned() -> void:
	_refresh.call_deferred()


func _refresh() -> void:
	if not _mt or not _mt.is_local_client:
		set_target(null)
		return
	var player := _find_local_player()
	if not is_instance_valid(player):
		set_target(null)
		return
	var scene := MultiplayerTree.scene_for_node(player)
	set_target(scene as Node as SubViewport if scene else null)


func _find_local_player() -> Node:
	var player := _mt.local_player
	if is_instance_valid(player):
		return player
	var sm := _mt.get_service(MultiplayerSceneManager)
	if not sm:
		return null
	var local_id := multiplayer.get_unique_id()
	for scene: MultiplayerScene in sm.active_scenes.values():
		for p in scene.get_players():
			var entity := NetwEntity.of(p)
			if entity and entity.peer_id == local_id:
				return p
			if NetwEntity.parse_peer(p.name) == local_id:
				return p
	return null


## Points this view at [param viewport] and forces it to render every frame.
##
## Restores the previous viewport's render settings. Pass [code]null[/code]
## or call [method clear_target] to detach.
func set_target(viewport: SubViewport) -> void:
	if _target == viewport:
		return
	if is_instance_valid(_target):
		_restore_target_render_state()
		if _target.tree_exiting.is_connected(_on_target_freed):
			_target.tree_exiting.disconnect(_on_target_freed)

	_target = viewport

	if is_instance_valid(_target):
		_previous_update_mode = _target.render_target_update_mode
		_previous_clear_mode = _target.render_target_clear_mode
		_target.render_target_update_mode = SubViewport.UPDATE_ALWAYS
		_target.render_target_clear_mode = SubViewport.CLEAR_MODE_ALWAYS
		if auto_resize_target:
			_target.size = Vector2i(size)
		if not _target.tree_exiting.is_connected(_on_target_freed):
			_target.tree_exiting.connect(_on_target_freed)
		_dbg.info("ActiveSceneView now displays '%s'.", [_target.name])

	queue_redraw()


## Detaches this view from its current target.
func clear_target() -> void:
	set_target(null)


func _draw() -> void:
	if not is_instance_valid(_target):
		return
	var tex := _target.get_texture()
	if tex:
		draw_texture_rect(tex, Rect2(Vector2.ZERO, size), false)


# Forwards GUI input to the target SubViewport. Events from _gui_input are
# already in this control's local coordinate space; when target.size matches
# this control's size, no remap is needed. When sizes differ, scale position.
func _gui_input(event: InputEvent) -> void:
	if not is_instance_valid(_target):
		return
	var to_push := event
	if event is InputEventMouse and not auto_resize_target \
			and Vector2(_target.size) != size and size.x > 0 and size.y > 0:
		var scale := Vector2(_target.size) / size
		var xform := Transform2D.IDENTITY.scaled(scale)
		to_push = event.xformed_by(xform)
	_target.push_input(to_push, true)


func _on_resized() -> void:
	if auto_resize_target and is_instance_valid(_target):
		_target.size = Vector2i(size)


func _on_target_freed() -> void:
	if is_instance_valid(_target):
		_restore_target_render_state()
	_target = null
	queue_redraw()


func _restore_target_render_state() -> void:
	_target.render_target_update_mode = _previous_update_mode
	_target.render_target_clear_mode = _previous_clear_mode
