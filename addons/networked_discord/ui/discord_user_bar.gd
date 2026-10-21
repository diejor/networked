extends PanelContainer

const Avatar := preload("res://addons/networked_discord/ui/discord_avatar.gd")
const MIC := preload("res://addons/networked_discord/ui/icons/mic.svg")
const MIC_OFF := preload("res://addons/networked_discord/ui/icons/mic_off.svg")
const HEADSET := preload("res://addons/networked_discord/ui/icons/headset.svg")
const HEADSET_OFF := preload(
	"res://addons/networked_discord/ui/icons/headset_off.svg"
)

var discord: DiscordSocial:
	set(value):
		discord = value
		discord.friends_changed.connect(_on_discord_friends_changed)
		discord.party.changed.connect(_on_party_changed)
		discord.party.speaking_changed.connect(_on_party_speaking_changed)
		_on_discord_friends_changed()
		_on_party_changed()

@onready var avatar: Avatar = %Avatar
@onready var name_label: Label = %NameLabel
@onready var game_icon: TextureRect = %GameIcon
@onready var status_label: Label = %StatusLabel
@onready var mute_button: Button = %MuteButton
@onready var deafen_button: Button = %DeafenButton


func _ready() -> void:
	mute_button.pressed.connect(_on_mute_button_pressed)
	deafen_button.pressed.connect(_on_deafen_button_pressed)


func _on_discord_friends_changed() -> void:
	visible = discord.user_id != 0
	avatar.user = discord.profile(discord.user_id)
	if not avatar.user.changed.is_connected(_on_player_changed):
		avatar.user.changed.connect(_on_player_changed)
	_on_player_changed()


func _on_player_changed() -> void:
	var player := avatar.user
	name_label.text = player.display_name
	game_icon.visible = player.is_playing
	var game: String = ProjectSettings.get_setting("application/config/name")
	var status: String = DiscordProfile.Status.find_key(player.status)
	status_label.text = game if player.is_playing else status.capitalize()


func _on_party_changed() -> void:
	var party := discord.party
	var silent := party.muted or party.deafened
	mute_button.icon = MIC_OFF if silent else MIC
	mute_button.tooltip_text = "Unmute" if silent else "Mute"
	deafen_button.icon = HEADSET_OFF if party.deafened else HEADSET
	deafen_button.tooltip_text = "Undeafen" if party.deafened else "Deafen"


func _on_party_speaking_changed(member: int, speaking: bool) -> void:
	if member == discord.user_id:
		avatar.speaking = speaking


func _on_mute_button_pressed() -> void:
	var silent := discord.party.muted or discord.party.deafened
	discord.party.deafened = false
	discord.party.muted = not silent


func _on_deafen_button_pressed() -> void:
	discord.party.deafened = not discord.party.deafened
