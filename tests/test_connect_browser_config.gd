@tool
class_name TestConnectBrowserConfig
extends NetwTestSuite

const BROWSER := preload(
	"res://addons/networked/gdscript/connect/ui/connect_browser.tscn"
)


func test_typed_defaults_reach_every_form_without_mutating_resource() -> void:
	var tree := MultiplayerTree.new()
	add_child(tree)
	var connection: NetwConnectHandle = Netw.connection(tree)
	var config := ConnectENetConfig.new()
	config.port = 34567
	config.max_players = 12
	var browser: ConnectBrowser = BROWSER.instantiate()
	browser.server_list_path = "user://browser_config_test_unused.cfg"
	browser.transport_defaults = [config]
	browser.bind(connection)
	tree.add_child(browser)
	await get_tree().process_frame

	browser._on_host_pressed()
	var host: HostPopup = browser._host_popup
	for at in host._transports.size():
		if host._transports[at].peer_class == &"ENetMultiplayerPeer":
			host._backend_picker.selected = at
			host._populate_settings()
			break
	assert_dict(host._settings.as_dictionary()).contains_key_value(
		"port",
		34567,
	)
	assert_dict(host._settings.as_dictionary()).contains_key_value(
		"max_players",
		12,
	)
	host.hide()
	browser._on_join_direct_pressed()
	var direct: JoinDirectPopup = browser._join_direct_popup
	direct.preset(&"ENetMultiplayerPeer", "localhost")
	assert_dict(direct._settings.as_dictionary()).contains_key_value(
		"port",
		34567,
	)
	direct.hide()
	browser._add_popup.open_add(connection)
	browser._add_popup._select_peer_class(&"ENetMultiplayerPeer")
	browser._add_popup._populate_settings()
	assert_dict(browser._add_popup._settings.as_dictionary()) \
			.contains_key_value("port", 34567)
	browser._add_popup.hide()
	browser._join_popup.open_join(
		connection,
		"Player",
		&"ENetMultiplayerPeer",
		{ port = 45678 },
	)
	assert_dict(browser._join_popup._settings.as_dictionary()) \
			.contains_key_value("port", 45678)
	browser._join_popup.hide()
	assert_int(config.port).is_equal(34567)
	browser._join_popup.open_join(
		connection,
		"Player",
		&"ENetMultiplayerPeer",
	)
	assert_dict(browser._join_popup._settings.as_dictionary()) \
			.contains_key_value("port", 34567)
	tree.queue_free()


func test_webrtc_resource_round_trip_and_native_discovery_boundary() -> void:
	var path := "user://browser_config_%d.tres" % Time.get_ticks_usec()
	var config := ConnectWebRTCConfig.new()
	config.trackers = []
	config.signaling_namespace = "private-game"
	config.ice_servers = []
	config.max_players = 8
	assert_int(ResourceSaver.save(config, path)).is_equal(OK)
	var restored := ResourceLoader.load(
		path,
		"",
		ResourceLoader.CACHE_MODE_IGNORE,
	) as ConnectWebRTCConfig
	assert_str(restored.signaling_namespace).is_equal("private-game")
	assert_that(restored.trackers).is_empty()
	assert_that(restored.ice_servers).is_empty()
	assert_dict(restored.host_settings()).contains_key_value("max_players", 8)
	assert_dict(restored.client_settings()).not_contains_keys("max_players")

	var tree := MultiplayerTree.new()
	add_child(tree)
	var connection: NetwConnectHandle = Netw.connection(tree)
	assert_int(
		connection.transport_set_browse_settings(
			&"WebRTCMultiplayerPeer",
			{ trackers = 42 },
		),
	).is_equal(ERR_INVALID_PARAMETER)
	var browser: ConnectBrowser = BROWSER.instantiate()
	browser.server_list_path = "user://browser_config_test_unused.cfg"
	browser.transport_defaults = [restored]
	browser.bind(connection)
	tree.add_child(browser)
	await get_tree().process_frame
	assert_bool(browser._session_ready).is_true()
	assert_int(
		connection.transport_set_browse_settings(
			&"WebRTCMultiplayerPeer",
			restored.browse_settings(),
		),
	).is_equal(OK)
	restored.signaling_namespace = "another-game"
	assert_int(
		connection.transport_set_browse_settings(
			&"WebRTCMultiplayerPeer",
			restored.browse_settings(),
		),
	).is_equal(ERR_ALREADY_IN_USE)
	tree.queue_free()
	DirAccess.remove_absolute(ProjectSettings.globalize_path(path))
