## Unit tests for [DictionaryEntity] persistence and serialization.
class_name TestDictionarySave
extends NetworkedTestSuite

var entity: DictionaryEntity


func before_test() -> void:
	entity = DictionaryEntity.new()


func test_set_and_get_value() -> void:
	entity.set_value(&"health", 100)
	assert_that(entity.get_value(&"health")).is_equal(100)


func test_has_value_true_after_set() -> void:
	entity.set_value(&"name", "alice")
	assert_that(entity.has_value(&"name")).is_true()


func test_has_value_false_for_missing() -> void:
	assert_that(entity.has_value(&"nonexistent")).is_false()


func test_get_value_default_for_missing() -> void:
	assert_that(entity.get_value(&"missing", 42)).is_equal(42)


func test_get_value_null_default_for_missing() -> void:
	assert_that(entity.get_value(&"missing")).is_null()


func test_serialize_deserialize_round_trip() -> void:
	entity.set_value(&"score", 999)
	entity.set_value(&"label", "test")
	var bytes := entity.serialize()

	var loaded := DictionaryEntity.new()
	loaded.deserialize(bytes)
	assert_that(loaded.get_value(&"score")).is_equal(999)
	assert_that(loaded.get_value(&"label")).is_equal("test")


func test_serialize_preserves_types() -> void:
	entity.set_value(&"vec", Vector2(3.0, 4.0))
	entity.set_value(&"num", 7)
	entity.set_value(&"str", "hello")

	var loaded := DictionaryEntity.new()
	loaded.deserialize(entity.serialize())

	var vec: Variant = loaded.get_value(&"vec")
	assert_that(vec is Vector2).is_true()
	assert_that(vec).is_equal(Vector2(3.0, 4.0))
	assert_that(loaded.get_value(&"num") is int).is_true()
	assert_that(loaded.get_value(&"str") is String).is_true()


func test_to_dict_returns_all_values() -> void:
	entity.set_value(&"a", 1)
	entity.set_value(&"b", "two")
	var dict := entity.to_dict()
	assert_that(dict.get(&"a")).is_equal(1)
	assert_that(dict.get(&"b")).is_equal("two")


func test_from_dict_populates_entity() -> void:
	entity.from_dict({&"x": 10, &"y": 20})
	assert_that(entity.get_value(&"x")).is_equal(10)
	assert_that(entity.get_value(&"y")).is_equal(20)


func test_get_property_names() -> void:
	entity.set_value(&"a", 1)
	entity.set_value(&"b", 2)
	entity.set_value(&"c", 3)
	var names := entity.get_property_names()
	assert_that(names.size()).is_equal(3)
	assert_that(names.has(&"a")).is_true()
	assert_that(names.has(&"b")).is_true()
	assert_that(names.has(&"c")).is_true()


func test_for_in_iteration() -> void:
	entity.set_value(&"x", 10)
	entity.set_value(&"y", 20)
	var visited: Array[StringName] = []
	for key in entity:
		visited.append(key)
	assert_that(visited.size()).is_equal(2)
	assert_that(visited.has(&"x")).is_true()
	assert_that(visited.has(&"y")).is_true()


func test_empty_iteration() -> void:
	var visited: Array[StringName] = []
	for key in entity:
		visited.append(key)
	assert_that(visited.size()).is_equal(0)


func test_entity_inheritance() -> void:
	assert_that(entity is Entity).is_true()
