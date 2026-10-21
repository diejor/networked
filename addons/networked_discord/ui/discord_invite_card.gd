extends PanelContainer

const Avatar := preload("res://addons/networked_discord/ui/discord_avatar.gd")
const SlotRow := preload(
	"res://addons/networked_discord/ui/discord_slot_row.gd"
)
const ElapsedLabel := preload(
	"res://addons/networked_discord/ui/discord_elapsed_label.gd"
)

var party: DiscordParty
var sender: DiscordProfile
var is_join_request: bool

@onready var header_label: Label = %HeaderLabel
@onready var icon: Avatar = %Icon
@onready var name_label: Label = %NameLabel
@onready var elapsed_label: ElapsedLabel = %ElapsedLabel
@onready var sender_party: HBoxContainer = %SenderParty
@onready var slot_row: SlotRow = %SlotRow
@onready var playing_label: Label = %PlayingLabel
@onready var join_button: Button = %JoinButton


func _ready() -> void:
	party.changed.connect(_on_party_changed)
	sender.changed.connect(_on_sender_changed)
	join_button.pressed.connect(_on_join_button_pressed)
	elapsed_label.user = sender
	name_label.text = ProjectSettings.get_setting("application/config/name")
	if is_join_request:
		icon.corner_radius = 1000.0
		icon.user = sender
		header_label.text = "Join Request"
		join_button.text = "Let in"
	_on_sender_changed()


func _draw() -> void:
	draw_texture_rect(
		get_theme_icon(&"background", &"DiscordInviteCard"),
		Rect2(Vector2.ZERO, size),
		false,
	)
	material.set_shader_parameter(&"size", size)
	material.set_shader_parameter(
		&"corner_radius",
		float(get_theme_constant(&"corner_radius", &"DiscordInviteCard")),
	)


func _on_party_changed() -> void:
	var waiting := party.invitations
	if is_join_request:
		waiting = party.join_requests
	if sender.id not in waiting:
		queue_free()


func _on_sender_changed() -> void:
	if is_join_request:
		name_label.text = sender.display_name + " asks to join"
	else:
		header_label.text = "Invitation from " + sender.display_name
	var users: Array[DiscordProfile] = [sender]
	slot_row.show_slots(users, sender.party_size, sender.party_max)
	sender_party.visible = sender.party_max > 0
	playing_label.text = "%d/%d Playing" % [sender.party_size, sender.party_max]


func _on_join_button_pressed() -> void:
	join_button.disabled = true
	if is_join_request:
		join_button.text = "Letting in"
		party.invite(sender.id)
	else:
		join_button.text = "Joining"
		party.join(sender.id)
