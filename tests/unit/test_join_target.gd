## Unit tests for [JoinTarget].
class_name TestJoinTarget
extends NetwTestSuite

func test_make_backend_instance_returns_null_when_template_missing() -> void:
	var t := JoinTarget.new()
	assert_that(t.make_backend_instance()).is_null()


func test_make_backend_instance_returns_distinct_copy() -> void:
	var template := ENetBackend.new()
	template.port = 12345
	var t := JoinTarget.new()
	t.backend = template

	var a := t.make_backend_instance()
	var b := t.make_backend_instance()

	assert_that(a).is_not_null()
	assert_that(b).is_not_null()
	assert_bool(a == template).is_false()
	assert_bool(a == b).is_false()
	assert_int((a as ENetBackend).port).is_equal(12345)


func test_tres_roundtrip_preserves_fields() -> void:
	var template := ENetBackend.new()
	template.port = 7777
	var original := JoinTarget.new()
	original.display_name = "Co-op Sandbox"
	original.backend = template
	original.address = "play.example.com"
	original.metadata = { "region": "eu" }

	var path := "user://_test_join_target_%d.tres" % Time.get_ticks_usec()
	var save_err := ResourceSaver.save(original, path)
	assert_int(save_err).is_equal(OK)

	var loaded := ResourceLoader.load(
		path,
		"JoinTarget",
		ResourceLoader.CACHE_MODE_IGNORE,
	) as JoinTarget
	assert_that(loaded).is_not_null()
	assert_that(loaded.display_name).is_equal("Co-op Sandbox")
	assert_that(loaded.address).is_equal("play.example.com")
	assert_that(loaded.metadata.get("region", "")).is_equal("eu")
	assert_that(loaded.backend).is_not_null()
	assert_int((loaded.backend as ENetBackend).port).is_equal(7777)

	DirAccess.remove_absolute(ProjectSettings.globalize_path(path))
