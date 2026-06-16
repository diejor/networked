## Handle for one predicted entity standing across a real loopback.
##
## [PredictionScenario] returns this after composing a matched
## [LagCompSimBody] pair, one per peer, each carrying a real [StateSynchronizer],
## [InputSynchronizer], and [PredictionComponent]. It proxies the metrics a
## scenario asserts on (corrections, replay depth, fire counts, divergence) off
## the live components and the [PredictionObserver], so a test reads the same
## vocabulary the retired spike doubles exposed.
class_name PredictedEntity
extends RefCounted

## Server-side entity root, the consuming peer's [LagCompSimBody].
var server_root: LagCompSimBody
## Client-side entity root, the predicting peer's [LagCompSimBody].
var client_root: LagCompSimBody

var server_entity: NetwEntity
var client_entity: NetwEntity

var server_state: StateSynchronizer
var client_state: StateSynchronizer
var server_input: InputSynchronizer
var client_input: InputSynchronizer
var server_prediction: PredictionComponent
var client_prediction: PredictionComponent

## Divergence recorder bound to the client predictor.
var observer: PredictionObserver

# Scenario back-reference (untyped to avoid a class cycle) and the suite, so
# assert_converged can step ticks and raise GdUnit assertions.
var _scenario: RefCounted
var _suite: NetwTestSuite

# Scripted input: a persistent hold plus per-tick overrides keyed by predict tick.
var _hold_input: Dictionary = { &"motion": Vector2.ZERO, &"bombing": false }
var _input_at: Dictionary = { }


## The consuming peer's body, an alias of [member server_root].
var server_body: LagCompSimBody:
	get:
		return server_root

## The predicting peer's body, an alias of [member client_root].
var client_body: LagCompSimBody:
	get:
		return client_root

## Corrections the client applied since spawn.
var corrections: int:
	get:
		return client_prediction.corrections

## Deepest replay window the client walked.
var max_replay_depth: int:
	get:
		return client_prediction.max_replay_depth

## The client predictor's divergence threshold.
var epsilon: float:
	get:
		return client_prediction.divergence_epsilon

## Fresh fires the client body counted, never re-counted by a replay.
var fire_count: int:
	get:
		return client_root.fire_count

## Fresh fires the server body counted while consuming input.
var server_fire_count: int:
	get:
		return server_root.fire_count

## Inputs the server consumed into authoritative state.
var consumed: int:
	get:
		return server_prediction.consumed_count

## Input ticks the server stepped over as lost.
var missing: int:
	get:
		return server_prediction.missing_count


# Binds the matched root pair and resolves their entity records.
func _bind(server_node: LagCompSimBody, client_node: LagCompSimBody) -> void:
	server_root = server_node
	client_root = client_node
	server_entity = NetwEntity.of(server_node)
	client_entity = NetwEntity.of(client_node)


# Resolves the component slots once both roots are in tree and wired.
func _resolve_slots() -> void:
	server_state = server_entity.state
	client_state = client_entity.state
	server_input = server_entity.input
	client_input = client_entity.input
	server_prediction = server_entity.prediction
	client_prediction = client_entity.prediction


# Returns the scripted input for the upcoming predict [param tick].
func _input_for(tick: int) -> Dictionary:
	return _input_at.get(tick, _hold_input)


## Worst finite divergence the client ever observed.
func peak_divergence() -> float:
	return observer.peak_divergence()


## Worst finite divergence over the last [param n] receives.
func tail_divergence(n: int) -> float:
	return observer.tail_divergence(n)


## Runs the scenario past RTT with no input and asserts the client body
## reconverged onto the server truth.
func assert_converged(ticks: int = 30) -> void:
	_scenario.hold_input(self, { &"motion": Vector2.ZERO, &"bombing": false })
	_scenario.run(ticks)
	var residual := client_root.position.distance_to(server_root.position)
	_suite.assert_float(residual).is_less(1.0)
