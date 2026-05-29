## Headless smoke test for [ServerBrowser]: instantiate, inject a
## [ServerList] with one direct target pointing at a live host, wait
## for the probe to resolve, assert the row renders OK.
class_name TestServerBrowserSmoke
extends NetwTestSuite


const _BROWSER_SCENE := preload(
	"res://addons/networked/connect/server_browser.tscn"
)


class _SmokeSource:
	extends ServerInfoSource
	func build_server_info(_tree: MultiplayerTree) -> ServerInfo:
		var info := ServerInfo.new()
		info.players = 1
		info.max_players = 8
		info.is_local_listener = true
		return info


func test_browser_populates_direct_row_with_ok_status() -> void:
	var source := _SmokeSource.new()
	var host := await EnetTestSupport.start_host(self, source)
	assert_that(host).is_not_empty()

	var temp_path := "user://_test_browser_smoke_%d.tres" % (
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

	var browser: ServerBrowser = _BROWSER_SCENE.instantiate()
	browser.server_list_path = temp_path
	add_child(browser)

	await wait_until(
		func() -> bool:
			var list_box: VBoxContainer = browser.get_node(
				"VBox/Scroll/ListBox"
			)
			for child in list_box.get_children():
				if child is ServerBrowserRow and child.result != null:
					return true
			return false,
		3.0,
	)

	var list_box: VBoxContainer = browser.get_node("VBox/Scroll/ListBox")
	var row: ServerBrowserRow = null
	for child in list_box.get_children():
		if child is ServerBrowserRow:
			row = child
			break

	assert_that(row).is_not_null()
	assert_that(row.result).is_not_null()
	assert_int(row.result.status).is_equal(ServerInfoResult.Status.OK)
	assert_int(row.result.info.players).is_equal(1)

	browser.queue_free()
	DirAccess.remove_absolute(ProjectSettings.globalize_path(temp_path))
	await EnetTestSupport.stop_tree(host.tree)
