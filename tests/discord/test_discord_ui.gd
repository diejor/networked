extends GdUnitTestSuite

const APPLICATION_ID := 1561065725962092666
const SECRET := "p1|" + "0123456789abcdef0123456789abcdef" \
		+ "0123456789abcdef0123456789abcdef"
const INVITE_LIST := preload(
	"res://addons/networked_discord/ui/discord_invite_list.tscn"
)
const PANEL := preload("res://addons/networked_discord/ui/discord_panel.tscn")

var viewport: MultiplayerViewport


func before_test() -> void:
	viewport = MultiplayerViewport.new()
	add_child(viewport)


func after_test() -> void:
	viewport.free()
	await get_tree().process_frame


func press_toggle(target: Node) -> void:
	var key := InputEventKey.new()
	key.keycode = KEY_QUOTELEFT
	key.shift_pressed = true
	key.unicode = "~".unicode_at(0)
	key.pressed = true
	target.get_viewport().push_input(key)


func discord() -> DiscordSocial:
	var social := DiscordSocial.new()
	social.application_id = APPLICATION_ID
	viewport.add_child(social)
	return social


func offer(sender: int) -> void:
	var invite := DiscordActivityInvite.new()
	invite.set_sender_id(sender)
	invite.set_message_id(sender)
	invite.set_party_id(SECRET.sha256_text())
	invite.set_type(DiscordActivityActionTypes.JOIN)
	invite.set_is_valid(true)
	DiscordParty._on_activity_invite_created(invite)


func test_the_shortcut_hides_the_button_collapses_and_an_invite_shows() -> void:
	discord()
	var panel: PanelContainer = PANEL.instantiate()
	viewport.add_child(panel)
	var started: bool = panel.minimized
	panel.toggle_button.pressed.emit()
	assert_bool(panel.minimized).is_not_equal(started)
	assert_bool(panel.visible).is_true()
	press_toggle(panel)
	assert_bool(panel.visible).is_false()
	assert_bool(panel.minimized).is_not_equal(started)
	offer(7)
	assert_bool(panel.visible).is_true()
	panel.minimized = started

	var field := LineEdit.new()
	viewport.add_child(field)
	field.grab_focus()
	press_toggle(panel)
	assert_bool(panel.visible).is_true()


func test_an_invitation_card_leaves_once_accepted_anywhere() -> void:
	var list: VBoxContainer = INVITE_LIST.instantiate()
	viewport.add_child(list)
	list.discord = discord()
	offer(7)
	await get_tree().process_frame
	assert_int(list.get_child_count()).is_equal(1)
	var button: Button = list.get_child(0).find_child("JoinButton")
	assert_bool(button.disabled).is_false()
	button.pressed.emit()
	assert_bool(button.disabled).is_true()

	DiscordParty._on_activity_join(SECRET)
	await get_tree().process_frame
	assert_int(list.get_child_count()).is_zero()
