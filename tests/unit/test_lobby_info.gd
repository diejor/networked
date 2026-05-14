## Unit tests for [LobbyInfo].
class_name TestLobbyInfo
extends NetworkedTestSuite


func test_make_populates_required_fields() -> void:
	var info := LobbyInfo.make(123, "Room", 2, 8)
	assert_that(info.id).is_equal(123)
	assert_that(info.lobby_name).is_equal("Room")
	assert_that(info.players).is_equal(2)
	assert_that(info.max_players).is_equal(8)
	assert_that(info.metadata).is_empty()


func test_make_carries_metadata() -> void:
	var info := LobbyInfo.make(
		7, "X", 1, 4, {"host": "alice", "mode": "deathmatch"}
	)
	assert_that(info.metadata["host"]).is_equal("alice")
	assert_that(info.metadata["mode"]).is_equal("deathmatch")


func test_defaults_are_zero_values() -> void:
	var info := LobbyInfo.new()
	assert_that(info.id).is_equal(0)
	assert_that(info.lobby_name).is_equal("")
	assert_that(info.players).is_equal(0)
	assert_that(info.max_players).is_equal(0)
