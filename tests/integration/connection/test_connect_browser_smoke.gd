@tool
## Headless smoke test for [ConnectSession] + [ConnectBrowser]:
## drive a session against a live host, assert the public API
## (signals, get_result) reports a probed direct target as OK.
##
## Uses the tree-owned [ConnectSession] path the reference browser resolves
## through [NetwConnect].
class_name TestConnectBrowserSmoke
extends NetwTestSuite

const _BROWSER_SCENE := preload(
	"res://addons/networked/connect/ui/connect_browser.tscn"
)


class _SmokeSource:
	extends ServerInfoSource
	func build_server_info(_tree: MultiplayerTree) -> ServerInfo:
		var info := ServerInfo.new()
		info.players = 1
		info.max_players = 8
		info.is_local_listener = true
		return info


func test_session_reports_ok_for_probed_direct_target() -> void:
	var source := _SmokeSource.new()
	var host := await EnetTestSupport.start_host(self, source)
	assert_that(host).is_not_empty()

	var temp_path := "user://_test_connect_smoke_%d.tres" % (
			Time.get_ticks_usec()
	)

	var target := JoinTarget.new()
	target.display_name = "Smoke Host"
	target.address = "127.0.0.1"
	var client_backend := ENetBackend.new()
	client_backend.port = host.port
	target.backend = client_backend

	var list := ServerList.new()
	list.targets = [target]
	ServerList.save(list, temp_path)

	var tree := MultiplayerTree.new()
	add_child(tree)

	var session := tree.get_connect_session()
	session.load_server_list(temp_path)

	var browser: ConnectBrowser = _BROWSER_SCENE.instantiate()
	browser.tree = tree
	browser.server_list_path = temp_path
	add_child(browser)

	var loaded := session.get_saved_targets()
	assert_int(loaded.size()).is_equal(1)
	var loaded_target: JoinTarget = loaded[0]

	@warning_ignore("redundant_await")
	await assert_func(session, "get_result", [loaded_target]) \
			.wait_until(3000) \
			.is_not_null()

	var result := session.get_result(loaded_target)
	assert_that(result).is_not_null()
	assert_int(result.status).is_equal(ServerInfoResult.Status.OK)
	assert_int(result.info.players).is_equal(1)

	browser.queue_free()
	tree.queue_free()
	DirAccess.remove_absolute(ProjectSettings.globalize_path(temp_path))
	await EnetTestSupport.stop_tree(host.tree)


# Default-resolution path: a browser placed under a MultiplayerTree with no
# injected session resolves and drives the tree's canonical ConnectSession.
func test_browser_resolves_tree_canonical_session() -> void:
	var temp_path := "user://_test_connect_browser_%d.tres" % (
			Time.get_ticks_usec()
	)
	ServerList.save(ServerList.new(), temp_path)

	var tree := MultiplayerTree.new()
	add_child(tree)

	var canonical := tree.get_connect_session()
	assert_that(canonical).is_not_null()

	var browser: ConnectBrowser = _BROWSER_SCENE.instantiate()
	browser.server_list_path = temp_path
	tree.add_child(browser)
	await get_tree().process_frame

	var list_box := browser.get_node("%ListBox") as VBoxContainer
	assert_int(list_box.get_child_count()).is_equal(0)

	# A target added through the canonical session reaches the browser UI,
	# proving the browser bound to the same session's relayed signals.
	var target := JoinTarget.new()
	target.address = "203.0.113.1"
	target.backend = ENetBackend.new()
	canonical.add_target(target)
	await get_tree().process_frame

	assert_int(list_box.get_child_count()).is_equal(1)

	browser.queue_free()
	tree.queue_free()
	DirAccess.remove_absolute(ProjectSettings.globalize_path(temp_path))
