@tool
## Headless smoke test for [NetwServerBrowser] + [ConnectBrowser]: drive the
## model against a live host and assert its public API (signals, result_of)
## reports a probed direct target as OK, then that a target added through a
## bound model reaches the browser UI.
class_name TestConnectBrowserSmoke
extends NetwTestSuite

const _BROWSER_SCENE := preload(
	"res://addons/networked/connect/ui/connect_browser.tscn"
)


func _smoke_info(_api: NetwMultiplayer) -> NetwServerInfo:
	var info := NetwServerInfo.new()
	info.players = 1
	info.max_players = 8
	info.is_local_listener = true
	return info


func test_model_reports_ok_for_probed_direct_target() -> void:
	var host := await EnetTestSupport.start_host(self, _smoke_info)
	assert_that(host).is_not_empty()

	var tree := MultiplayerTree.new()
	add_child(tree)
	var model := NetwServerBrowser.new(Netw.of(tree))

	var target := NetwConnectTarget.new()
	target.scheme = &"enet"
	target.display_name = "Smoke Host"
	target.address = "127.0.0.1:%d" % host.port
	model.add_target(target)
	model.probe(target)

	@warning_ignore("redundant_await")
	await assert_func(model, "get_result", [target]) \
			.wait_until(3000) \
			.is_not_null()

	var result := model.get_result(target)
	assert_that(result).is_not_null()
	assert_int(result.status).is_equal(NetwProbeResult.Status.OK)
	assert_int(result.info.players).is_equal(1)

	tree.queue_free()
	await EnetTestSupport.stop_tree(host.tree)


# Bound-model path: a browser handed an explicit model drives that model, so a
# target added through it reaches the UI. The browser owns no browse state of
# its own, which is what keeps the model and the session two separate objects.
func test_browser_drives_the_model_it_was_bound_to() -> void:
	var temp_path := "user://_test_connect_browser_%d.tres" % (
			Time.get_ticks_usec()
	)
	var tree := MultiplayerTree.new()
	add_child(tree)

	var model := NetwServerBrowser.new(Netw.of(tree))

	var browser: ConnectBrowser = _BROWSER_SCENE.instantiate()
	browser.server_list_path = temp_path
	browser.bind(Netw.of(tree), model)
	tree.add_child(browser)
	await get_tree().process_frame

	var list_box := browser.get_node("%ListBox") as VBoxContainer
	assert_int(list_box.get_child_count()).is_equal(0)

	# A target added through the bound model reaches the browser UI, proving the
	# browser wired itself to that model's signals.
	var target := NetwConnectTarget.new()
	target.scheme = &"enet"
	target.address = "203.0.113.1"
	model.add_target(target)
	await get_tree().process_frame

	assert_int(list_box.get_child_count()).is_equal(1)

	browser.queue_free()
	tree.queue_free()
	DirAccess.remove_absolute(ProjectSettings.globalize_path(temp_path))
