## Records the divergence series a client [PredictionComponent] reports.
##
## Connects to [signal PredictionComponent.state_evaluated] (every receive) and
## [signal PredictionComponent.reconciled] (corrections only), so the scenario
## reads peak and tail divergence and the correction count off real-node signals
## instead of the retired spike's metric fields.
##
## [codeblock]
## divergence_log entry:
##   ┠╴ recv_tick: int    # tick the packet was received on
##   ┠╴ ack: int          # last consumed input tick the server stamped
##   ┠╴ divergence: float # predicted-vs-authoritative error (may be INF)
##   ┖╴ corrected: bool   # whether this receive triggered a snap
## [/codeblock]
class_name PredictionObserver
extends RefCounted

## One entry per state receive, in arrival order.
var divergence_log: Array[Dictionary] = []

## Corrections seen through [signal PredictionComponent.reconciled].
var correction_count: int = 0

var _prediction: PredictionComponent


## Binds to [param prediction]'s divergence signals.
func observe(prediction: PredictionComponent) -> void:
	_prediction = prediction
	prediction.state_evaluated.connect(_on_state_evaluated)
	prediction.reconciled.connect(_on_reconciled)


## Returns the worst finite divergence seen, ignoring the INF first-contact gap.
func peak_divergence() -> float:
	var worst := 0.0
	for entry in divergence_log:
		var d: float = entry[&"divergence"]
		if d != INF:
			worst = maxf(worst, d)
	return worst


## Returns the worst finite divergence over the last [param n] receives.
func tail_divergence(n: int) -> float:
	var worst := 0.0
	var start: int = maxi(0, divergence_log.size() - n)
	for i in range(start, divergence_log.size()):
		var d: float = divergence_log[i][&"divergence"]
		if d != INF:
			worst = maxf(worst, d)
	return worst


func _on_state_evaluated(
		recv_tick: int,
		ack: int,
		divergence: float,
		corrected: bool,
) -> void:
	divergence_log.append(
		{
			&"recv_tick": recv_tick,
			&"ack": ack,
			&"divergence": divergence,
			&"corrected": corrected,
		},
	)


func _on_reconciled(_error: float) -> void:
	correction_count += 1
