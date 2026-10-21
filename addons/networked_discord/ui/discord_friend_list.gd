extends VBoxContainer

const Row := preload(
	"res://addons/networked_discord/ui/discord_friend_row.gd"
)
const ROW := preload(
	"res://addons/networked_discord/ui/discord_friend_row.tscn"
)
const DASH := " \U002014 "

var discord: DiscordSocial:
	set(value):
		discord = value
		discord.friends_changed.connect(_on_discord_friends_changed)
		_on_discord_friends_changed()
var rows: Dictionary[int, Row]

@onready var sign_in_button: Button = %SignInButton
@onready var search_edit: LineEdit = %SearchEdit
@onready var empty_label: Label = %EmptyLabel
@onready var scroll: ScrollContainer = %Scroll
@onready var playing_header: Label = %PlayingHeader
@onready var playing: VBoxContainer = %Playing
@onready var online_header: Label = %OnlineHeader
@onready var online: VBoxContainer = %Online
@onready var offline_header: Label = %OfflineHeader
@onready var offline: VBoxContainer = %Offline


func _ready() -> void:
	sign_in_button.pressed.connect(_on_sign_in_button_pressed)
	search_edit.text_changed.connect(_on_search_edit_text_changed)


func _on_sign_in_button_pressed() -> void:
	sign_in_button.disabled = true
	sign_in_button.text = "Signing in"
	await discord.sign_in().wait()
	sign_in_button.disabled = false
	sign_in_button.text = "Sign in with Discord"


func _on_search_edit_text_changed(_text: String) -> void:
	_on_discord_friends_changed()


func _on_discord_friends_changed() -> void:
	var signed_in := discord.user_id != 0
	var friends := discord.friends
	var query := search_edit.text.strip_edges().to_lower()
	sign_in_button.visible = not signed_in
	search_edit.visible = signed_in and not friends.is_empty()
	scroll.visible = signed_in

	for id: int in rows.keys():
		if id not in friends:
			rows[id].queue_free()
			rows.erase(id)
	for id: int in friends:
		if not rows.has(id):
			rows[id] = ROW.instantiate()
			rows[id].party = discord.party
			rows[id].user = discord.profile(id)
			offline.add_child(rows[id])
	if not query.is_empty():
		friends.sort_custom(
			func(a: int, b: int) -> bool:
				return query.similarity(rows[a].user.display_name.to_lower()) \
						> query.similarity(rows[b].user.display_name.to_lower())
		)

	var shown: Dictionary[VBoxContainer, int] = {
		playing: 0,
		online: 0,
		offline: 0,
	}
	for id: int in friends:
		var row := rows[id]
		var section := offline
		if row.user.is_playing:
			section = playing
		elif row.user.status != DiscordProfile.Status.OFFLINE:
			section = online
		row.visible = query.is_subsequence_ofn(row.user.display_name)
		row.reparent(section)
		section.move_child(row, -1)
		shown[section] += int(row.visible)

	var game: String = ProjectSettings.get_setting("application/config/name")
	playing_header.text = "Playing " + game + DASH + str(shown[playing])
	playing_header.visible = shown[playing] > 0
	online_header.text = "Online" + DASH + str(shown[online])
	online_header.visible = shown[online] > 0
	offline_header.text = "Offline" + DASH + str(shown[offline])
	offline_header.visible = shown[offline] > 0
	empty_label.visible = signed_in \
			and shown[playing] + shown[online] + shown[offline] == 0
	empty_label.text = "No friends match" if not friends.is_empty() \
			else "No Discord friends yet"
