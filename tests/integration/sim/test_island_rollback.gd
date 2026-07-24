## Law for the reserved joint-island reconciliation seam.
##
## The former rollback recovery policy mixed scope with mechanism. G6 moves
## scope to island reconciliation but keeps JOINT unavailable until a game
## proves contact decisive, error-sensitive, and small-scope.
class_name TestIslandRollback
extends NetwTestSuite

const Handle := NetwLagCompensationInterface.PredictionHandle


func test_joint_reconciliation_names_the_usefulness_gate() -> void:
	var handle := Handle.new()
	handle.island().exact()

	await assert_error(
		func() -> void:
			handle.island().reconcile(Handle.Reconcile.JOINT),
	).is_push_error(
		"PredictionHandle.island().reconcile: JOINT stays reserved until "
		+ "contact is decisive, error-sensitive, and small-scope.",
	)
	assert_int(handle.island_config[&"reconcile"]) \
			.is_equal(Handle.Reconcile.INDEPENDENT)
