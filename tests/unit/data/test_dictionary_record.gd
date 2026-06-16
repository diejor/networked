## Unit tests for [DictionaryRecord] persistence and serialization.
class_name TestDictionaryRecord
extends NetwTestSuite

func test_inheritance_and_properties() -> void:
	var record := DictionaryRecord.new()
	assert_that(record is NetwRecord).is_true()

	assert_that(record.has_value(&"missing")).is_false()
	assert_that(record.get_value(&"missing")).is_null()
	assert_that(record.get_value(&"missing", 42)).is_equal(42)

	record.set_value(&"health", 100)

	assert_that(record.has_value(&"health")).is_true()
	assert_that(record.get_value(&"health")).is_equal(100)


func test_dict_conversions_and_iteration() -> void:
	var record := DictionaryRecord.new()

	var visited: Array[StringName] = []
	for key in record:
		visited.append(key)
	assert_that(visited.is_empty()).is_true()

	record.from_dict({ &"x": 10, &"y": 20 })
	assert_that(record.get_value(&"x")).is_equal(10)
	assert_that(record.get_value(&"y")).is_equal(20)

	var dict := record.to_dict()
	assert_that(dict.get(&"x")).is_equal(10)
	assert_that(dict.get(&"y")).is_equal(20)

	var names := record.get_property_names()
	assert_that(names.size()).is_equal(2)
	assert_that(names.has(&"x")).is_true()
	assert_that(names.has(&"y")).is_true()

	visited.clear()
	for key in record:
		visited.append(key)
	assert_that(visited.size()).is_equal(2)
	assert_that(visited.has(&"x")).is_true()
	assert_that(visited.has(&"y")).is_true()


@warning_ignore("unused_parameter")
func test_serialization_round_trip(
		key: StringName,
		value: Variant,
		test_parameters := [
			[&"num", 7],
			[&"str", "hello"],
			[&"vec", Vector2(3.0, 4.0)],
		],
) -> void:
	var record := DictionaryRecord.new()
	record.set_value(key, value)

	var bytes := record.serialize()
	var loaded := DictionaryRecord.new()
	loaded.deserialize(bytes)

	assert_that(loaded.get_value(key)).is_equal(value)
	assert_that(typeof(loaded.get_value(key))).is_equal(typeof(value))
