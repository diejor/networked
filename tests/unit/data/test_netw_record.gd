extends NetwTestSuite

func test_dictionary_record_round_trips_values() -> void:
	var record := DictionaryRecord.new()
	record.set_value(&"health", 100)
	record.set_value(&"position", Vector2(10, 20))

	var copy := DictionaryRecord.new()
	copy.from_dict(record.to_dict())

	assert_int(copy.get_value(&"health")).is_equal(100)
	assert_vector(copy.get_value(&"position")).is_equal(Vector2(10, 20))


func test_dictionary_record_supports_dynamic_property_access() -> void:
	var record := DictionaryRecord.new()

	record.health = 75

	assert_bool(record.has_value(&"health")).is_true()
	assert_int(record.health).is_equal(75)


func test_dictionary_record_reports_empty_state() -> void:
	var record := DictionaryRecord.new()

	assert_bool(record.is_empty()).is_true()

	record.set_value(&"health", 1)

	assert_bool(record.is_empty()).is_false()


func test_snapshot_is_detached_dictionary_record() -> void:
	var values := { &"position": Vector2(3, 4) }

	var snapshot := NetwSnapshot.from_dictionary(values)
	snapshot.position = Vector2(9, 9)

	assert_vector(values[&"position"]).is_equal(Vector2(3, 4))
	assert_vector(snapshot.position).is_equal(Vector2(9, 9))
