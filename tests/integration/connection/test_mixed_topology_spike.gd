## Verifies a root-default host and a subpath-tree client coexist, connect, and
## admit inside one process.
##
## The shipping install anchors one [NetwMultiplayer] at the [SceneTree] default
## (see [method NetwMultiplayer.install_as_default]), while the harness and the
## tiling rig scope each session to a [MultiplayerTree] subtree. Running both at
## once is the in-process shipping-host mode a conformance suite drives: a
## root-default session hosts, a subpath session joins it, both polled by the
## same [SceneTree].
## [codeblock]
## coexist   root-default mount + subpath mount are both live sessions at once,
##           each resolving its own branch. Godot's
##           one-custom-MultiplayerAPI-under-another rule does not bite.
##
## host      the tree-less root session comes online as a listen host through
##           NetwMultiplayer.host, no owning MultiplayerTree required.
##
## admit     the subpath client's join frame is admitted into the host roster,
##           the replication substrate crossing the embedding boundary.
## [/codeblock]
##
## The two mounts talk over the in-process [LocalLoopbackSession] bus, the same
## transport the harness uses. The root override is captured and restored per
## case so it never leaks, the same discipline [TestRootOverrideInstall] uses.
class_name TestMixedTopologySpike
extends NetwTestSuite

const _DT := 1.0 / 60.0

var _original: MultiplayerAPI
var _host_api: NetwMultiplayer
var _client_tree: MultiplayerTree


func before_test() -> void:
	_original = get_tree().get_multiplayer()


func after_test() -> void:
	if is_instance_valid(_client_tree):
		_client_tree.queue_free()
	if _host_api != null and get_tree().get_multiplayer() == _host_api:
		_host_api.multiplayer_peer = null
		_host_api.embedding.dispose()
	_host_api = null
	get_tree().set_multiplayer(_original)
	await drain_frames(get_tree(), 3)
	super.after_test()


# Brings the root-default host online over the loopback bus through the tree-less
# host verb. Returns once the session reports online.
func _install_and_host() -> bool:
	_host_api = NetwMultiplayer.install_as_default(get_tree())
	var payload := JoinPayload.new()
	payload.username = "host"
	var config := NetwHostConfig.new()
	config.transport = NetwLocalParams.new()
	await NetwConnector.of(_host_api).host(payload, config)
	return await _pump_until(func() -> bool: return _host_api.is_online)


# Mounts a subpath-tree client and returns its owned session api.
func _mount_client() -> NetwMultiplayer:
	_client_tree = MultiplayerTree.new()
	_client_tree.name = "SpikeClient"
	_client_tree.desired_role = NetwMultiplayer.Role.CLIENT
	_client_tree.auto_host_headless = false
	_client_tree.transport = NetwLocalParams.new()
	add_child(_client_tree)
	return _client_tree.api


# Joins the client over the loopback bus, returning its join Error.
func _join_client() -> void:
	var payload := JoinPayload.new()
	payload.username = "client"
	var target := NetwConnectTarget.new()
	target.scheme = &"local"
	NetwConnector.of(_client_tree.api).join(target, payload)


# Pumps both mounts for up to [param timeout_ms], returning as soon as
# [param cond] holds. The root host is driven through its tree-less connect kit,
# the subpath client through its own [MultiplayerTree] _process.
func _pump_until(cond: Callable, timeout_ms: int = 3000) -> bool:
	var deadline := Time.get_ticks_msec() + timeout_ms
	while Time.get_ticks_msec() < deadline:
		if cond.call():
			return true
		if _host_api != null:
			_host_api.poll()
			if _host_api.has_multiplayer_peer():
				_host_api.poll()
		await get_tree().process_frame
	return cond.call()


func test_root_and_subpath_mounts_coexist_as_live_sessions() -> void:
	_host_api = NetwMultiplayer.install_as_default(get_tree())
	var client_api := _mount_client()

	# The root override owns the tree default; the client owns its subtree;
	# both are live sessions and neither displaces the other.
	assert_that(get_tree().get_multiplayer()).is_same(_host_api)
	assert_that(get_tree().get_multiplayer(_client_tree.get_path())).is_same(
		client_api,
	)
	assert_that(_host_api.is_active()).is_true()
	assert_that(client_api.is_active()).is_true()
	assert_that(NetwMultiplayer.live_sessions()).contains([_host_api, client_api])
	# A node under the client subtree resolves to the client, not the root host.
	assert_that(NetwMultiplayer.of(_client_tree)).is_same(client_api)


func test_tree_less_root_comes_online_as_a_host() -> void:
	var online := await _install_and_host()
	assert_bool(online).override_failure_message(
		"tree-less root session never came online through NetwMultiplayer.host",
	).is_true()
	assert_that(_host_api.is_host).is_true()
	assert_that(_host_api.get_unique_id()).is_equal(1)


func test_subpath_client_is_admitted_across_the_mount_boundary() -> void:
	var online := await _install_and_host()
	assert_bool(online).is_true()

	var client_api := _mount_client()
	_join_client()

	# The join frame is an RPC round trip across the two mounts: the client
	# submits, the root host admits, and the roster echoes back. Admission on
	# both sides is the substrate crossing the embedding boundary.
	var admitted := await _pump_until(
		func() -> bool: return client_api.local_participant != null
	)
	assert_bool(admitted).override_failure_message(
		"subpath client was never admitted to its own session",
	).is_true()

	var client_id := client_api.get_unique_id()
	var host_sees_client := await _pump_until(
		func() -> bool: return _host_api.peer_get_participant(client_id) != null
	)
	assert_bool(host_sees_client).override_failure_message(
		"root host admitted no roster row for the subpath client",
	).is_true()
