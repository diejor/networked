## Unit tests for [NetwInterest]. The facade is 3 methods deep; the
## real API lives on [NetwInterestLayer].
class_name TestNetwInterest
extends NetwTestSuite


var mt: MultiplayerTree
var interest: NetwInterest


func before_test() -> void:
	mt = MultiplayerTree.new()
	mt.name = "TestTree"
	add_child(mt)
	auto_free(mt)
	interest = mt.interest


func test_layer_creates_on_first_use() -> void:
	var a := interest.layer(&"a")
	assert_that(a).is_not_null()
	assert_that(a.layer_id).is_equal(&"a")


func test_layer_returns_same_instance() -> void:
	var a := interest.layer(&"a")
	var b := interest.layer(&"a")
	assert_that(a).is_equal(b)


func test_get_layer_returns_null_for_missing_layer() -> void:
	assert_that(interest.get_layer(&"missing")).is_null()


func test_get_layer_returns_existing() -> void:
	var created := interest.layer(&"x")
	assert_that(interest.get_layer(&"x")).is_equal(created)


func test_all_layers_lists_created_layers() -> void:
	interest.layer(&"a")
	interest.layer(&"b")
	var ids: Array[StringName] = []
	for layer: NetwInterestLayer in interest.all_layers():
		ids.append(layer.layer_id)
	ids.sort()
	assert_that(ids).contains_exactly([&"a", &"b"])
