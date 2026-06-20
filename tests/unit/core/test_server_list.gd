## Unit tests for [ConnectSession] saved target persistence.
class_name TestServerList
extends NetwTestSuite

func _temp_path() -> String:
	return "user://_test_server_list_%d.tres" % Time.get_ticks_usec()


func _make_target(
		name: String,
		address: String,
		port: int,
) -> JoinTarget:
	var backend := ENetBackend.new()
	backend.port = port
	var target := JoinTarget.new()
	target.display_name = name
	target.address = address
	target.backend = backend
	return target


func test_load_server_list_returns_empty_on_missing_path() -> void:
	var session := ConnectSession.new()
	add_child(session)

	session.load_server_list(_temp_path())

	assert_that(session.get_saved_targets()).is_empty()
	session.queue_free()


func test_save_then_load_roundtrips_targets() -> void:
	var path := _temp_path()
	var session := ConnectSession.new()
	add_child(session)
	session.server_list_path = path

	session.add_target(_make_target("Dusk LAN", "192.168.1.42", 7000))
	session.add_target(_make_target("Katie Lobby", "10.0.0.4", 7001))
	var save_err := session.save_server_list(path)
	assert_int(save_err).is_equal(OK)

	var loaded := ConnectSession.new()
	add_child(loaded)
	loaded.load_server_list(path)
	var targets := loaded.get_saved_targets()
	assert_that(targets.size()).is_equal(2)
	assert_that(targets[0].display_name).is_equal("Dusk LAN")
	assert_that(targets[0].address).is_equal("192.168.1.42")
	assert_int((targets[0].backend as ENetBackend).port).is_equal(7000)
	assert_that(targets[1].display_name).is_equal("Katie Lobby")
	assert_that(targets[1].address).is_equal("10.0.0.4")

	DirAccess.remove_absolute(ProjectSettings.globalize_path(path))
	session.queue_free()
	loaded.queue_free()
