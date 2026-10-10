@tool
class_name TestConnectFieldList
extends NetwTestSuite

const ERROR_SCRUBBER := preload(
	"res://addons/networked_test/gdunit4/gdunit_error_scrubber.gd"
)


func test_settings_render_one_field_each_and_read_back() -> void:
	var container := VBoxContainer.new()
	add_child(container)
	var fields := ConnectFieldList.new()

	fields.render_settings(
		container,
		{ port = 21253, relay = true, region = "eu", hook = Callable() },
	)

	assert_int(container.get_child_count()).is_equal(3)
	assert_dict(fields.as_dictionary()).is_equal(
		{ port = 21253, relay = true, region = "eu" },
	)
	container.free()


func test_a_join_schema_reads_back_in_order_and_typed() -> void:
	var container := VBoxContainer.new()
	add_child(container)
	var fields := ConnectFieldList.new()

	fields.render_schema(
		container,
		[
			{ name = "team", type = TYPE_STRING_NAME },
			{ name = "level", type = TYPE_INT },
		],
	)

	assert_array(fields.as_array()).is_equal([&"", 0])
	container.free()


func test_a_string_list_field_round_trips_through_its_control() -> void:
	var authored := PackedStringArray(
		[
			"wss://tracker.openwebtorrent.com",
			"wss://tracker.webtorrent.dev",
		],
	)
	var control := ConnectFieldList.make_value_control(authored)
	var read_back: Variant = ConnectFieldList.value_from_control(
		control,
		TYPE_PACKED_STRING_ARRAY,
	)

	assert_that(read_back).is_equal(authored)
	control.free()


func test_a_string_list_has_one_editable_row_per_entry() -> void:
	var control := ConnectFieldList.make_value_control(
		PackedStringArray(["wss://one.example"]),
	)
	add_child(control)
	await get_tree().process_frame

	(control.get_node("Entry0/Edit") as LineEdit).text = "wss://edited.example"
	(control.get_node("AddEntryButton") as Button).pressed.emit()
	(control.get_node("Entry1/Edit") as LineEdit).text = "wss://two.example"
	var read_back: Variant = ConnectFieldList.value_from_control(
		control,
		TYPE_PACKED_STRING_ARRAY,
	)

	assert_bool(control is LineEdit).is_false()
	assert_that(read_back).is_equal(
		PackedStringArray(["wss://edited.example", "wss://two.example"]),
	)
	control.queue_free()


func test_a_removed_string_list_row_leaves_the_value() -> void:
	var control := ConnectFieldList.make_value_control(
		PackedStringArray(["wss://one.example", "wss://two.example"]),
	)
	add_child(control)
	await get_tree().process_frame

	var remove := control.get_node("Entry0").get_child(1) as Button
	remove.pressed.emit()
	await get_tree().process_frame

	assert_that(
		ConnectFieldList.value_from_control(
			control,
			TYPE_PACKED_STRING_ARRAY,
		),
	).is_equal(PackedStringArray(["wss://two.example"]))
	control.queue_free()


func test_a_structured_field_round_trips_as_json() -> void:
	var authored: Array = [{ "urls": ["stun:stun.example:19302"] }]
	var control := ConnectFieldList.make_value_control(authored)
	var read_back: Variant = ConnectFieldList.value_from_control(
		control,
		TYPE_ARRAY,
	)

	assert_that(read_back).is_equal(authored)
	control.free()


func test_ice_servers_have_one_editable_card_per_server() -> void:
	var authored: Array = [
		{ "urls": ["stun:stun.example:19302"] },
		{
			"urls": ["turn:turn.example:3478"],
			"username": "guest",
			"credential": "secret",
		},
	]
	var control := ConnectFieldList.make_value_control(
		authored,
		&"ice_servers",
	)
	var urls := control.get_node("Server1/Fields/URLsEdit") as LineEdit
	var username := control.get_node("Server1/Fields/UsernameEdit") as LineEdit
	urls.text = "turn:one.example:3478, turns:two.example:5349"
	username.text = "player"
	var read_back: Variant = ConnectFieldList.value_from_control(
		control,
		TYPE_ARRAY,
	)

	assert_bool(control is LineEdit).is_false()
	assert_that(read_back).is_equal(
		[
			{ "urls": ["stun:stun.example:19302"] },
			{
				"urls": [
					"turn:one.example:3478",
					"turns:two.example:5349",
				],
				"username": "player",
				"credential": "secret",
			},
		],
	)
	control.free()


func test_ice_server_add_button_appends_an_editable_card() -> void:
	var control := ConnectFieldList.make_value_control([], &"ice_servers")
	add_child(control)
	await get_tree().process_frame

	(control.get_node("AddServerButton") as Button).pressed.emit()

	assert_that(control.get_node_or_null("Server0")).is_not_null()
	assert_that(
		ConnectFieldList.value_from_control(control, TYPE_ARRAY),
	).is_equal([{ "urls": [] }])
	control.queue_free()


func test_a_malformed_structured_field_keeps_the_default_downstream() -> void:
	var control := ConnectFieldList.make_value_control([] as Array)
	(control as LineEdit).text = "not json"

	assert_that(
		ConnectFieldList.value_from_control(control, TYPE_ARRAY),
	).is_null()
	assert_int(
		await ERROR_SCRUBBER.erase_matching(["'not json' is not a JSON array"]),
	).is_equal(1)
	control.free()


func test_an_installation_seam_is_not_offered_as_a_field() -> void:
	assert_bool(ConnectFieldList.can_author_value(null)).is_false()
	assert_bool(ConnectFieldList.can_author_value(Callable())).is_false()
	assert_bool(ConnectFieldList.can_author_value(RID())).is_false()

	assert_bool(ConnectFieldList.can_author_value("")).is_true()
	assert_bool(ConnectFieldList.can_author_value(0)).is_true()
	assert_bool(
		ConnectFieldList.can_author_value(PackedStringArray()),
	).is_true()
	assert_bool(ConnectFieldList.can_author_value([] as Array)).is_true()
