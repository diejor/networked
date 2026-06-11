## Renders the host's currently-active [SubViewport] scene into this viewport.
##
## On listen-server hosts, this view pulls the local player's current scene
## from the [MultiplayerTree] and [MultiplayerSceneManager] and draws it
## edge-to-edge so the host sees the game like a pure client does.
##
## [br][br]
## [b]You normally don't add this node yourself.[/b] [MultiplayerTree] adds
## one automatically when [member MultiplayerTree.desired_role] is
## [constant MultiplayerTree.LISTEN_SERVER] and no existing
## [HostSceneView] descendant is found. Drop one into your scene
## only when you need to customize [member stretch_override], reparent it
## under a specific [Control], or otherwise deviate from the defaults.
##
## [br][br]
## Pure clients and dedicated servers do not use this node pure clients
## render their scene directly into root (via the [member Viewport.world_2d] swap in
## [code]ServerScene.tscn[/code]) and dedicated servers don't render at all.
##
## [br][br]
## Defaults to filling its parent rect with
## [constant Control.PRESET_FULL_RECT]. When
## parented under a plain [Node], the view re-syncs to its enclosing viewport
## on resize. Stretch behavior mirrors Godot's project-level
## [code]display/window/stretch/*[/code] settings via [StretchLayout], assign
## [member stretch_override] to deviate per-view.
class_name HostSceneView
extends ParticipantView

var _mt: MultiplayerTree
var _suppressed := false
var _display_source := ParticipantDisplaySource.new()

# ---- Lifecycle ----


func _enter_tree() -> void:
	if Engine.is_editor_hint():
		return
	super._enter_tree()
	_mt = NetwServices.register(self, HostSceneView)
	if not _mt:
		return
	set_anchors_and_offsets_preset(Control.PRESET_FULL_RECT)
	mouse_filter = Control.MOUSE_FILTER_IGNORE
	forward_unhandled_input = true
	if not _display_source.changed.is_connected(_on_display_source_changed):
		_display_source.changed.connect(_on_display_source_changed)
	_display_source.configure(_mt)


func _exit_tree() -> void:
	if Engine.is_editor_hint():
		return
	_display_source.dispose()
	_mt = null
	NetwServices.unregister(self, HostSceneView)
	super._exit_tree()


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
