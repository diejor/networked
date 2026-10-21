extends GdUnitTestSuite

const APPLICATION_ID := 1561065725962092666

var environment: Dictionary[String, String]
var viewport: MultiplayerViewport


func before_test() -> void:
	var runtime := ProjectSettings.globalize_path(
		"res://tmp/discord-tests/xdg_runtime_dir",
	)
	DirAccess.make_dir_recursive_absolute(runtime)
	environment["XDG_RUNTIME_DIR"] = OS.get_environment("XDG_RUNTIME_DIR")
	OS.set_environment("XDG_RUNTIME_DIR", runtime)

	viewport = MultiplayerViewport.new()
	add_child(viewport)


func after_test() -> void:
	viewport.free()
	await get_tree().process_frame
	for key: String in environment:
		if environment[key].is_empty():
			OS.unset_environment(key)
		else:
			OS.set_environment(key, environment[key])
	environment.clear()


func offer(
		sender: int,
		message: int,
		party_secret: String,
		kind := DiscordActivityActionTypes.JOIN,
) -> void:
	var invite := DiscordActivityInvite.new()
	invite.set_sender_id(sender)
	invite.set_message_id(message)
	invite.set_party_id(party_secret.sha256_text())
	invite.set_type(kind)
	invite.set_is_valid(true)
	DiscordParty._on_activity_invite_created(invite)


func expire(message: int) -> void:
	var gone := DiscordActivityInvite.new()
	gone.set_message_id(message)
	gone.set_is_valid(false)
	DiscordParty._on_activity_invite_updated(gone)


func test_an_invitation_leaves_the_list_once_answered_or_gone() -> void:
	var social := DiscordSocial.new()
	social.application_id = APPLICATION_ID
	viewport.add_child(social)
	var party := social.party
	var invited: Array[int] = []
	party.invited.connect(invited.append)
	var joined := "p1|" + "a".repeat(64)
	offer(7, 1, joined)
	offer(8, 2, "p1|" + "b".repeat(64))
	offer(8, 3, "p1|" + "c".repeat(64))
	offer(9, 4, joined, DiscordActivityActionTypes.JOIN_REQUEST)
	assert_array(invited).contains_exactly([7, 8, 8])
	assert_array(party.invitations).contains_exactly([7, 8])
	assert_array(party.join_requests).contains_exactly([9])

	DiscordParty._on_activity_join(joined)
	assert_array(party.invitations).contains_exactly([8])
	expire(2)
	assert_array(party.invitations).contains_exactly([8])
	expire(3)
	assert_array(party.invitations).is_empty()
	social.signed_out.emit()
	assert_array(party.join_requests).is_empty()
