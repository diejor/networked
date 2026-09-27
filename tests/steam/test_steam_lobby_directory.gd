class_name TestSteamLobbyDirectory
extends NetwTestSuite

func test_recognizes_steam_peer_class() -> void:
	var dir := SteamLobbyDirectory.new()
	assert_str(dir._peer_class()).is_equal("SteamMultiplayerPeer")
	dir.free()


func test_is_available_excludes_web() -> void:
	var dir := SteamLobbyDirectory.new()
	assert_bool(dir._is_available()).is_equal(not OS.has_feature("web"))
	dir.free()


func test_address_form_names_the_lobby_id() -> void:
	var dir := SteamLobbyDirectory.new()
	assert_str(dir._address_label()).is_equal("Lobby ID")
	assert_bool(dir._address_help().is_empty()).is_false()
	dir.free()
