## A reliable call that beats its target's spawn waits in the park.
##
## The park is [NetwCallPark] and its own laws are stated natively. What is
## stated here is the wiring: that the dispatch path parks a call for a route it
## does not know yet, that binding the route runs it, and that an unreliable
## frame is dropped instead of parked.
class_name TestDeferredCallPark
extends NetwTestSuite

const _ROUTE := 7

var mt: MultiplayerTree
var api: NetwMultiplayer
var entity: NetwEntity


func before_test() -> void:
	mt = MultiplayerTree.new()
	mt.name = "DeferredCallPark"
	add_child(mt)
	auto_free(mt)
	api = mt.api
	var owner := Node2D.new()
	owner.name = "Target"
	mt.add_child(owner)
	auto_free(owner)
	entity = NetwEntity.ensure(owner)


func _drive_call(reliable: bool) -> void:
	_drive_channel(NetwFrameEnvelope.Channel.CALL, reliable)


func _drive_channel(channel: int, reliable: bool) -> void:
	api._drive_carrier(
		3,
		NetwFrameEnvelope.pack(
			_ROUTE,
			0,
			channel,
			PackedByteArray([1]),
		),
		reliable,
	)


func test_a_reliable_call_for_an_unknown_route_waits_in_the_park() -> void:
	_drive_call(true)

	assert_int(api._rpc_core._park.size()).is_equal(1)
	assert_int(api._rpc_core._park.active_count(3)).is_equal(1)


func test_binding_the_route_runs_the_parked_call() -> void:
	_drive_call(true)
	api._native_core.liveness_bind_route(_ROUTE, entity)
	await await_millis(20)

	# The sweep runs well before the deadline, so a row it drops is one that
	# ran. A call still waiting would survive this sweep.
	api._rpc_core.sweep_deferred_calls()
	assert_int(api._rpc_core._park.size()).is_equal(0)


func test_a_channel_that_asked_to_defer_waits_for_the_route() -> void:
	var seen: Array[int] = []
	api._replication.register_channel(
		120,
		func(_e: NetwEntity, _p: PackedByteArray, s: int) -> void:
			seen.append(s),
		true,
	)

	_drive_channel(120, true)

	assert_int(api._rpc_core._park.size()).is_equal(1)
	assert_array(seen).is_empty()

	api._native_core.liveness_bind_route(_ROUTE, entity)
	await await_millis(20)

	assert_array(seen).contains_exactly([3])


func test_a_channel_that_did_not_ask_drops_at_an_unknown_route() -> void:
	var seen: Array[int] = []
	api._replication.register_channel(
		121,
		func(_e: NetwEntity, _p: PackedByteArray, s: int) -> void:
			seen.append(s),
		false,
	)

	_drive_channel(121, true)

	assert_int(api._rpc_core._park.size()).is_equal(0)
	assert_int(
		api._replication.counters()[&"drops_unknown_route"],
	).is_equal(1)
	assert_array(seen).is_empty()


func test_an_unreliable_call_for_an_unknown_route_is_dropped() -> void:
	_drive_call(false)

	assert_int(api._rpc_core._park.size()).is_equal(0)
	assert_int(
		api._replication.counters()[&"drops_unknown_route"],
	).is_equal(1)
