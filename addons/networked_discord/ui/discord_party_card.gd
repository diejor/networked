extends PanelContainer

const SlotRow := preload(
	"res://addons/networked_discord/ui/discord_slot_row.gd"
)
const ElapsedLabel := preload(
	"res://addons/networked_discord/ui/discord_elapsed_label.gd"
)

var discord: DiscordSocial:
	set(value):
		discord = value
		slot_row.discord = discord
		discord.friends_changed.connect(_on_discord_friends_changed)
		discord.party.changed.connect(_on_party_changed)
		leave_button.pressed.connect(discord.party.leave)
		_on_discord_friends_changed()
		_on_party_changed()

@onready var name_label: Label = %NameLabel
@onready var size_label: Label = %SizeLabel
@onready var elapsed_label: ElapsedLabel = %ElapsedLabel
@onready var slot_row: SlotRow = %SlotRow
@onready var leave_button: Button = %LeaveButton


func _ready() -> void:
	name_label.text = ProjectSettings.get_setting("application/config/name")


func _on_discord_friends_changed() -> void:
	visible = discord.user_id != 0
	elapsed_label.user = discord.profile(discord.user_id)


func _on_party_changed() -> void:
	var members := discord.party.members.size()
	size_label.text = "%d of %d" % [members, discord.party.max_members]
	leave_button.visible = members > 1
