## Law: the arguments after the username in [method Netw.prepare_join] reach
## the [method Netw.configure_join] handler one for one, and an Array among
## them stays one argument.
class_name TestNetwJoinVarargs
extends NetwTestSuite

const PUMP_TIMEOUT_MS := 5000

var session: NetwHarnessSession
var tree: MultiplayerTree
var declarer: JoinDeclarer


class JoinDeclarer:
	extends Node

	var seen: Array = []
	var calls := 0


	func _init() -> void:
		Netw.configure_join(accepted)


	func accepted(who: NetwPlayer, tag: StringName, payload: Array) -> void:
		calls += 1
		seen = [who, tag, payload]


func before_test() -> void:
	session = NetwHarnessSession.new()
	tree = MultiplayerTree.new()
	tree.name = "JoinVarargTree"
	session.adopt_tree(tree, NetwMultiplayer.ROLE_LISTEN_SERVER)
	add_child(tree)
	declarer = JoinDeclarer.new()
	declarer.name = "Declarer"
	tree.add_child(declarer)
	await get_tree().process_frame


func after_test() -> void:
	if is_instance_valid(tree):
		tree.queue_free()
	if get_tree():
		await NetwTestSuite.drain_frames(get_tree(), 2)
	session.reset()
	await super.after_test()


func test_an_array_argument_stays_one_argument() -> void:
	var payload: Array = [1, 2]
	Netw.prepare_join(declarer, &"ana", &"red", payload)
	await _bring_host_online()
	await _pump_until(func() -> bool: return declarer.calls > 0)

	assert_int(declarer.calls).is_equal(1)
	assert_int(declarer.seen.size()).is_equal(3)
	assert_object(declarer.seen[0]).is_instanceof(NetwPlayer)
	assert_that(declarer.seen[1]).is_equal(&"red")
	assert_that(declarer.seen[2]).is_equal([1, 2])


func test_a_username_alone_passes_no_arguments() -> void:
	Netw.prepare_join(declarer, &"ana")
	await _bring_host_online()
	await _pump_until(func() -> bool: return tree.api.is_online)

	assert_int(declarer.calls).is_equal(0)


func test_a_collected_list_submits_through_a_callable() -> void:
	var collected: Array = [&"blue", [7]]
	var submit: Callable = Netw.prepare_join
	submit.callv([declarer, &"ana"] + collected)
	await _bring_host_online()
	await _pump_until(func() -> bool: return declarer.calls > 0)

	assert_int(declarer.calls).is_equal(1)
	assert_that(declarer.seen[1]).is_equal(&"blue")
	assert_that(declarer.seen[2]).is_equal([7])


func _bring_host_online() -> void:
	var peer := session.session().get_server_peer()
	tree.api.multiplayer_peer = peer
	await _pump_until(func() -> bool: return tree.api.is_online)


func _pump_until(condition: Callable) -> void:
	var deadline := Time.get_ticks_msec() + PUMP_TIMEOUT_MS
	while not condition.call():
		if Time.get_ticks_msec() > deadline:
			return
		await get_tree().process_frame
