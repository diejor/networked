## The player's Discord party, invitations and friends, for a scene that holds
## a [DiscordSocial].
extends PanelContainer

const SETTINGS_PATH := "user://discord_panel.cfg"
const SCREEN_MARGIN := 16.0
const LIST_MAX_HEIGHT := 360.0

## Hides and shows the panel while no text field has focus.
@export var visibility_shortcut: Shortcut:
	set(value):
		visibility_shortcut = value
		if not is_node_ready():
			return
		var key := value.get_as_text() if value != null else ""
		hint_label.text = key.replace("QuoteLeft", "`") + " to hide"
		hint_label.visible = value != null
## Shows only the header, the party's slots and new invitations.
@export var minimized := false:
	set(value):
		minimized = value
		if not is_node_ready():
			return
		full_views.visible = not minimized
		mini_party.visible = minimized
		toggle_button.text = "+" if minimized else "-"
		toggle_button.tooltip_text = "Expand" if minimized else "Minimize"
		save_settings()

var moved := false
var settings_key := "panel"

@onready var discord := Netw.session(self).service(DiscordSocial) \
		as DiscordSocial
@onready var header: HBoxContainer = %Header
@onready var hint_label: Label = %HintLabel
@onready var toggle_button: Button = %ToggleButton
@onready var mini_party: HBoxContainer = %MiniParty
@onready var full_views: VBoxContainer = %FullViews
@onready var friend_scroll: ScrollContainer = %FriendList.scroll


func _ready() -> void:
	if discord == null:
		push_warning(
			"The Discord panel found no DiscordSocial in its session. "
			+ "Add a DiscordSocial node to the scene that holds the panel.",
		)
	if discord == null or discord.client == null:
		hide()
		set_process_shortcut_input(false)
		return

	for view: Node in [
		%MiniParty,
		%InviteList,
		%PartyCard,
		%FriendList,
		%UserBar,
	]:
		view.discord = discord
	discord.party.invited.connect(_on_party_invited)
	discord.party.join_requested.connect(_on_party_invited)
	toggle_button.pressed.connect(_on_toggle_button_pressed)
	header.gui_input.connect(_on_header_gui_input)
	get_viewport().size_changed.connect(_on_viewport_size_changed)
	resized.connect(_on_viewport_size_changed)
	minimum_size_changed.connect(reset_size)
	visibility_shortcut = visibility_shortcut

	var scene := get_tree().current_scene
	if scene != null and not scene.scene_file_path.is_empty():
		settings_key = scene.scene_file_path
	var settings := ConfigFile.new()
	settings.load(SETTINGS_PATH)
	var saved: Variant = settings.get_value(settings_key, "position", false)
	if saved is Vector2:
		moved = true
		set_anchors_preset(Control.PRESET_TOP_LEFT)
		position = saved
		_on_viewport_size_changed()
	var shrunk: Variant = settings.get_value(settings_key, "minimized", false)
	minimized = shrunk is bool and shrunk


func _shortcut_input(event: InputEvent) -> void:
	if visibility_shortcut != null \
			and visibility_shortcut.matches_event(event) \
			and event.is_pressed() and not event.is_echo():
		visible = not visible
		get_viewport().set_input_as_handled()


func save_settings() -> void:
	var settings := ConfigFile.new()
	settings.load(SETTINGS_PATH)
	if moved:
		settings.set_value(settings_key, "position", position)
	settings.set_value(settings_key, "minimized", minimized)
	settings.save(SETTINGS_PATH)


func _on_party_invited(_friend: int) -> void:
	if not visible:
		minimized = true
		show()


func _on_toggle_button_pressed() -> void:
	minimized = not minimized


func _on_header_gui_input(event: InputEvent) -> void:
	var motion := event as InputEventMouseMotion
	if motion != null and (motion.button_mask & MOUSE_BUTTON_MASK_LEFT) != 0:
		moved = true
		set_anchors_preset(Control.PRESET_TOP_LEFT)
		position += motion.relative
		_on_viewport_size_changed()

	var button := event as InputEventMouseButton
	if button != null and button.button_index == MOUSE_BUTTON_LEFT \
			and not button.pressed:
		save_settings()


func _on_viewport_size_changed() -> void:
	var view := get_viewport_rect().size
	var rest := get_combined_minimum_size().y \
			- friend_scroll.custom_minimum_size.y
	var room := view.y - SCREEN_MARGIN - position.y - rest
	var list_height := clampf(room, 0.0, LIST_MAX_HEIGHT)
	if not is_equal_approx(list_height, friend_scroll.custom_minimum_size.y):
		friend_scroll.custom_minimum_size.y = list_height
	position = position.clamp(Vector2.ZERO, (view - size).max(Vector2.ZERO))
