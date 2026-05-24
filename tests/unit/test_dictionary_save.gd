## Unit tests for [DictionaryEntity] persistence and serialization.
class_name TestDictionarySave
extends NetwTestSuite


func test_inheritance_and_properties() -> void:
	var entity := DictionaryEntity.new()
	assert_that(entity is Entity).is_true()

	# Get default/null for missing values
	assert_that(entity.has_value(&"missing")).is_false()
	assert_that(entity.get_value(&"missing")).is_null()
	assert_that(entity.get_value(&"missing", 42)).is_equal(42)

	# Set and get values
	entity.set_value(&"health", 100)
	assert_that(entity.has_value(&"health")).is_true()
	assert_that(entity.get_value(&"health")).is_equal(100)


func test_dict_conversions_and_iteration() -> void:
	var entity := DictionaryEntity.new()

	# Empty iteration
	var visited: Array[StringName] = []
	for key in entity:
		visited.append(key)
	assert_that(visited.is_empty()).is_true()

	# Populated conversions
	entity.from_dict({&"x": 10, &"y": 20})
	assert_that(entity.get_value(&"x")).is_equal(10)
	assert_that(entity.get_value(&"y")).is_equal(20)

	var dict := entity.to_dict()
	assert_that(dict.get(&"x")).is_equal(10)
	assert_that(dict.get(&"y")).is_equal(20)

	# Property names
	var names := entity.get_property_names()
	assert_that(names.size()).is_equal(2)
	assert_that(names.has(&"x")).is_true()
	assert_that(names.has(&"y")).is_true()

	# Populated iteration
	visited.clear()
	for key in entity:
		visited.append(key)
	assert_that(visited.size()).is_equal(2)
	assert_that(visited.has(&"x")).is_true()
	assert_that(visited.has(&"y")).is_true()


func test_serialization_round_trip(
	key: StringName,
	value: Variant,
	test_parameters := [
		[&"num", 7],
		[&"str", "hello"],
		[&"vec", Vector2(3.0, 4.0)],
	]
) -> void:
	var entity := DictionaryEntity.new()
	entity.set_value(key, value)

	var bytes := entity.serialize()
	var loaded := DictionaryEntity.new()
	loaded.deserialize(bytes)

	assert_that(loaded.get_value(key)).is_equal(value)
	# Check type preservation
	assert_that(typeof(loaded.get_value(key))).is_equal(typeof(value))
