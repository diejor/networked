extends HBoxContainer

const Avatar := preload("res://addons/networked_discord/ui/discord_avatar.gd")
const AVATAR := preload("res://addons/networked_discord/ui/discord_avatar.tscn")
const MIC_OFF := preload("res://addons/networked_discord/ui/icons/mic_off.svg")
const HEADSET_OFF := preload(
	"res://addons/networked_discord/ui/icons/headset_off.svg"
)
const VOICE_BADGES: Dictionary[DiscordParty.VoiceState, Texture2D] = {
	DiscordParty.VoiceState.MUTED: MIC_OFF,
	DiscordParty.VoiceState.DEAFENED: HEADSET_OFF,
}

## The width and height of one slot, in pixels.
@export var slot_size := 24

## A larger party draws one fewer slot and a count of the rest.
@export var max_slots := 10

var discord: DiscordSocial:
	set(value):
		discord = value
		discord.party.changed.connect(_on_party_changed)
		discord.party.speaking_changed.connect(_on_party_speaking_changed)
		_on_party_changed()

@onready var more_label: Label = $MoreLabel


func show_slots(users: Array[DiscordProfile], filled: int, total: int) -> void:
	for child: Node in get_children():
		if child is Avatar:
			child.free()

	var gap := get_theme_constant(&"cutout_gap", &"DiscordAvatar") - 1
	var step := slot_size + get_theme_constant(&"separation")
	var drawn := total if total <= max_slots else max_slots - 1
	for index: int in drawn:
		var slot: Avatar = AVATAR.instantiate()
		slot.custom_minimum_size = Vector2(slot_size, slot_size)
		slot.open_slot = index >= filled
		if index < users.size():
			slot.user = users[index]
			slot.name = str(users[index].id)
		if index > 0:
			slot.neighbour_cutout = Vector3(
				slot_size * 0.5 - step,
				slot_size * 0.5,
				slot_size * 0.5 + gap,
			)
		add_child(slot)
		move_child(slot, index)
	more_label.visible = total > drawn
	more_label.text = "+%d" % (total - drawn)


func _on_party_changed() -> void:
	var party := discord.party
	var ids := party.members
	var members: Array[DiscordProfile] = []
	for id: int in ids:
		members.append(discord.profile(id))
	show_slots(
		members,
		members.size(),
		party.max_members if not members.is_empty() else 0,
	)

	for id: int in ids:
		var slot := get_node_or_null(str(id)) as Avatar
		if slot == null:
			continue
		var voice := party.voice_state(id)
		slot.badge = VOICE_BADGES.get(voice)
		slot.dimmed = ids.size() > 1 and voice == DiscordParty.VoiceState.ABSENT
		slot.speaking = party.is_speaking(id)


func _on_party_speaking_changed(member: int, speaking: bool) -> void:
	var slot := get_node_or_null(str(member)) as Avatar
	if slot != null:
		slot.speaking = speaking
