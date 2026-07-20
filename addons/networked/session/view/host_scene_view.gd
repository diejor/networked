## Draws a listen-server host's offscreen player scene edge-to-edge.
##
## Only a [constant NetwSceneConfig.Concurrency.CONCURRENT] session puts the
## host's own world in an offscreen [SubViewport], so this view exists only
## there. It targets the [SubViewport] holding the local player's
## [member NetwSceneInterface.current_scene] and draws it so the host sees the
## game a pure client sees directly in the root viewport. A dedicated server
## renders no scene, and a [constant NetwSceneConfig.Concurrency.SINGLE] host
## renders like a client, so neither builds this view.
##
## [br][br]
## [b]You normally do not add this node yourself.[/b] A listen-server host adds
## one automatically when [member NetwSessionInterface.role] is
## [constant NetwSessionInterface.Role.LISTEN_SERVER], scene concurrency is
## [constant NetwSceneConfig.Concurrency.CONCURRENT], and no view already owns
## the display. When no [signal NetwEntity.view_activated] listener claims the
## camera, this view makes the first [Camera2D] or [Camera3D] in the player
## branch current. Drop one into your scene only to customize that window.
##
## [br][br]
## Defaults to filling its parent rect with
## [constant Control.PRESET_FULL_RECT]. Parented under a plain [Node], the view
## re-syncs to its enclosing viewport on resize. Stretch behavior mirrors
## Godot's project-level [code]display/window/stretch/*[/code] settings through
## [StretchLayout]. Assign [member stretch_override] to deviate per view.
class_name HostSceneView
extends ParticipantView

var _api: NetwMultiplayer
var _suppressed := false
var _display_source := ParticipantDisplaySource.new()

# ---- Lifecycle ----


func _enter_tree() -> void:
	if Engine.is_editor_hint():
		return
	super._enter_tree()
	_api = NetwMultiplayer.of(self)
	if not _api:
		return
	_api.register_service(self, HostSceneView)
	set_anchors_and_offsets_preset(Control.PRESET_FULL_RECT)
	mouse_filter = Control.MOUSE_FILTER_PASS
	forward_unhandled_input = true
	if not _display_source.changed.is_connected(_on_display_source_changed):
		_display_source.changed.connect(_on_display_source_changed)
	# The current viewport can resolve before the local player spawns into it, so
	# re-announce on local-player changes to adopt the camera without needing the
	# viewport itself to change.
	if not _api.local_player_changed.is_connected(_on_local_player_changed):
		_api.local_player_changed.connect(_on_local_player_changed)
	_display_source.configure(_api)


func _exit_tree() -> void:
	if Engine.is_editor_hint():
		return
	if _api and _api.local_player_changed.is_connected(_on_local_player_changed):
		_api.local_player_changed.disconnect(_on_local_player_changed)
	_display_source.dispose()
	if _api:
		_api.unregister_service(self, HostSceneView)
	_api = null
	super._exit_tree()


# The local player is assigned while its entity is still entering the tree, so
# defer the camera adoption until its viewport and camera are fully mounted.
func _on_local_player_changed(_player: NetwEntity) -> void:
	_reannounce_deferred.call_deferred()


func _reannounce_deferred() -> void:
	if not _suppressed and is_inside_tree():
		_announce_view(_display_source.current)


## Sets whether this automatic host view should draw and forward input.
func set_suppressed(suppressed: bool) -> void:
	if _suppressed == suppressed:
		return
	_suppressed = suppressed
	visible = not suppressed
	forward_unhandled_input = not suppressed
	if suppressed:
		clear_target()
	else:
		_on_display_source_changed(_display_source.current)


func _on_display_source_changed(viewport: SubViewport) -> void:
	if _suppressed:
		set_target(null)
		return
	set_target(viewport)
	_announce_view(viewport)


# Tells the local player's entity it is now the displayed view, so its camera
# (whatever implementation) can assert itself. Scoped to the player actually
# under this viewport, so the spectator fallback never activates a stray view.
func _announce_view(viewport: SubViewport) -> void:
	if not is_instance_valid(viewport) or not _api:
		return
	var local_entity := _api.local_player
	if local_entity == null or not is_instance_valid(local_entity.owner):
		return
	var player := local_entity.owner
	if not viewport.is_ancestor_of(player):
		return
	if local_entity.view_activated.get_connections().is_empty():
		_adopt_camera(player, MultiplayerScene.of(player))
	local_entity.view_activated.emit()


# Makes the first conventional camera current when no custom view hook exists.
func _adopt_camera(player: Node, scene: MultiplayerScene) -> void:
	var camera_2d := _first_camera(player, "Camera2D") as Camera2D
	if camera_2d == null and scene and is_instance_valid(scene.level):
		camera_2d = _first_camera(scene.level, "Camera2D") as Camera2D
	if camera_2d:
		camera_2d.make_current()
		return
	var camera_3d := _first_camera(player, "Camera3D") as Camera3D
	if camera_3d == null and scene and is_instance_valid(scene.level):
		camera_3d = _first_camera(scene.level, "Camera3D") as Camera3D
	if camera_3d:
		camera_3d.make_current()


# Returns the first conventional camera in a branch.
func _first_camera(root: Node, type: String) -> Node:
	if root == null:
		return null
	if root.is_class(type):
		return root
	var cameras := root.find_children("*", type, true, false)
	return cameras[0] if not cameras.is_empty() else null
