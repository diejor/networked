## Checks the transport identity [SteamLobbyDirectory] declares, which is what
## the session adapts it into a transport by. Reaches no Steam runtime.
class_name TestSteamLobbyDirectory
extends NetwTestSuite

func test_recognizes_steam_peer_class() -> void:
	var dir := SteamLobbyDirectory.new()
	assert_str(dir._peer_class()).is_equal("SteamMultiplayerPeer")
	dir.free()


# Steam has no web export, so availability follows the platform web feature.
func test_is_available_excludes_web() -> void:
	var dir := SteamLobbyDirectory.new()
	assert_bool(dir._is_available()).is_equal(not OS.has_feature("web"))
	dir.free()


# A lobby id is not something a player can type unaided, so the form labels it
# and says where one comes from.
func test_address_form_names_the_lobby_id() -> void:
	var dir := SteamLobbyDirectory.new()
	assert_str(dir._address_label()).is_equal("Lobby ID")
	assert_bool(dir._address_help().is_empty()).is_false()
	dir.free()
