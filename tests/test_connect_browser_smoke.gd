@tool
class_name TestConnectBrowserSmoke
extends NetwTestSuite

const _BROWSER_SCENE := preload(
	"res://addons/networked/gdscript/connect/ui/connect_browser.tscn"
)


func _smoke_info(info: NetwServerInfo) -> NetwServerInfo:
	info.players = 1
	info.max_players = 8
	info.is_local_listener = true
	return info


func test_handle_reports_ok_for_probed_direct_target() -> void:
	var host := await EnetTestSupport.start_host(self, _smoke_info)
	assert_that(host).is_not_empty()

	var tree := MultiplayerTree.new()
	add_child(tree)
	var connection := Netw.connection(tree)

	var address := "127.0.0.1:%d" % host.port
	connection.endpoint_add(&"ENetMultiplayerPeer", address, "Smoke Host")
	connection.endpoint_refresh()

	var guard := 0
	while (
			int(
				connection.endpoint(&"ENetMultiplayerPeer", address).get(
					"status",
					FAILED,
				),
			) != OK
			and guard < 180
	):
		await get_tree().process_frame
		guard += 1

	var snapshot := connection.endpoint(&"ENetMultiplayerPeer", address)
	assert_int(int(snapshot.get("status", FAILED))) \
			.override_failure_message("the direct endpoint was never probed.") \
			.is_equal(OK)
	var info: NetwServerInfo = snapshot.get("info", null)
	assert_int(info.players).is_equal(1)

	tree.queue_free()
	await EnetTestSupport.stop_tree(host.tree)


func test_browser_renders_the_handle_it_resolved() -> void:
	var temp_path := "user://_test_connect_browser_%d.cfg" % (
			Time.get_ticks_usec()
	)
	var tree := MultiplayerTree.new()
	add_child(tree)

	var connection := Netw.connection(tree)

	var browser: ConnectBrowser = _BROWSER_SCENE.instantiate()
	browser.server_list_path = temp_path
	browser.bind(connection)
	tree.add_child(browser)
	await get_tree().process_frame

	var list_box := browser.get_node("%ListBox") as VBoxContainer
	assert_int(list_box.get_child_count()).is_equal(0)

	connection.endpoint_add(&"ENetMultiplayerPeer", "203.0.113.1")
	await get_tree().process_frame

	assert_int(list_box.get_child_count()).is_equal(1)

	browser.queue_free()
	tree.queue_free()
	DirAccess.remove_absolute(ProjectSettings.globalize_path(temp_path))


func test_a_join_form_offers_what_the_transport_lets_a_client_author() -> void:
	var tree := MultiplayerTree.new()
	add_child(tree)
	var connection := Netw.connection(tree)

	var enet := connection.transport(&"ENetMultiplayerPeer")
	var client: Dictionary = enet.get("client_settings", { })

	assert_bool(client.has("port")).is_true()
	assert_bool(client.has("max_players")).is_false()

	tree.queue_free()


func test_a_bookmarked_setting_survives_the_file_it_is_saved_to() -> void:
	var temp_path := "user://_test_connect_browser_saved_%d.cfg" % (
			Time.get_ticks_usec()
	)
	var tree := MultiplayerTree.new()
	add_child(tree)
	var connection := Netw.connection(tree)

	var browser: ConnectBrowser = _BROWSER_SCENE.instantiate()
	browser.server_list_path = temp_path
	browser.bind(connection)
	tree.add_child(browser)
	await get_tree().process_frame

	browser._on_endpoint_submitted(
		&"ENetMultiplayerPeer",
		"203.0.113.7",
		"Saved Host",
		{ "port": 40000 },
		&"",
		"",
	)
	await get_tree().process_frame

	var reopened: ConnectBrowser = _BROWSER_SCENE.instantiate()
	reopened.server_list_path = temp_path
	reopened.bind(connection)
	tree.add_child(reopened)
	await get_tree().process_frame

	assert_that(
		reopened._authored_settings(
			&"ENetMultiplayerPeer",
			"203.0.113.7",
		),
	).is_equal({ "port": 40000 })

	browser.queue_free()
	reopened.queue_free()
	tree.queue_free()
	DirAccess.remove_absolute(ProjectSettings.globalize_path(temp_path))


func test_direct_join_needs_no_endpoint_record() -> void:
	var temp_path := "user://_test_connect_browser_direct_%d.cfg" % (
			Time.get_ticks_usec()
	)
	var tree := MultiplayerTree.new()
	add_child(tree)

	var connection := Netw.connection(tree)

	var browser: ConnectBrowser = _BROWSER_SCENE.instantiate()
	browser.server_list_path = temp_path
	browser.bind(connection)
	tree.add_child(browser)
	await get_tree().process_frame

	var address := "127.0.0.1:37515"
	assert_dict(connection.endpoint(&"ENetMultiplayerPeer", address)).is_empty()

	browser._on_join_direct_submitted(
		&"ENetMultiplayerPeer",
		address,
		"",
		{ },
		&"direct_player",
		[],
	)

	var banner := browser.get_node("%Banner") as HBoxContainer
	assert_bool(banner.visible).is_false()

	browser._cancel_setup()
	browser.queue_free()
	tree.queue_free()
	DirAccess.remove_absolute(ProjectSettings.globalize_path(temp_path))


func test_an_authored_impairment_lasts_one_setup() -> void:
	var temp_path := "user://_test_connect_browser_link_%d.cfg" % (
			Time.get_ticks_usec()
	)
	var tree := MultiplayerTree.new()
	add_child(tree)

	var connection := Netw.connection(tree)
	var conditions := NetwLinkConditions.new()
	conditions.simulate_lag = true

	var browser: ConnectBrowser = _BROWSER_SCENE.instantiate()
	browser.server_list_path = temp_path
	browser.debug_link = conditions
	browser.bind(connection)
	tree.add_child(browser)
	await get_tree().process_frame

	var api := Netw.of(tree)
	assert_that(api.session_get_config().link_conditions).is_null()

	var peer := ENetMultiplayerPeer.new()
	assert_that(browser._shaped(peer)).is_not_null()

	browser._on_join_direct_submitted(
		&"ENetMultiplayerPeer",
		"127.0.0.1:37516",
		"",
		{ },
		&"shaped_player",
		[],
	)
	assert_that(api.session_get_config().link_conditions).is_null()

	browser._cancel_setup()
	assert_that(api.session_get_config().link_conditions).is_null()

	browser.queue_free()
	tree.queue_free()
	DirAccess.remove_absolute(ProjectSettings.globalize_path(temp_path))


func test_an_impairment_the_game_authored_wins() -> void:
	var temp_path := "user://_test_connect_browser_authored_%d.cfg" % (
			Time.get_ticks_usec()
	)
	var authored := NetwLinkConditions.new()
	var tree := MultiplayerTree.new()
	tree.link_conditions = authored
	add_child(tree)

	var connection := Netw.connection(tree)

	var browser: ConnectBrowser = _BROWSER_SCENE.instantiate()
	browser.server_list_path = temp_path
	browser.debug_link = NetwLinkConditions.new()
	browser.bind(connection)
	tree.add_child(browser)
	await get_tree().process_frame

	var api := Netw.of(tree)
	assert_that(api.session_get_config().link_conditions).is_not_null()

	var peer := ENetMultiplayerPeer.new()
	assert_bool(browser._shaped(peer) == peer).is_true()

	browser._on_join_direct_submitted(
		&"ENetMultiplayerPeer",
		"127.0.0.1:37517",
		"",
		{ },
		&"authored_player",
		[],
	)
	assert_that(api.session_get_config().link_conditions).is_not_null()

	browser._cancel_setup()
	assert_that(api.session_get_config().link_conditions).is_not_null()

	browser.queue_free()
	tree.queue_free()
	DirAccess.remove_absolute(ProjectSettings.globalize_path(temp_path))


func test_a_string_list_field_round_trips_through_its_control() -> void:
	var authored := PackedStringArray(
		[
			"wss://tracker.openwebtorrent.com",
			"wss://tracker.webtorrent.dev",
		],
	)
	var control := ConnectBrowser.make_value_control(authored)
	var read_back: Variant = ConnectBrowser.value_from_control(
		control,
		TYPE_PACKED_STRING_ARRAY,
	)

	assert_that(read_back).is_equal(authored)
	control.free()


func test_a_string_list_has_one_editable_row_per_entry() -> void:
	var control := ConnectBrowser.make_value_control(
		PackedStringArray(["wss://one.example"]),
	)
	add_child(control)
	await get_tree().process_frame

	(control.get_node("Entry0/Edit") as LineEdit).text = "wss://edited.example"
	(control.get_node("AddEntryButton") as Button).pressed.emit()
	(control.get_node("Entry1/Edit") as LineEdit).text = "wss://two.example"
	var read_back: Variant = ConnectBrowser.value_from_control(
		control,
		TYPE_PACKED_STRING_ARRAY,
	)

	assert_bool(control is LineEdit).is_false()
	assert_that(read_back).is_equal(
		PackedStringArray(
			[
				"wss://edited.example",
				"wss://two.example",
			],
		),
	)
	control.queue_free()


func test_a_removed_string_list_row_leaves_the_value() -> void:
	var control := ConnectBrowser.make_value_control(
		PackedStringArray(["wss://one.example", "wss://two.example"]),
	)
	add_child(control)
	await get_tree().process_frame

	var remove := control.get_node("Entry0").get_child(1) as Button
	remove.pressed.emit()
	await get_tree().process_frame

	assert_that(
		ConnectBrowser.value_from_control(
			control,
			TYPE_PACKED_STRING_ARRAY,
		),
	).is_equal(PackedStringArray(["wss://two.example"]))
	control.queue_free()


func test_a_structured_field_round_trips_as_json() -> void:
	var authored: Array = [{ "urls": ["stun:stun.example:19302"] }]
	var control := ConnectBrowser.make_value_control(authored)
	var read_back: Variant = ConnectBrowser.value_from_control(
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
	var control := ConnectBrowser.make_value_control(authored, &"ice_servers")
	var urls := control.get_node("Server1/Fields/URLsEdit") as LineEdit
	var username := control.get_node(
		"Server1/Fields/UsernameEdit",
	) as LineEdit
	urls.text = "turn:one.example:3478, turns:two.example:5349"
	username.text = "player"
	var read_back: Variant = ConnectBrowser.value_from_control(
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
	var control := ConnectBrowser.make_value_control([], &"ice_servers")
	add_child(control)
	await get_tree().process_frame

	var add_button := control.get_node("AddServerButton") as Button
	add_button.pressed.emit()

	assert_that(control.get_node_or_null("Server0")).is_not_null()
	assert_that(
		ConnectBrowser.value_from_control(
			control,
			TYPE_ARRAY,
		),
	).is_equal([{ "urls": [] }])
	control.queue_free()


func test_a_malformed_structured_field_keeps_the_default_downstream() -> void:
	var control := ConnectBrowser.make_value_control([] as Array)
	(control as LineEdit).text = "not json"
	var read_back: Variant = ConnectBrowser.value_from_control(
		control,
		TYPE_ARRAY,
	)

	assert_that(read_back).is_null()
	control.free()


func test_an_installation_seam_is_not_offered_as_a_field() -> void:
	assert_bool(ConnectBrowser.can_author_value(null)).is_false()
	assert_bool(ConnectBrowser.can_author_value(Callable())).is_false()
	assert_bool(ConnectBrowser.can_author_value(RID())).is_false()

	assert_bool(ConnectBrowser.can_author_value("")).is_true()
	assert_bool(ConnectBrowser.can_author_value(0)).is_true()
	assert_bool(ConnectBrowser.can_author_value(PackedStringArray())).is_true()
	assert_bool(ConnectBrowser.can_author_value([] as Array)).is_true()
