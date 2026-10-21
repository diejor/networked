extends Button

const Avatar := preload("res://addons/networked_discord/ui/discord_avatar.gd")

var party: DiscordParty
var user: DiscordProfile

@onready var avatar: Avatar = %Avatar
@onready var name_label: Label = %NameLabel
@onready var game_icon: TextureRect = %GameIcon
@onready var status_label: Label = %StatusLabel
@onready var action_button: Button = %ActionButton


func _ready() -> void:
	avatar.user = user
	user.changed.connect(_on_user_changed)
	party.changed.connect(_on_user_changed)
	action_button.pressed.connect(_on_action_button_pressed)
	_on_user_changed()


func _on_user_changed() -> void:
	name_label.text = user.display_name
	game_icon.visible = user.is_playing
	var game: String = ProjectSettings.get_setting("application/config/name")
	var status: String = DiscordProfile.Status.find_key(user.status)
	status_label.text = game if user.is_playing else status.capitalize()

	action_button.visible = user.is_playing \
			or user.status != DiscordProfile.Status.OFFLINE
	action_button.disabled = user.id in party.members
	action_button.text = "Join" if user.is_playing else "Invite"
	if action_button.disabled:
		action_button.text = "In party"


func _on_action_button_pressed() -> void:
	action_button.disabled = true
	if user.is_playing:
		action_button.text = "Asked"
		party.join(user.id)
	else:
		action_button.text = "Invited"
		party.invite(user.id)
