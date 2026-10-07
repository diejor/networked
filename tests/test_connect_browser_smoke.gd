@tool
class_name TestConnectBrowserSmoke
extends NetwTestSuite

const BROWSER := preload(
	"res://addons/networked/gdscript/connect/ui/connect_browser.tscn"
)

var tree: MultiplayerViewport
var connection: NetwConnection
var browser: ConnectBrowser


class AnotherGame:
	extends Node

	func _init() -> void:
		Netw.configure_server_info(answer)


	func answer(info: NetwServerInfo) -> NetwServerInfo:
		info.app_id = &"another-game"
		return info


class SmallServer:
	extends Node

	var max_players := 8

	func _init() -> void:
		Netw.configure_server_info(answer)


	func answer(info: NetwServerInfo) -> NetwServerInfo:
		info.max_players = max_players
		return info


func before_test() -> void:
	tree = MultiplayerViewport.new()
	add_child(tree)
	connection = Netw.session(tree).connection
	browser = BROWSER.instantiate()
	tree.add_child(browser)


func after_test() -> void:
	tree.queue_free()
	await get_tree().process_frame


func test_rows_follow_added_and_removed_endpoints() -> void:
	var endpoint := connection.add(
		&"ENetMultiplayerPeer",
		"203.0.113.1",
		"Alpha",
	)

	assert_int(browser.list_box.get_child_count()).is_equal(1)
	assert_str(browser.rows[endpoint].name_label.text).is_equal("Alpha")
	assert_bool(browser.empty_state.visible).is_false()

	connection.remove(endpoint)
	await get_tree().process_frame

	assert_int(browser.list_box.get_child_count()).is_equal(0)
	assert_bool(browser.empty_state.visible).is_true()


func test_a_probe_answer_reaches_the_row_and_the_join_button() -> void:
	var host := await EnetTestSupport.start_host(self, AnotherGame.new())
	var endpoint := connection.add(
		&"ENetMultiplayerPeer",
		"127.0.0.1:%d" % host.port,
	)
	browser.rows[endpoint].selected.emit()

	assert_bool(browser.details.visible).is_true()
	assert_bool(browser.join_button.disabled).is_false()

	await EnetTestSupport.await_until(
		self,
		func() -> bool: return endpoint.status != NetwEndpoint.STATUS_UNKNOWN,
	)

	assert_int(endpoint.status).is_equal(NetwEndpoint.STATUS_INCOMPATIBLE)
	assert_str(browser.rows[endpoint].status_dot.tooltip_text) \
			.is_equal("Incompatible")
	assert_bool(browser.join_button.disabled).is_true()
	await EnetTestSupport.stop_tree(host.tree)


func test_the_details_panel_follows_the_selected_endpoint() -> void:
	var server := SmallServer.new()
	var host := await EnetTestSupport.start_host(self, server)
	var endpoint := connection.add(
		&"ENetMultiplayerPeer",
		"127.0.0.1:%d" % host.port,
	)
	await EnetTestSupport.await_until(
		self,
		func() -> bool: return endpoint.status == NetwEndpoint.STATUS_ONLINE,
	)
	browser.rows[endpoint].selected.emit()

	assert_str(browser.details_status_label.text).is_equal("Online")
	assert_str(browser.details_ping_label.text).ends_with(" ms")
	assert_str(browser.details_players_label.text).is_equal("0/8")

	server.max_players = 4
	endpoint.probe()
	await EnetTestSupport.await_until(
		self,
		func() -> bool: return endpoint.info.max_players == 4,
	)

	assert_str(browser.details_players_label.text).is_equal("0/4")
	await EnetTestSupport.stop_tree(host.tree)


func test_browsing_follows_the_server_list_visibility() -> void:
	assert_bool(connection.browsing).is_true()

	browser.hide()
	assert_bool(connection.browsing).is_false()

	browser.show()
	assert_bool(connection.browsing).is_true()

	connection.state_changed.emit(NetwConnection.STATE_ONLINE)
	assert_bool(browser.visible).is_true()
	assert_bool(connection.browsing).is_false()

	connection.state_changed.emit(NetwConnection.STATE_OFFLINE)
	assert_bool(connection.browsing).is_true()


func test_the_connecting_popup_follows_state_and_progress() -> void:
	connection.state_changed.emit(NetwConnection.STATE_CREATING)
	assert_bool(browser.connecting_popup.visible).is_true()

	connection.progressed.emit("Asking the tracker", 0.5)
	assert_str(browser.connecting_popup.title_label.text) \
			.is_equal("Asking the tracker")
	assert_float(browser.connecting_popup.progress_bar.value).is_equal(0.5)

	connection.state_changed.emit(NetwConnection.STATE_ONLINE)
	assert_bool(browser.connecting_popup.visible).is_false()


func test_a_failure_reaches_the_banner_and_the_popup() -> void:
	connection.state_changed.emit(NetwConnection.STATE_CONNECTING)
	connection.failed.emit(ERR_CANT_CONNECT, "The server could not be reached.")
	connection.state_changed.emit(NetwConnection.STATE_OFFLINE)
	await get_tree().process_frame

	assert_bool(browser.banner.visible).is_true()
	assert_str(browser.banner_label.text) \
			.is_equal("The server could not be reached.")
	assert_bool(browser.connecting_popup.visible).is_true()
	assert_str(browser.connecting_popup.cancel_button.text).is_equal("Close")

	connection.state_changed.emit(NetwConnection.STATE_CREATING)
	assert_bool(browser.banner.visible).is_false()


func test_the_room_bar_shows_the_code_while_hosting() -> void:
	var room_bar := browser.get_node(^"RoomBar")
	assert_bool(room_bar.panel.visible).is_false()

	browser.host(&"LocalMultiplayerPeer", { }, NetwServerInfo.new(), [])

	assert_int(await EnetTestSupport.await_online(tree)).is_equal(OK)
	assert_bool(room_bar.panel.visible).is_true()
	assert_str(room_bar.code_edit.text).is_equal("local")


func test_a_hidden_room_bar_stays_hidden_while_hosting() -> void:
	var room_bar: Control = browser.get_node(^"RoomBar")
	room_bar.hide()

	browser.host(&"LocalMultiplayerPeer", { }, NetwServerInfo.new(), [])

	assert_int(await EnetTestSupport.await_online(tree)).is_equal(OK)
	assert_bool(room_bar.visible).is_false()


func test_transport_defaults_reach_the_connection() -> void:
	var config := ConnectENetConfig.new()
	config.port = 34567
	config.max_players = 12
	var configured: ConnectBrowser = BROWSER.instantiate()
	configured.transport_defaults = [config]
	tree.add_child(configured)

	var enet := connection.transport(&"ENetMultiplayerPeer")
	assert_int(enet.client_settings.port).is_equal(34567)
	assert_bool(enet.client_settings.has("max_players")).is_false()
	assert_int(enet.host_settings.port).is_equal(34567)
	assert_int(enet.host_settings.max_players).is_equal(12)


func test_a_join_through_the_browser_goes_online_and_hides_the_list() -> void:
	var host := await EnetTestSupport.start_host(self)
	var endpoint := connection.add(
		&"ENetMultiplayerPeer",
		"127.0.0.1:%d" % host.port,
	)

	browser.join(endpoint, { }, [])

	assert_int(await EnetTestSupport.await_online(tree)).is_equal(OK)
	assert_int(connection.state).is_equal(NetwConnection.STATE_ONLINE)
	assert_bool(browser.server_list.visible).is_false()
	assert_bool(browser.connecting_popup.visible).is_false()
	await EnetTestSupport.stop_tree(host.tree)
