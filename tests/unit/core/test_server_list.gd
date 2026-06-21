## Unit tests for [ConnectSession] saved target persistence.
class_name TestServerList
extends NetwTestSuite

func _temp_path() -> String:
	return "user://_test_server_list_%d.tres" % Time.get_ticks_usec()


func _make_target(
		_name: String,
		address: String,
		port: int,
) -> JoinTarget:
	var backend := ENetBackend.new()
	backend.port = port
	var target := JoinTarget.new()
	target.display_name = _name
	target.address = address
	target.backend = backend
	return target


## Verifies that loading a server list from a missing path returns an empty list.
func test_load_server_list_returns_empty_on_missing_path() -> void:
	var session := ConnectSession.new()
	add_child(session)

	session.load_server_list(_temp_path())

	assert_that(session.get_saved_targets()).is_empty()
	session.queue_free()


## Verifies that saving and loading targets successfully roundtrips all properties.
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


## Verifies that loading a legacy text resource (.tres) file does not crash
## and returns an empty/new server list.
func test_load_legacy_text_resource_does_not_crash_and_returns_empty() -> void:
	var path := _temp_path()
	var file := FileAccess.open(path, FileAccess.WRITE)
	assert_object(file).is_not_null()
	file.store_string(
		"[gd_resource type=\"Resource\" script_class=\"ServerList\" format=3]\n"
		+ "[ext_resource type=\"Script\" path=\"res://addons/networked/connect/server_list.gd\" id=\"1_y3nq2\"]\n"
		+ "[resource]\n"
		+ "script = ExtResource(\"1_y3nq2\")\n"
		+ "targets = []\n"
	)
	file.close()

	var session := ConnectSession.new()
	add_child(session)
	session.load_server_list(path)

	assert_object(session.server_list).is_not_null()
	assert_that(session.get_saved_targets()).is_empty()
	DirAccess.remove_absolute(ProjectSettings.globalize_path(path))
	session.queue_free()


## Verifies that ServerList resource can be saved and loaded directly.
func test_server_list_direct_serialization() -> void:
	var path := _temp_path()
	var list := ServerList.new()
	list.targets.append(_make_target("Direct Dusk", "192.168.1.100", 8000))

	var save_err := ServerList.save(list, path)
	assert_int(save_err).is_equal(OK)

	var loaded := ServerList.load_or_new(path)
	assert_object(loaded).is_not_null()
	assert_int(loaded.targets.size()).is_equal(1)
	assert_that(loaded.targets[0].display_name).is_equal("Direct Dusk")

	DirAccess.remove_absolute(ProjectSettings.globalize_path(path))
