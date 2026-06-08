## Unit tests for [NetwConnect]: signal relay, method forwarding, and
## weakref validity after the underlying [ConnectSession] is freed.
class_name TestNetwConnect
extends NetwTestSuite

func _make_target(address: String = "127.0.0.1") -> JoinTarget:
	var target := JoinTarget.new()
	target.address = address
	target.backend = ENetBackend.new()
	target.display_name = "T_" + address
	return target


func test_relays_target_added() -> void:
	var session := ConnectSession.new()
	add_child(session)
	var facade := NetwConnect.new(session)

	var captured: Array = []
	facade.target_added.connect(func(t): captured.append(t))

	var target := _make_target()
	session.add_target(target)

	assert_int(captured.size()).is_equal(1)
	assert_that(captured[0]).is_same(target)
	session.queue_free()


func test_forwards_add_target_round_trip() -> void:
	var session := ConnectSession.new()
	add_child(session)
	var facade := NetwConnect.new(session)

	var target := _make_target("10.0.0.1")
	facade.add_target(target)

	var targets := facade.get_targets()
	assert_int(targets.size()).is_equal(1)
	assert_that(targets[0]).is_same(target)
	session.queue_free()


func test_is_valid_flips_after_session_freed() -> void:
	var session := ConnectSession.new()
	add_child(session)
	var facade := NetwConnect.new(session)

	assert_bool(facade.is_valid()).is_true()

	session.free()
	assert_bool(facade.is_valid()).is_false()
