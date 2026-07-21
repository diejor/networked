## Laws for the two axes the prediction role compresses into one name.
##
## Whether a peer authors its own command and whether its simulation is the truth
## are independent facts. Naming their pairs is convenient, but the name is the
## derived thing, so these laws hold every role to being reachable from some pair
## and hold the pairs no peer produces today to staying unreachable.
class_name TestPredictRoleAxes
extends NetwTestSuite

const PredictionHandle := NetwLagCompensationInterface.PredictionHandle
const InputSource := PredictionHandle.InputSource
const SimMode := PredictionHandle.SimMode
const Role := PredictionHandle.Role


func test_each_role_is_named_by_the_pair_that_produces_it() -> void:
	assert_int(PredictionHandle.role_for_axes(
		InputSource.LOCAL, SimMode.AUTHORITATIVE,
	)).is_equal(Role.HOST_LOCAL)
	assert_int(PredictionHandle.role_for_axes(
		InputSource.LOCAL, SimMode.SPECULATIVE,
	)).is_equal(Role.PREDICT)
	assert_int(PredictionHandle.role_for_axes(
		InputSource.RECEIVED, SimMode.AUTHORITATIVE,
	)).is_equal(Role.CONSUME)
	assert_int(PredictionHandle.role_for_axes(
		InputSource.NONE, SimMode.DISPLAY,
	)).is_equal(Role.REMOTE)


func test_a_peer_that_simulates_nothing_is_remote_whatever_it_would_read() -> void:
	# The command source stops mattering the moment nothing simulates, so every
	# source paired with DISPLAY has to land on the same name.
	for source in [
		InputSource.LOCAL,
		InputSource.RECEIVED,
		InputSource.PREDICTED,
		InputSource.NONE,
	]:
		assert_int(PredictionHandle.role_for_axes(source, SimMode.DISPLAY)) \
				.override_failure_message(
					"a peer running no simulation needs no command, so where it "
					+ "would have read one cannot change what it is",
				).is_equal(Role.REMOTE)


func test_speculating_on_a_command_nobody_authored_here_stays_unreachable() -> void:
	# This is the shape a future input relay would take: a peer speculating on
	# another peer's command. Nothing produces it today, and until something
	# does it must not quietly acquire the behavior of a role it is not.
	assert_int(PredictionHandle.role_for_axes(
		InputSource.RECEIVED, SimMode.SPECULATIVE,
	)).override_failure_message(
		"a pair no peer produces must resolve to the role that simulates "
		+ "nothing, never to one that would speculate on it",
	).is_equal(Role.REMOTE)
	assert_int(PredictionHandle.role_for_axes(
		InputSource.PREDICTED, SimMode.SPECULATIVE,
	)).is_equal(Role.REMOTE)
	assert_int(PredictionHandle.role_for_axes(
		InputSource.PREDICTED, SimMode.AUTHORITATIVE,
	)).is_equal(Role.REMOTE)


func test_a_fresh_handle_simulates_nothing_until_it_wires() -> void:
	var handle := PredictionHandle.new()

	assert_int(handle.input_source).is_equal(InputSource.NONE)
	assert_int(handle.sim_mode).override_failure_message(
		"an entity that has not resolved its authority yet must not claim to "
		+ "be producing anybody's truth",
	).is_equal(SimMode.DISPLAY)
	assert_int(PredictionHandle.role_for_axes(
		handle.input_source, handle.sim_mode,
	)).is_equal(Role.REMOTE)
