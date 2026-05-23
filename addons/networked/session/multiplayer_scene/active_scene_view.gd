## Renders an active [SubViewport] scene into the host's root viewport.
##
## Add this as a child of [MultiplayerTree] in your main scene. On
## listen-server hosts, this view pulls the local player's current scene
## directly from the tree and [MultiplayerSceneManager], rather than being
## pushed to by the scene manager.
##
## [br][br]
## Pure clients and dedicated servers do not need this node — pure clients
## render their scene directly into root (via the world_2d swap in
## [code]ServerScene.tscn[/code]) and dedicated servers don't render at all.
##
## [br][br]
## [b]Layout:[/b] defaults to filling its parent rect (PRESET_FULL_RECT). When
## parented under a plain [Node], the view re-syncs to the root window on
## resize. Stretch behavior mirrors Godot's project-level
## [code]display/window/stretch/*[/code] settings via [StretchLayout]; assign
## [member stretch_override] to deviate per-view.
class_name ActiveSceneView
extends Control

## Optional per-view stretch configuration. When [code]null[/code] (default),
## settings are read from [ProjectSettings] so the host matches what pure
## clients get from the root viewport's stretch pipeline.
@export var stretch_override: StretchSettings = null

var _target: SubViewport = null
var _mt: MultiplayerTree
var _settings: StretchSettings = null
var _layout: StretchLayout.Result = null
var _previous_update_mode: int = SubViewport.UPDATE_DISABLED
var _previous_clear_mode: int = SubViewport.CLEAR_MODE_NEVER
var _previous_size_2d_override: Vector2i = Vector2i.ZERO
var _previous_override_stretch: bool = false
var _dbg: NetwHandle = Netw.dbg.handle(self)


# ---- Lifecycle ----

func _enter_tree() -> void:
	if Engine.is_editor_hint():
		return
	_mt = NetwServices.register(self, ActiveSceneView)
	if not _mt:
		return
	set_anchors_and_offsets_preset(Control.PRESET_FULL_RECT)
	mouse_filter = Control.MOUSE_FILTER_IGNORE
	set_process(false)
	resized.connect(_on_resized)
	_mt.configured.connect(_subscribe_scene_manager)
	_mt.local_player_changed.connect(_on_local_player_changed)


func _ready() -> void:
	if Engine.is_editor_hint():
		return
	_settings = stretch_override if stretch_override else StretchSettings.from_project()
	_sync_root_sized_rect()
	# Anchors only propagate from a parent Control. When parented under a
	# plain Node (the usual MultiplayerTree case), the window resize never
	# reaches our anchors, so we listen to the root viewport ourselves.
	var root := get_tree().root
	if not root.size_changed.is_connected(_sync_root_sized_rect):
		root.size_changed.connect(_sync_root_sized_rect)


func _exit_tree() -> void:
	if Engine.is_editor_hint():
		return
	var root := get_tree().root
	if root and root.size_changed.is_connected(_sync_root_sized_rect):
		root.size_changed.disconnect(_sync_root_sized_rect)
	clear_target()
	_mt = null
	NetwServices.unregister(self, ActiveSceneView)


func _process(_dt: float) -> void:
	if is_instance_valid(_target):
		queue_redraw()


# ---- Target attachment ----

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
		_previous_size_2d_override = _target.size_2d_override
		_previous_override_stretch = _target.size_2d_override_stretch
		_target.render_target_update_mode = SubViewport.UPDATE_ALWAYS
		_target.render_target_clear_mode = SubViewport.CLEAR_MODE_ALWAYS
		_apply_layout()
		if not _target.tree_exiting.is_connected(_on_target_freed):
			_target.tree_exiting.connect(_on_target_freed)
		_dbg.info("ActiveSceneView now displays '%s'.", [_target.name])

	set_process(is_instance_valid(_target))
	queue_redraw()


## Detaches this view from its current target.
func clear_target() -> void:
	set_target(null)


func _on_target_freed() -> void:
	if is_instance_valid(_target):
		_restore_target_render_state()
	_target = null
	set_process(false)
	queue_redraw()


func _restore_target_render_state() -> void:
	_target.render_target_update_mode = _previous_update_mode
	_target.render_target_clear_mode = _previous_clear_mode
	_target.size_2d_override = _previous_size_2d_override
	_target.size_2d_override_stretch = _previous_override_stretch


# ---- Layout (stretch pipeline mirror) ----

func _on_resized() -> void:
	_apply_layout()


func _apply_layout() -> void:
	if _settings == null:
		_settings = stretch_override if stretch_override else StretchSettings.from_project()
	_layout = StretchLayout.compute(_settings, size)
	if is_instance_valid(_target):
		_target.size = _layout.target_size
		_target.size_2d_override = _layout.size_2d_override
		_target.size_2d_override_stretch = _layout.override_stretch
	queue_redraw()


# Makes the view fill the game window when parented under a plain Node.
func _sync_root_sized_rect() -> void:
	if get_parent() is Control:
		return
	var rect := get_tree().root.get_visible_rect()
	anchor_left = 0.0
	anchor_top = 0.0
	anchor_right = 0.0
	anchor_bottom = 0.0
	position = Vector2.ZERO
	set_deferred("size", rect.size)


# ---- Rendering & input ----

func _draw() -> void:
	if not is_instance_valid(_target) or _layout == null:
		return
	var tex := _target.get_texture()
	if tex:
		draw_texture_rect(tex, _layout.inner_rect, false)


# Forwards GUI mouse input into the target SubViewport. Events from
# _gui_input are in this control's local space; remap them into the
# SubViewport's logical 2D space (which may differ from the inner_rect when
# size_2d_override or pure viewport mode are in play).
func _gui_input(event: InputEvent) -> void:
	if not is_instance_valid(_target) or _layout == null:
		return
	var to_push := event
	if event is InputEventMouse and _layout.inner_rect.size.x > 0.0 \
			and _layout.inner_rect.size.y > 0.0:
		var logical := Vector2(_target.size_2d_override) \
				if _target.size_2d_override != Vector2i.ZERO \
				else Vector2(_target.size)
		var scale := logical / _layout.inner_rect.size
		var xform := Transform2D.IDENTITY.scaled(scale) \
				.translated(-_layout.inner_rect.position * scale)
		to_push = event.xformed_by(xform)
	_target.push_input(to_push, true)


# Keyboard/action input that survived the host UI gets forwarded into the
# target viewport unmodified.
func _unhandled_input(event: InputEvent) -> void:
	if not is_instance_valid(_target):
		return
	if event is InputEventMouse:
		return
	_target.push_input(event, true)


# ---- Local-player discovery ----

func _on_local_player_changed(player: Node) -> void:
	if is_instance_valid(player):
		if not player.tree_entered.is_connected(_on_local_player_entered):
			player.tree_entered.connect(_on_local_player_entered)
	_refresh.call_deferred()


func _on_local_player_entered() -> void:
	_refresh.call_deferred()


func _subscribe_scene_manager() -> void:
	if not _mt.is_host:
		return
	var sm := _mt.get_service(MultiplayerSceneManager)
	if sm:
		sm.scene_despawned.connect(func(_s): _refresh.call_deferred())
		sm.scene_spawned.connect(_watch_scene_for_local_player_arrival)
		sm.scene_activated.connect(_on_scene_activated)
		sm.startup_scenes_spawned.connect(_on_startup_scenes_spawned)
		for scene: MultiplayerScene in sm.active_scenes.values():
			_watch_scene_for_local_player_arrival(scene)


func _watch_scene_for_local_player_arrival(scene: MultiplayerScene) -> void:
	if not scene.player_spawned.is_connected(_on_scene_player_spawned):
		scene.player_spawned.connect(
			_on_scene_player_spawned.bind(scene))
	_refresh.call_deferred()


func _on_scene_player_spawned(_player: Node, _scene: MultiplayerScene) -> void:
	_refresh.call_deferred()


func _on_scene_activated(_scene: MultiplayerScene) -> void:
	_refresh.call_deferred()


func _on_startup_scenes_spawned() -> void:
	_refresh.call_deferred()


func _refresh() -> void:
	if not _mt or not _mt.is_local_client:
		set_target(null)
		return
	var player := _find_local_player()
	if not is_instance_valid(player):
		set_target(_find_active_viewport())
		return
	var scene := MultiplayerTree.scene_for_node(player)
	var viewport := scene as Node as SubViewport if scene else null
	set_target(viewport if viewport else _find_active_viewport())


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


# Returns an active host-rendered scene viewport when no local player is found.
func _find_active_viewport() -> SubViewport:
	if not _mt or not _mt.is_host:
		return null
	var sm := _mt.get_service(MultiplayerSceneManager)
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
