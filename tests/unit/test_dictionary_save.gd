class_name TestDictionarySave
extends NetworkedTestSuite

var save: DictionarySave


func before_test() -> void:
	save = DictionarySave.new()


func test_set_and_get_value() -> void:
	save.set_value(&"health", 100)
	assert_that(save.get_value(&"health")).is_equal(100)


func test_has_value_true_after_set() -> void:
	save.set_value(&"name", "alice")
	assert_that(save.has_value(&"name")).is_true()


func test_has_value_false_for_missing() -> void:
	assert_that(save.has_value(&"nonexistent")).is_false()


func test_get_value_default_for_missing() -> void:
	assert_that(save.get_value(&"missing", 42)).is_equal(42)


func test_get_value_null_default_for_missing() -> void:
	assert_that(save.get_value(&"missing")).is_null()


func test_serialize_deserialize_round_trip() -> void:
	save.set_value(&"score", 999)
	save.set_value(&"label", "test")
	var bytes := save.serialize()

	var loaded := DictionarySave.new()
	loaded.deserialize(bytes)
	assert_that(loaded.get_value(&"score")).is_equal(999)
	assert_that(loaded.get_value(&"label")).is_equal("test")


func test_serialize_preserves_types() -> void:
	save.set_value(&"vec", Vector2(3.0, 4.0))
	save.set_value(&"num", 7)
	save.set_value(&"str", "hello")

	var loaded := DictionarySave.new()
	loaded.deserialize(save.serialize())

	var vec: Variant = loaded.get_value(&"vec")
	assert_that(vec is Vector2).is_true()
	assert_that(vec).is_equal(Vector2(3.0, 4.0))
	assert_that(loaded.get_value(&"num") is int).is_true()
	assert_that(loaded.get_value(&"str") is String).is_true()


func test_get_property_names() -> void:
	save.set_value(&"a", 1)
	save.set_value(&"b", 2)
	save.set_value(&"c", 3)
	var names := save.get_property_names()
	assert_that(names.size()).is_equal(3)
	assert_that(names.has(&"a")).is_true()
	assert_that(names.has(&"b")).is_true()
	assert_that(names.has(&"c")).is_true()


func test_for_in_iteration() -> void:
	save.set_value(&"x", 10)
	save.set_value(&"y", 20)
	var visited: Array[StringName] = []
	for key in save:
		visited.append(key)
	assert_that(visited.size()).is_equal(2)
	assert_that(visited.has(&"x")).is_true()
	assert_that(visited.has(&"y")).is_true()


func test_empty_iteration() -> void:
	var visited: Array[StringName] = []
	for key in save:
		visited.append(key)
	assert_that(visited.size()).is_equal(0)
