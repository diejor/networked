class_name TestSteamLobbyDirectory
extends NetwTestSuite

func test_recognizes_steam_peer_class() -> void:
	var directory := SteamLobbyDirectory.new()
	assert_str(directory._peer_class()).is_equal("SteamMultiplayerPeer")
	directory.free()


func test_address_form_names_the_lobby_id() -> void:
	var directory := SteamLobbyDirectory.new()
	assert_str(directory.address_label).is_equal("Lobby ID")
	assert_bool(directory.address_help.is_empty()).is_false()
	assert_bool(directory.supports(LobbyDirectory.CAPABILITY_INVITES)).is_true()
	directory.free()


func test_member_updates_use_steam_ids_and_leave_reasons() -> void:
	var directory := SteamLobbyDirectory.new()
	directory.lobby_id = 77
	var joined: Array[int] = []
	var left: Array = []
	directory.member_joined.connect(func(id: int) -> void: joined.append(id))
	directory.member_left.connect(
		func(id: int, reason: SteamLobbyDirectory.LeaveReason) -> void:
			left.append([id, reason])
	)
	directory._on_lobby_chat_update(
		88,
		9001,
		0,
		Steam.CHAT_MEMBER_STATE_CHANGE_ENTERED,
	)
	assert_array(joined).is_empty()
	directory._on_lobby_chat_update(
		77,
		9001,
		0,
		Steam.CHAT_MEMBER_STATE_CHANGE_ENTERED,
	)
	assert_array(joined).contains_exactly([9001])
	var reasons := {
		Steam.CHAT_MEMBER_STATE_CHANGE_LEFT: SteamLobbyDirectory.LeaveReason.LEFT,
		Steam.CHAT_MEMBER_STATE_CHANGE_DISCONNECTED: (
				SteamLobbyDirectory.LeaveReason.DISCONNECTED
		),
		Steam.CHAT_MEMBER_STATE_CHANGE_KICKED: SteamLobbyDirectory.LeaveReason.KICKED,
		Steam.CHAT_MEMBER_STATE_CHANGE_BANNED: SteamLobbyDirectory.LeaveReason.BANNED,
	}
	for state: int in reasons:
		directory._on_lobby_chat_update(77, 9001, 42, state)
		assert_array(left.back()).contains_exactly([9001, reasons[state]])
	assert_int(left.size()).is_equal(4)
	directory._on_lobby_chat_update(
		77,
		9001,
		42,
		Steam.CHAT_MEMBER_STATE_CHANGE_LEFT | Steam.CHAT_MEMBER_STATE_CHANGE_BANNED,
	)
	assert_int(left.back()[1]).is_equal(SteamLobbyDirectory.LeaveReason.BANNED)
	directory.lobby_id = 0
	directory.free()


func test_data_updates_distinguish_lobby_and_member_data() -> void:
	var directory := SteamLobbyDirectory.new()
	directory.lobby_id = 77
	var events: Array[int] = []
	directory.data_changed.connect(func() -> void: events.append(0))
	directory.member_data_changed.connect(func(id: int) -> void: events.append(id))
	directory._on_lobby_data_update(0, 77, 77)
	directory._on_lobby_data_update(1, 88, 88)
	assert_array(events).is_empty()
	directory._on_lobby_data_update(1, 77, 77)
	directory._on_lobby_data_update(1, 77, 9001)
	assert_array(events).contains_exactly([0, 9001])
	directory.lobby_id = 0
	directory.free()


func test_invalid_visibility_fails_before_opening_a_steam_lobby() -> void:
	var directory := SteamLobbyDirectory.new()
	var errors: Array[int] = []
	directory.lobby_failed.connect(
		func(error: Error, _message: String) -> void:
			errors.append(error)
	)
	await directory._host_lobby({ "visibility": 99 })
	assert_array(errors).contains_exactly([ERR_INVALID_PARAMETER])
	assert_int(directory.lobby_id).is_equal(0)
	directory.free()
