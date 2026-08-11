## Records the divergence series a client prediction handle reports.
##
## Connects to [signal NetwPredictionHandle.state_evaluated],
## which fires on every receive and carries whether that receive triggered a
## correction, so the scenario reads peak and tail divergence and the correction
## count off one real signal instead of the retired spike's metric fields.
##
## [codeblock]
## divergence_log entry:
##   ┠╴ recv_tick: int    # tick the packet was received on
##   ┠╴ ack: int          # last consumed input tick the server stamped
##   ┠╴ divergence: float # predicted-vs-authoritative error (may be INF)
##   ┖╴ diverged: bool    # the verdict, whether or not anything was written
## [/codeblock]
class_name PredictionObserver
extends RefCounted

## One entry per state receive, in arrival order.
var divergence_log: Array[Dictionary] = []

## Divergences judged, counted from the [param diverged] flag of
## [signal NetwPredictionHandle.state_evaluated].
##
## A verdict, not a write. Several exits judge a divergence and deliberately
## write nothing, and this counts those too -- which is the whole reason C2
## unified the flag. A law that means "the body was corrected" wants
## [member correction_count].
var verdict_count: int = 0

## Corrections the engine actually ran, since the last reset.
##
## Read from the engine's own counter rather than from a signal, because the
## signal reports verdicts now. The baseline is what makes a reset work on a
## number the engine only ever increments.
var correction_count: int:
	get:
		if _prediction == null:
			return 0
		return _prediction.stats.corrections - _corrections_baseline

var _corrections_baseline: int = 0

var _prediction: NetwPredictionHandle


## Binds to [param prediction]'s divergence signals.
func observe(prediction: NetwPredictionHandle) -> void:
	_prediction = prediction
	prediction.state_evaluated.connect(_on_state_evaluated)


## Clears the counted evidence, rebasing the correction baseline on the
## engine's running total.
func reset() -> void:
	divergence_log.clear()
	verdict_count = 0
	_corrections_baseline = _prediction.stats.corrections if _prediction else 0


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
		diverged: bool,
) -> void:
	divergence_log.append(
		{
			&"recv_tick": recv_tick,
			&"ack": ack,
			&"divergence": divergence,
			&"diverged": diverged,
		},
	)
	if diverged:
		verdict_count += 1
