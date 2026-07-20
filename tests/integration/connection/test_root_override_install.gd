## Tests [method NetwMultiplayer.install_as_default], the root-override install.
##
## Installing at the [SceneTree] default makes every node's
## [member Node.multiplayer] resolve to the session and anchors it at
## [code]/root[/code] so it outlives a native scene change. The original default
## is captured and restored around each test so the override never leaks.
class_name TestRootOverrideInstall
extends NetwTestSuite

var _original: MultiplayerAPI


func before_test() -> void:
	_original = get_tree().get_multiplayer()


func after_test() -> void:
	var installed := get_tree().get_multiplayer() as NetwMultiplayer
	if installed != null and installed != _original:
		installed.dispose()
	get_tree().set_multiplayer(_original)
	super.after_test()


func test_install_sets_the_default_and_resolves() -> void:
	var api := NetwMultiplayer.install_as_default(get_tree())

	assert_that(get_tree().get_multiplayer()).is_same(api)
	assert_that(api.is_active()).is_true()
	assert_that(NetwMultiplayer.live_sessions().has(api)).is_true()

	# Any node in the tree resolves to the session.
	var node := Node.new()
	add_child(node)
	auto_free(node)
	assert_that(NetwMultiplayer.of(node)).is_same(api)


func test_anchor_is_root_so_it_outlives_a_scene_change() -> void:
	var api := NetwMultiplayer.install_as_default(get_tree())

	# The spawn anchor is /root, a sibling of current_scene, so a
	# change_scene_to_* that frees current_scene never touches the session.
	assert_that(api.root_path).is_equal(NodePath("/root"))
	assert_that(api.root).is_same(get_tree().root)


func test_uninstall_restores_a_stock_default() -> void:
	var api := NetwMultiplayer.install_as_default(get_tree())
	assert_that(api.is_active()).is_true()

	NetwMultiplayer.uninstall_default(get_tree())

	assert_that(get_tree().get_multiplayer()).is_not_same(api)
	assert_that(api.is_active()).is_false()
	assert_that(NetwMultiplayer.live_sessions().has(api)).is_false()


func test_tree_less_session_is_ready_to_arm() -> void:
	var api := NetwMultiplayer.install_as_default(get_tree())

	# No MultiplayerTree: the session runs on its defaults, offline until a peer
	# is assigned and a listen host once one connects.
	assert_that(api.root as MultiplayerTree).is_null()
	assert_that(api.session.state).is_equal(NetwSessionInterface.State.OFFLINE)
	assert_that(api.session.desired_role).is_equal(
		NetwSessionInterface.Role.LISTEN_SERVER,
	)
