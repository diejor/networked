## The prediction-contract warning's grace, observed with no frame.
##
## A node declaring both a state set and an input set is claiming a prediction
## contract, and the pipeline sees that claim before a [code]PredictionComponent[/code]
## child has necessarily registered. The report is therefore owed a grace, and
## these cases pin that grace to the session's own settle: the warning fires at
## the pump, and a component registered before that pump takes it back.
class_name TestPredictionContractSettle
extends NetwTestSuite

const DerivedStatePlayer := preload(
	"res://tests/support/sync/derived_state_player.gd"
)

var mt: MultiplayerTree
var pipeline: NetwSyncPipeline


func before_test() -> void:
	mt = MultiplayerTree.new()
	mt.name = "ContractTree"
	add_child(mt)
	auto_free(mt)
	pipeline = mt.api._replication._sync_pipeline


## The pipeline holds the report for the settle rather than firing it inline,
## and the settle is reachable without a frame.
func test_the_contract_warning_fires_at_the_settle_with_no_frame() -> void:
	var node := _declare("PredictedWithoutComponent")

	assert_bool(_check_queued(node)).override_failure_message(
		"a state-plus-input declaration must queue its contract check",
	).is_true()

	await assert_error(
		func() -> void: mt.api._settle(),
	).is_push_warning(
		NetwSyncPipeline._missing_prediction_component_message(
			"PredictedWithoutComponent",
		),
	)

	assert_bool(_check_queued(node)).is_false()


## The grace is the whole point: a component that registers between the
## declaration and the pump satisfies the contract, and nothing is reported.
func test_a_component_added_before_the_pump_takes_the_warning_back() -> void:
	var node := _covered("PredictedWithComponent")

	await assert_error(
		func() -> void: mt.api._settle(),
	).is_success()

	assert_bool(_check_queued(node)).is_false()


## Two nodes declared in one cascade are both checked, which is what keying the
## grace by the node buys. A shared key would report one and forget the other.
## Both satisfy the contract, so the settle they share is silent and the keys
## are the whole evidence.
func test_two_declarations_in_one_cascade_are_both_checked() -> void:
	var first := _covered("FirstCovered")
	var second := _covered("SecondCovered")

	assert_bool(_check_queued(first)).override_failure_message(
		"the first declaration must not lose its check to the second",
	).is_true()
	assert_bool(_check_queued(second)).is_true()

	await assert_error(
		func() -> void: mt.api._settle(),
	).is_success()

	assert_bool(_check_queued(first)).is_false()
	assert_bool(_check_queued(second)).is_false()


# Whether the session holds a contract check for [param node] at its next settle.
func _check_queued(node: Node) -> bool:
	return mt.api._native_core.settle_has_key(
		StringName("sync-prediction-contract?%d" % node.get_instance_id()),
	)


# A node whose script declares one state field and one input field and carries
# no prediction component, which is exactly the contradiction the check reports.
func _declare(node_name: String) -> Node:
	var node: Node = DerivedStatePlayer.new()
	node.name = node_name
	mt.add_child(node)
	auto_free(node)
	pipeline.register_derived(node)
	return node


# The same declaration with the component it claims, so its check finds the
# contract satisfied and the settle it rides is silent.
func _covered(node_name: String) -> Node:
	var node := _declare(node_name)
	var component := PredictionComponent.new()
	component.name = "Prediction"
	node.add_child(component)
	return node
