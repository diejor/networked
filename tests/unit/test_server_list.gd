## Unit tests for [ServerList].
class_name TestServerList
extends NetwTestSuite


func _temp_path() -> String:
	return "user://_test_server_list_%d.tres" % Time.get_ticks_usec()


func test_load_or_new_returns_empty_on_missing_path() -> void:
	var path := _temp_path()
	var list := ServerList.load_or_new(path)
	assert_that(list).is_not_null()
	assert_that(list.targets).is_empty()


func test_save_then_load_roundtrips_targets() -> void:
	var path := _temp_path()

	var backend := ENetBackend.new()
	backend.port = 7000

	var t1 := JoinTarget.new()
	t1.display_name = "Dusk LAN"
	t1.address = "192.168.1.42"
	t1.backend = backend

	var t2 := JoinTarget.new()
	t2.display_name = "katie's lobby"
	t2.provider_id = &"steam"
	t2.remote_id = 76561197960265728

	var list := ServerList.new()
	list.targets = [t1, t2]

	var save_err := ServerList.save(list, path)
	assert_int(save_err).is_equal(OK)

	var loaded := ServerList.load_or_new(path)
	assert_that(loaded.targets.size()).is_equal(2)
	assert_that(loaded.targets[0].display_name).is_equal("Dusk LAN")
	assert_bool(loaded.targets[0].is_direct()).is_true()
	assert_int((loaded.targets[0].backend as ENetBackend).port).is_equal(7000)
	assert_that(loaded.targets[1].provider_id).is_equal(&"steam")
	assert_bool(loaded.targets[1].is_direct()).is_false()

	DirAccess.remove_absolute(ProjectSettings.globalize_path(path))


func test_save_rejects_null() -> void:
	var err := ServerList.save(null, _temp_path())
	assert_int(err).is_equal(ERR_INVALID_PARAMETER)
